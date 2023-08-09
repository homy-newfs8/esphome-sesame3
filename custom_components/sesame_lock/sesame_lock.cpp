#include "sesame_lock.h"
#include <TaskManagerIO.h>
#include <esphome/core/application.h>

using libsesame3bt::Sesame;

namespace {

constexpr uint32_t CONNECT_RETRY_INTERVAL = 3'000;
constexpr uint32_t CONNECT_FAILURE_TIMEOUT = 30'000;
constexpr uint32_t AUTHENTICATE_TIMEOUT = 5'000;
constexpr uint32_t OPERATION_TIMEOUT = 3'000;
constexpr uint32_t JAMMED_TIMEOUT = 3'000;
constexpr uint32_t REBOOT_DELAY = 5'000;

std::string TAG_string;
const char* TAG;

}  // namespace

namespace esphome {
namespace sesame_lock {

void
SesameLock::init(const char* pubkey, const char* secret, const char* btaddr) {
	TAG_string = get_object_id();
	TAG = TAG_string.c_str();
	BLEDevice::init("");
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
			if (esphome::millis() - state_started > CONNECT_FAILURE_TIMEOUT) {
				ESP_LOGE(TAG, "Cannot connect to sesame too long, reboot after 5 secs");
				set_state(state_t::wait_reboot);
				break;
			}
			if (!last_connect_attempted || esphome::millis() - last_connect_attempted >= CONNECT_RETRY_INTERVAL) {
				last_connect_attempted = esphome::millis();
				if (!sesame.connect()) {
					ESP_LOGW(TAG, "Failed to connect sesame");
					break;
				}
				ESP_LOGI(TAG, "Connected to sesame, authenticating");
				set_state(state_t::connecting);
			}
			break;
		case state_t::connecting:
			if (sesame_state == SesameClient::state_t::idle || esphome::millis() - state_started > AUTHENTICATE_TIMEOUT) {
				set_state(state_t::not_connected);
				break;
			}
			if (sesame_state == SesameClient::state_t::active) {
				last_connect_attempted = 0;
				set_state(state_t::running);
				ESP_LOGI(TAG, "Sesame authentication finished");
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
			if (esphome::millis() - state_started > REBOOT_DELAY) {
				mark_failed();
				App.safe_reboot();
			}
			break;
	}
}

}  // namespace sesame_lock
}  // namespace esphome
