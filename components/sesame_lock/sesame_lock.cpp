#include "sesame_lock.h"
#include <Arduino.h>
#include <TaskManagerIO.h>
#include <esphome/core/application.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr uint8_t CONNECT_TIMEOUT_SEC = 5;
constexpr uint32_t CONNECT_RETRY_INTERVAL = 3'000;
constexpr uint32_t CONNECT_STATE_TIMEOUT_MARGIN = 3'000;
constexpr int8_t CONNECT_RETRY_LIMIT = 5;
constexpr uint32_t AUTHENTICATE_TIMEOUT = 5'000;
constexpr uint32_t OPERATION_TIMEOUT = 3'000;
constexpr uint32_t JAMMED_TIMEOUT = 3'000;
constexpr uint32_t REBOOT_DELAY_SEC = 5;

}  // namespace

namespace esphome {
namespace sesame_lock {

bool SesameLock::initialized = SesameLock::static_init();

bool
SesameLock::static_init() {
	if ((ble_connect_mux = xSemaphoreCreateMutexStatic(&ble_connect_mux_))) {
		return true;
	} else {
		return false;
	}
}

void
SesameLock::init(model_t model, const char* pubkey, const char* secret, const char* btaddr, const char* tag) {
	log_tag_string = get_name();
	TAG = log_tag_string.c_str();

	if (model == model_t::sesame_bot) {
		traits.set_supports_open(true);
	}
	default_history_tag = tag;
	++instances;
	if (!SesameLock::initialized) {
		ESP_LOGE(TAG, "Failed to initialize");
		mark_failed();
		return;
	}

	sesame.set_connect_timeout_sec(CONNECT_TIMEOUT_SEC);
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
		taskManager.schedule(onceMillis(0), [this]() { reflect_sesame_status(); });
	});
	if (handle_history()) {
		recv_history_tag.reserve(SesameClient::MAX_CMD_TAG_SIZE + 1);
		sesame.set_history_callback([this](auto& client, const auto& history) {
			recv_history_type = history.type;
			recv_history_tag.assign(history.tag, history.tag_len);
			ESP_LOGD(TAG, "hist: type=%u, str=(%u)%.*s", static_cast<uint8_t>(history.type), history.tag_len, history.tag_len,
			         history.tag);
			taskManager.schedule(onceMillis(0), [this]() { publish_lock_history_state(); });
		});
	}
	set_state(state_t::not_connected);
}

void
SesameLock::setup() {
	ESP_LOGD(TAG, "Start connection");
	BLEDevice::init("");
	if (!xTaskCreateUniversal([](void* self) { static_cast<SesameLock*>(self)->ble_connect_task(); }, "bleconn", 2048, this, 0,
	                          &ble_connect_task_id, CONFIG_ARDUINO_RUNNING_CORE)) {
		ESP_LOGE(TAG, "Failed to start connect task, reboot after 5 secs");
		mark_failed();
		return;
	}
}

void
SesameLock::lock(const char* tag) {
	if (!operable_warn()) {
		return;
	}
	sesame.lock(tag);
}

void
SesameLock::unlock(const char* tag) {
	if (!operable_warn()) {
		return;
	}
	sesame.unlock(tag);
}

void
SesameLock::open(const char* tag) {
	if (!operable_warn()) {
		return;
	}
	sesame.click(tag);
}

void
SesameLock::reflect_sesame_status() {
	if (!sesame_status) {
		return;
	}
	lock::LockState new_lock_state;
	if (sesame_status->in_lock() && sesame_status->in_unlock()) {
		// lock status not determined
		return;
	}
	if (sesame_status->in_unlock()) {
		new_lock_state = lock::LOCK_STATE_UNLOCKED;
	} else if (sesame_status->in_lock()) {
		new_lock_state = lock::LOCK_STATE_LOCKED;
	} else {
		// lock status not determined
		return;
	}
	update_lock_state(new_lock_state);
	if (pct_sensor) {
		pct_sensor->publish_state(sesame_status->battery_pct());
	}
	if (voltage_sensor) {
		voltage_sensor->publish_state(sesame_status->voltage());
	}
}

void
SesameLock::update_lock_state(lock::LockState new_state) {
	if (lock_state == new_state) {
		return;
	}
	lock_state = new_state;
	if (handle_history()) {
		sesame.request_history();
	} else {
		publish_state(lock_state);
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
	publish_state(lock_state);
}

void
SesameLock::set_state(state_t next_state) {
	if (state == next_state) {
		return;
	}
	state = next_state;
	state_started = esphome::millis();
}

bool
SesameLock::operable_warn() const {
	if (state != state_t::running) {
		ESP_LOGW(TAG, "Not connected to SESAME yet, ignored requested action");
		return false;
	}
	return true;
}

void
SesameLock::control(const lock::LockCall& call) {
	if (!operable_warn()) {
		return;
	}
	if (call.get_state()) {
		auto tobe = *call.get_state();
		if (tobe == lock::LOCK_STATE_LOCKED) {
			sesame.lock(default_history_tag);
		} else if (tobe == lock::LOCK_STATE_UNLOCKED) {
			sesame.unlock(default_history_tag);
		}
	}
}

void
SesameLock::open_latch() {
	if (!operable_warn()) {
		return;
	}
	if (sesame.get_model() == model_t::sesame_bot) {
		sesame.click(default_history_tag);
	} else {
		unlock();
	}
}

void
SesameLock::loop() {
	taskManager.runLoop();
	switch (state) {
		case state_t::not_connected:
			if (connect_tried >= CONNECT_RETRY_LIMIT) {
				ESP_LOGE(TAG, "Cannot connect to SESAME %d times, reboot after %u secs", connect_tried, REBOOT_DELAY_SEC);
				set_state(state_t::wait_reboot);
				break;
			}
			if (!last_connect_attempted || esphome::millis() - last_connect_attempted >= CONNECT_RETRY_INTERVAL) {
				last_connect_attempted = esphome::millis();
				++connect_tried;
				ble_connect_result.reset();
				xTaskNotifyGive(ble_connect_task_id);
				set_state(state_t::connecting);
			}
			break;
		case state_t::connecting:
			if (esphome::millis() - state_started > CONNECT_TIMEOUT_SEC * 1000 * instances + CONNECT_STATE_TIMEOUT_MARGIN) {
				ESP_LOGE(TAG, "Connect attempt not finished within expected time, reboot after %u secs", REBOOT_DELAY_SEC);
				set_state(state_t::wait_reboot);
				break;
			}
			if (ble_connect_result.has_value()) {
				if (*ble_connect_result) {
					ESP_LOGI(TAG, "Conncted to SESAME");
					set_state(state_t::authenticating);
				} else {
					ESP_LOGW(TAG, "Failed to connect to SESAME");
					set_state(state_t::not_connected);
				}
			}
			break;
		case state_t::authenticating:
			if (sesame_state == SesameClient::state_t::idle || esphome::millis() - state_started > AUTHENTICATE_TIMEOUT) {
				set_state(state_t::not_connected);
				break;
			}
			connect_tried = 0;
			if (sesame_state == SesameClient::state_t::active) {
				last_connect_attempted = 0;
				set_state(state_t::running);
				ESP_LOGI(TAG, "Authenticated by SESAME");
			}
			break;
		case state_t::running:
			if (sesame_state != SesameClient::state_t::active) {
				set_state(state_t::not_connected);
				break;
			}
			break;
		case state_t::wait_reboot:
			if (esphome::millis() - state_started > REBOOT_DELAY_SEC * 1000) {
				mark_failed();
				App.safe_reboot();
			}
			break;
	}
}

void
SesameLock::ble_connect_task() {
	while (true) {
		ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
		xSemaphoreTake(ble_connect_mux, portMAX_DELAY);
		auto rc = sesame.connect();
		xSemaphoreGive(ble_connect_mux);
		taskManager.scheduleOnce(0, [this, rc]() { ble_connect_result = rc; });
	}
}

}  // namespace sesame_lock
}  // namespace esphome
