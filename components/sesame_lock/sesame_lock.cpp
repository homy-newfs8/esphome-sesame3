#include "sesame_lock.h"
#include <Arduino.h>
#include <TaskManagerIO.h>
#include <esphome/core/application.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using libsesame3bt::Sesame;

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
SesameLock::init(const char* pubkey, const char* secret, const char* btaddr) {
	++instances;
	tag_string = get_name();
	TAG = tag_string.c_str();
	if (!SesameLock::initialized) {
		ESP_LOGE(TAG, "Failed to initialize");
		mark_failed();
		return;
	}

	BLEDevice::init("");
	sesame.set_connect_timeout_sec(CONNECT_TIMEOUT_SEC);
	if (!sesame.begin(BLEAddress(btaddr, BLE_ADDR_RANDOM), Sesame::model_t::sesame_3)) {
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
		taskManager.schedule(onceMillis(0), [this]() { reflect_lock_state(); });
	});
	if (!xTaskCreateUniversal([](void* self) { static_cast<SesameLock*>(self)->ble_connect_task(); }, "bleconn", 2048, this, 0,
	                          &ble_connect_task_id, CONFIG_ARDUINO_RUNNING_CORE)) {
		ESP_LOGE(TAG, "Failed to start connect task, reboot after 5 secs");
		mark_failed();
		return;
	}
	set_state(state_t::not_connected);
}

void
SesameLock::reflect_lock_state() {
	lock::LockState new_lock_state;
	if (sesame_status.in_lock()) {
		if (sesame_status.in_unlock()) {
			new_lock_state = lock::LOCK_STATE_JAMMED;
		} else {
			new_lock_state = lock::LOCK_STATE_LOCKED;
		}
	} else if (sesame_status.in_unlock()) {
		new_lock_state = lock::LOCK_STATE_UNLOCKED;
	} else {
		new_lock_state = lock::LOCK_STATE_JAMMED;
	}
	if (op_started) {
		if (esphome::millis() - op_started > OPERATION_TIMEOUT ||
		    (lock_state == lock::LOCK_STATE_LOCKING && new_lock_state == lock::LOCK_STATE_LOCKED) ||
		    (lock_state == lock::LOCK_STATE_UNLOCKING && new_lock_state == lock::LOCK_STATE_UNLOCKED)) {
			update_lock_state(lock::LOCK_STATE_JAMMED);
			op_started = 0;
		}
		return;
	}
	if (jam_detected) {
		if (new_lock_state != lock::LOCK_STATE_JAMMED || esphome::millis() - jam_detected > JAMMED_TIMEOUT) {
			update_lock_state(new_lock_state);
			jam_detected = 0;
		}
		return;
	}
	if (new_lock_state == lock::LOCK_STATE_JAMMED) {
		jam_detected = esphome::millis();
	} else {
		update_lock_state(new_lock_state);
	}
}

void
SesameLock::update_lock_state(lock::LockState new_state) {
	if (lock_state == new_state) {
		return;
	}
	lock_state = new_state;
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

void
SesameLock::control(const lock::LockCall& call) {
	if (state != state_t::running) {
		ESP_LOGW(TAG, "Not connected to SESAME yet, ignored requested action");
		return;
	}
	if (call.get_state()) {
		auto tobe = *call.get_state();
		if (tobe == lock::LOCK_STATE_LOCKED) {
			sesame.lock("ESPHome");
		} else if (tobe == lock::LOCK_STATE_UNLOCKED) {
			sesame.unlock("ESPHome");
		}
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
			if (op_started || jam_detected) {
				reflect_lock_state();
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
