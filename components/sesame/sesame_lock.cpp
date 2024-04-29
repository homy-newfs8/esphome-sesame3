#include "sesame_lock.h"
#include <Arduino.h>
#include <esphome/core/application.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string_view>

namespace {

constexpr uint32_t CONNECT_RETRY_INTERVAL = 3'000;
constexpr uint32_t CONNECT_STATE_TIMEOUT_MARGIN = 3'000;
constexpr uint32_t AUTHENTICATE_TIMEOUT = 5'000;
constexpr uint32_t OPERATION_TIMEOUT = 3'000;
constexpr uint32_t JAMM_DETECTION_TIMEOUT = 3'000;
constexpr uint32_t REBOOT_DELAY_SEC = 5;
constexpr uint32_t HISTORY_TIMEOUT = 3'000;

constexpr const char* STATIC_TAG = "sesame_lock";

}  // namespace

using libsesame3bt::Sesame;
using libsesame3bt::SesameClient;
using model_t = Sesame::model_t;
using esphome::lock::LockState;

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
SesameLock::publish_lock_state(bool force_publish) {
	auto st = lock_state;
	if (st == LockState::LOCK_STATE_NONE) {
		st = unknown_state_alternative;
	}
	if (state == st && force_publish) {
		state_callback_.call();
	}
	publish_state(st);
}

void
SesameComponent::setup() {
	if (lock) {
		lock->publish_lock_state(true);
	}
}

void
SesameLock::lock(std::string_view tag) {
	if (!operable_warn()) {
		return;
	}
	parent_->sesame.lock(tag);
}

void
SesameLock::unlock(std::string_view tag) {
	if (!operable_warn()) {
		return;
	}
	parent_->sesame.unlock(tag);
}

void
SesameLock::open(std::string_view tag) {
	if (!operable_warn()) {
		return;
	}
	parent_->sesame.click(tag);
}

void
SesameLock::reflect_sesame_status() {
	const auto& sesame_status = parent_->sesame_status;
	if (!sesame_status) {
		update_lock_state(LockState::LOCK_STATE_NONE);
		return;
	}
	lock::LockState new_lock_state;
	if (sesame_status->in_lock() && sesame_status->in_unlock()) {
		if (!jam_detection_started && lock_state != LockState::LOCK_STATE_JAMMED) {
			jam_detection_started = esphome::millis();
		}
		return;
	}
	if (sesame_status->in_unlock()) {
		new_lock_state = lock::LOCK_STATE_UNLOCKED;
	} else if (sesame_status->in_lock()) {
		new_lock_state = lock::LOCK_STATE_LOCKED;
	} else {
		// lock status not determined
		if (!jam_detection_started && lock_state != LockState::LOCK_STATE_JAMMED) {
			jam_detection_started = esphome::millis();
		}
		return;
	}
	update_lock_state(new_lock_state);
}

void
SesameComponent::reflect_sesame_status() {
	if (pct_sensor) {
		pct_sensor->publish_state(sesame_status->battery_pct());
	}
	if (voltage_sensor) {
		voltage_sensor->publish_state(sesame_status->voltage());
	}
	if (lock) {
		lock->reflect_sesame_status();
	}
}

void
SesameLock::update_lock_state(lock::LockState new_state, bool force_publish) {
	if (!force_publish && lock_state == new_state) {
		return;
	}
	lock_state = new_state;
	if (!force_publish && lock_state != LockState::LOCK_STATE_NONE && handle_history()) {
		if (!parent_->sesame.request_history()) {
			ESP_LOGW(TAG, "Failed to request history");
			publish_lock_state(force_publish);
		}
	} else {
		publish_lock_state(force_publish);
	}
}

void
SesameLock::publish_lock_history_state() {
	if (history_type_sensor) {
		history_type_sensor->publish_state(static_cast<uint8_t>(recv_history_type));
	}
	if (history_tag_sensor) {
		history_tag_sensor->publish_state(recv_history_tag);
	}
	publish_lock_state();
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

bool
SesameLock::operable_warn() const {
	if (parent_->my_state != state_t::running) {
		ESP_LOGW(TAG, "Not connected to SESAME yet, ignored requested action");
		return false;
	}
	return true;
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
SesameLock::control(const lock::LockCall& call) {
	if (!operable_warn()) {
		return;
	}
	if (call.get_state()) {
		auto tobe = *call.get_state();
		if (tobe == lock::LOCK_STATE_LOCKED) {
			parent_->sesame.lock(default_history_tag);
		} else if (tobe == lock::LOCK_STATE_UNLOCKED) {
			parent_->sesame.unlock(default_history_tag);
		}
	}
}

void
SesameLock::open_latch() {
	if (!operable_warn()) {
		return;
	}
	if (parent_->sesame.get_model() == model_t::sesame_bot) {
		parent_->sesame.click(default_history_tag);
	} else {
		unlock();
	}
}

void
SesameComponent::loop() {
	auto now = esphome::millis();
	if (lock) {
		lock->test_unknown_state();
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
			if (lock) {
				lock->test_timeout();
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
	ESP_LOGD(TAG, "update");
	if (my_state == state_t::running) {
		sesame.request_status();
	} else if (my_state == state_t::not_connected) {
		operation_requested.update_status = true;
	}
}

SesameLock::SesameLock(SesameComponent* parent, model_t model, const char* tag) : parent_(parent) {
	TAG = parent->TAG;
	if (model == model_t::sesame_bot) {
		traits.set_supports_open(true);
	}
	default_history_tag = tag;
}

void
SesameLock::init() {
	if (handle_history()) {
		recv_history_tag.reserve(SesameClient::MAX_CMD_TAG_SIZE + 1);
		parent_->sesame.set_history_callback([this](auto& client, const auto& history) {
			recv_history_type = history.type;
			recv_history_tag.assign(history.tag, history.tag_len);
			ESP_LOGD(TAG, "hist: type=%u, str=(%u)%.*s", static_cast<uint8_t>(history.type), history.tag_len, history.tag_len,
			         history.tag);
			parent_->set_timeout(0, [this]() { publish_lock_history_state(); });
		});
	}
}

void
SesameLock::test_unknown_state() {
	if (parent_->sesame_status.has_value()) {
		unknown_state_started = 0;
	} else {
		if (lock_state != lock::LOCK_STATE_NONE) {
			auto now = esphome::millis();
			if (unknown_state_started == 0) {
				unknown_state_started = now;
			} else if (unknown_state_timeout_sec && now - unknown_state_started > unknown_state_timeout_sec * 1000) {
				reflect_sesame_status();
				unknown_state_started = 0;
			}
		}
	}
}

void
SesameLock::test_timeout() {
	auto now = esphome::millis();
	if (HISTORY_TIMEOUT) {
		if (last_history_requested && now - last_history_requested > HISTORY_TIMEOUT) {
			ESP_LOGW(TAG, "History not received");
			recv_history_type = Sesame::history_type_t::none;
			recv_history_tag.clear();
			publish_lock_history_state();
			last_history_requested = 0;
		}
	}
	if (JAMM_DETECTION_TIMEOUT) {
		if (jam_detection_started && now - jam_detection_started > JAMM_DETECTION_TIMEOUT) {
			ESP_LOGW(TAG, "Locking state not determined too long, treat as jammed");
			update_lock_state(LockState::LOCK_STATE_JAMMED);
			jam_detection_started = 0;
		}
	}
}

}  // namespace sesame_lock
}  // namespace esphome
