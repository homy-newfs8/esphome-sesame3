#include "sesame_component.h"
#include <Arduino.h>
#include <esphome/core/application.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string_view>

namespace {

constexpr uint32_t CONNECT_RETRY_INTERVAL = 3'000;
constexpr uint32_t CONNECT_STATE_TIMEOUT_MARGIN = 3'000;
constexpr uint32_t AUTHENTICATE_TIMEOUT = 5'000;
constexpr uint32_t REBOOT_DELAY_SEC = 5;

constexpr const char* STATIC_TAG = "sesame_lock";

}  // namespace

using libsesame3bt::Sesame;
using libsesame3bt::SesameClient;
using model_t = Sesame::model_t;

namespace esphome {
namespace sesame_lock {

bool SesameComponent::initialized = SesameComponent::static_init();

bool
SesameComponent::static_init() {
	if (!xTaskCreateUniversal(connect_task, "bleconn", 2048, nullptr, 0, &ble_connect_task_id, CONFIG_ARDUINO_RUNNING_CORE)) {
		ESP_LOGE(STATIC_TAG, "Failed to start connect task");
		return false;
	}
	return true;
}

SesameComponent::SesameComponent(const char* id) {
	log_tag_string = id;
	TAG = log_tag_string.c_str();
}

void
SesameComponent::init(model_t model, const char* pubkey, const char* secret, const char* btaddr) {
	if (!SesameComponent::initialized) {
		ESP_LOGE(TAG, "Failed to initialize");
		mark_failed();
		return;
	}
	sesame.set_connect_timeout_sec(connection_timeout_sec);
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
	sesame.set_state_callback([this](auto& client, auto state) { sesame_state = state; });
	sesame.set_status_callback([this](auto& client, auto status) {
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
	if (feature) {
		feature->publish_initial_state();
	}
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
				ESP_LOGE(TAG, "Cannot connect to SESAME %d times, reboot after %u secs", connect_tried, REBOOT_DELAY_SEC);
				set_state(state_t::wait_reboot);
				break;
			}
			if (always_connect || operation_requested.value != 0) {
				if (!last_connect_attempted || now - last_connect_attempted >= CONNECT_RETRY_INTERVAL) {
					if (enqueue_connect(this)) {
						++connect_tried;
						set_state(state_t::connecting);
					}
				}
			}
			break;
		case state_t::connecting:
			if (now - state_started > connection_timeout_sec * 1000 + CONNECT_STATE_TIMEOUT_MARGIN) {
				ESP_LOGE(TAG, "Connect attempt not finished within expected time, reboot after %u secs", REBOOT_DELAY_SEC);
				set_state(state_t::wait_reboot);
				break;
			}
			break;
		case state_t::authenticating:
			if (sesame_state == SesameClient::state_t::idle || now - state_started > AUTHENTICATE_TIMEOUT) {
				disconnect();
				break;
			}
			if (sesame_state == SesameClient::state_t::active) {
				connect_tried = 0;
				last_connect_attempted = 0;
				set_state(state_t::running);
				publish_connection_state(true);
				ESP_LOGI(TAG, "Authenticated by SESAME");
			}
			break;
		case state_t::running:
			if (!always_connect && operation_requested.value == 0) {
				disconnect();
				break;
			}
			if (sesame_state != SesameClient::state_t::active) {
				disconnect();
				break;
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

void
SesameComponent::connect() {
	ESP_LOGD(TAG, "connecting");
	auto rc = sesame.connect();
	last_connect_attempted = esphome::millis();
	if (rc) {
		ESP_LOGD(TAG, "connect done");
		set_state(state_t::authenticating);
	} else {
		ESP_LOGW(TAG, "connect failed");
		set_state(state_t::not_connected);
	}
}

void
SesameComponent::connect_task(void*) {
	for (;;) {
		ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
		SesameComponent* client;
		{
			std::lock_guard lock{ble_connecting_mux};
			client = ble_connecting_client;
		}
		if (!client) {
			continue;
		}
		BLEDevice::init("");
		client->connect();
		{
			std::lock_guard lock{ble_connecting_mux};
			ble_connecting_client = nullptr;
		}
	}
}

bool
SesameComponent::enqueue_connect(SesameComponent* client) {
	std::lock_guard lock(ble_connecting_mux);
	if (ble_connecting_client) {
		return false;
	}
	ble_connecting_client = client;
	xTaskNotifyGive(ble_connect_task_id);
	return true;
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
