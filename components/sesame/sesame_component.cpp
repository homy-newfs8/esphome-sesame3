#include "sesame_component.h"
#include <esphome/core/application.h>
#include <esphome/core/log.h>
#include <algorithm>

namespace {

constexpr uint32_t CONNECT_RETRY_INTERVAL = 3'000;
constexpr uint32_t CONNECT_STATE_TIMEOUT_MARGIN = 5'000;
constexpr uint32_t AUTHENTICATE_TIMEOUT = 5'000;
constexpr uint32_t REBOOT_DELAY_SEC = 5;

constexpr const char* STATIC_TAG = "sesame_lock";

}  // namespace

using libsesame3bt::Sesame;
using libsesame3bt::SesameClient;
using model_t = Sesame::model_t;

namespace esphome {
namespace sesame_lock {

void
SesameComponent::global_init() {
	if (global_initialized) {
		return;
	}
	connect_queue.reserve(instance_count);
	global_initialized = true;
}

SesameComponent::SesameComponent(const char* id) {
	log_tag_string = id;
	TAG = log_tag_string.c_str();
	++instance_count;
}

void
SesameComponent::init(model_t model, const char* pubkey, const char* secret, const char* btaddr) {
	sesame.set_connect_timeout(connection_timeout);
	if (!sesame.begin(BLEAddress(btaddr, BLE_ADDR_RANDOM), model)) {
		ESP_LOGE(TAG, "Failed to SesameClient::begin. May be unsupported model.");
		mark_failed();
		return;
	}
	if (!sesame.set_keys(pubkey, secret)) {
		ESP_LOGE(TAG, "Failed to set keys. Invalid pubkey or secret.");
		mark_failed();
		return;
	}
	sesame.set_status_callback([this](auto& client, auto status) {
		ESP_LOGD(TAG, "Status in_lock=%u,in_unlock=%u,tgt=%d,pos=%d,mot=%u,ret=%u", status.in_lock(), status.in_unlock(),
		         status.target(), status.position(), static_cast<uint8_t>(status.motor_status()), status.ret_code());
		sesame_status = status;
		set_timeout(0, [this]() {
			reflect_sesame_status();
			operation_requested.update_status = false;
		});
	});
	set_state(state_t::not_connected);
}

void
SesameComponent::setup() {
	global_init();
	if (feature) {
		feature->publish_initial_state();
	}
	BLEDevice::init("");
}

void
SesameComponent::reflect_sesame_status() {
	if (pct_sensor) {
		pct_sensor->publish_state(sesame_status->battery_pct());
	}
	if (voltage_sensor) {
		voltage_sensor->publish_state(sesame_status->voltage());
	}
	if (feature) {
		feature->reflect_status_changed();
	}
}

void
SesameComponent::set_state(state_t next_state) {
	if (my_state == next_state) {
		return;
	}
	if (my_state == state_t::wait_reboot) {
		return;
	}
	my_state = next_state;
	state_started = esphome::millis();
}

void
SesameComponent::disconnect() {
	sesame.disconnect();
	sesame_status.reset();
	set_state(state_t::not_connected);
	publish_connection_state(false);
	ESP_LOGI(TAG, "Disconnected");
}

void
SesameComponent::loop() {
	auto now = esphome::millis();
	if (feature) {
		feature->loop();
	}
	switch (my_state) {
		case state_t::not_connected:
			publish_connection_state(false);
			if (connect_limit && connect_tried >= connect_limit) {
				ESP_LOGE(TAG, "Cannot connect %d times, reboot after %u secs", connect_tried, REBOOT_DELAY_SEC);
				set_state(state_t::wait_reboot);
				break;
			}
			if (always_connect || operation_requested.value != 0) {
				if (!last_connect_attempted || now - last_connect_attempted >= CONNECT_RETRY_INTERVAL) {
					last_connect_attempted = now;
					enqueue_connect(this);
					set_state(state_t::wait_connect);
				}
			}
			break;
		case state_t::wait_connect:
			if (can_connect(this)) {
				ESP_LOGD(TAG, "Connecting");
				++connect_tried;
				if (sesame.connect_async()) {
					set_state(state_t::connecting);
				} else {
					ESP_LOGW(TAG, "Failed to start connect rc=%d", get_last_error());
					connect_done(this);
					disconnect();
				}
			}
			break;
		case state_t::connecting:
			if (now - state_started > connection_timeout + CONNECT_STATE_TIMEOUT_MARGIN) {
				ESP_LOGE(TAG, "Connect attempt not finished within expected time, reboot after %u secs", REBOOT_DELAY_SEC);
				connect_done(this);
				set_state(state_t::wait_reboot);
				break;
			}
			if (sesame.get_state() == SesameClient::state_t::connected) {
				ESP_LOGI(TAG, "Connected");
				connect_done(this);
				if (sesame.start_authenticate()) {
					set_state(state_t::authenticating);
				} else {
					ESP_LOGW(TAG, "Failed to start authenticate, rc=%d", get_last_error());
					disconnect();
				}
			} else if (sesame.get_state() != SesameClient::state_t::connecting) {
				ESP_LOGW(TAG, "Failed to connect, rc=%d", get_last_error());
				connect_done(this);
				disconnect();
			}
			break;
		case state_t::authenticating:
			if (sesame.get_state() == SesameClient::state_t::active) {
				connect_tried = 0;
				last_connect_attempted = 0;
				set_state(state_t::running);
				publish_connection_state(true);
				ESP_LOGI(TAG, "Authenticated");
			} else if (sesame.get_state() != SesameClient::state_t::authenticating || now - state_started > AUTHENTICATE_TIMEOUT) {
				ESP_LOGW(TAG, "Failed to authenticate");
				disconnect();
			}
			break;
		case state_t::running:
			if (!always_connect && operation_requested.value == 0) {
				disconnect();
			} else if (sesame.get_state() != SesameClient::state_t::active) {
				disconnect();
			}
			break;
		case state_t::wait_reboot:
			if (now - state_started > REBOOT_DELAY_SEC * 1000) {
				mark_failed();
				App.safe_reboot();
			}
			break;
	}
}

void
SesameComponent::publish_connection_state(bool connected) {
	if (connection_sensor) {
		connection_sensor->publish_state(connected);
	}
}

bool
SesameComponent::enqueue_connect(SesameComponent* client) {
	std::lock_guard lock(ble_connecting_mux);
	connect_queue.push_back(client);
	return connect_queue.front() == client;
}

void
SesameComponent::connect_done(SesameComponent* client) {
	std::lock_guard lock(ble_connecting_mux);
	if (client == connect_queue.front()) {
		connect_queue.erase(std::cbegin(connect_queue));
		return;
	}
	ESP_LOGD(STATIC_TAG, "Connection queue mishandled");
	std::remove(std::begin(connect_queue), std::end(connect_queue), client);
	return;
}

bool
SesameComponent::can_connect(SesameComponent* client) {
	std::lock_guard lock(ble_connecting_mux);
	return client == connect_queue.front();
}

void
SesameComponent::update() {
	if (my_state == state_t::running) {
		sesame.request_status();
	} else if (my_state == state_t::not_connected) {
		operation_requested.update_status = true;
	}
}

}  // namespace sesame_lock
}  // namespace esphome
