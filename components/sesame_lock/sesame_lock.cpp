#include "sesame_lock.h"
#include <Arduino.h>
#include <TaskManagerIO.h>
#include <esphome/core/application.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr uint32_t discover_timeout = 180'000;
constexpr uint32_t CONNECT_TIMEOUT = 10'000;
constexpr uint32_t DISCONNECT_TIMEOUT = 10'000;
constexpr uint32_t AUTH_TIMEOUT = 5'000;
constexpr uint32_t REBOOT_DELAY = 3'000;
constexpr int8_t connect_limit = 10;
constexpr uint32_t CONNECT_INTERVAL = 5'000;

constexpr uint32_t
sec(uint32_t ms) {
	return ms / 1000;
}

}  // namespace

using esphome::esp32_ble::ESPBTUUID;
using libsesame3bt::Sesame;
using libsesame3bt::core::SesameClientCore;
namespace espbt = esphome::esp32_ble_tracker;

namespace esphome {
namespace sesame_lock {

SesameLock::SesameLock() : sesame(*this) {}

void
SesameLock::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t* param) {
	switch (event) {
		case ESP_GATTC_OPEN_EVT: {
			ESP_LOGD(TAG, "ESP_GATTC_OPEN_EVT");
			if (param->open.status == ESP_GATT_OK) {
				ESP_LOGI(TAG, "Connected");
			} else {
				ESP_LOGW(TAG, "Open failed, connect again");
				set_state(state_t::connect_again);
			}
			break;
		}

		case ESP_GATTC_CLOSE_EVT:
			ESP_LOGD(TAG, "ESP_GATTC_CLOSE_EVT");
			set_state(state_t::disconnected);
			sesame.on_disconnected();
			break;

		case ESP_GATTC_SEARCH_CMPL_EVT: {
			ESP_LOGD(TAG, "ESP_GATTC_SEARCH_CMPL_EVT");
			if (param->search_cmpl.status != ESP_GATT_OK) {
				ESP_LOGW(TAG, "GATT Search failed, disconnect");
				set_state(state_t::disconnecting);
				disconnect();
				break;
			}
			auto* srv = this->parent()->get_service(ESPBTUUID::from_raw(Sesame::SESAME3_SRV_UUID));
			if (srv == nullptr) {
				ESP_LOGE(TAG, "No SESAME Service registered, confirm device MAC ADDRESS");
				mark_failed();
				break;
			}
			auto* tx = srv->get_characteristic(ESPBTUUID::from_raw(Sesame::TxUUID));
			if (tx == nullptr) {
				ESP_LOGE(TAG, "Failed to get TX char, confirm device MAC ADDRESS");
				mark_failed();
				break;
			}
			tx_handle = tx->handle;

			auto* rx = srv->get_characteristic(ESPBTUUID::from_raw(Sesame::RxUUID));
			if (rx == nullptr) {
				ESP_LOGE(TAG, "Failed to get RX char, confirm device MAC ADDRESS");
				mark_failed();
				break;
			}
			rx_handle = rx->handle;
			auto reg_status =
			    esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(), this->parent()->get_remote_bda(), rx->handle);
			if (reg_status) {
				ESP_LOGW(TAG, "Failed to subscribe notification, connect again");
				set_state(state_t::disconnecting);
				disconnect();
				break;
			}
			break;
		}

		case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
			ESP_LOGD(TAG, "ESP_GATTC_REG_FOR_NOTIFY_EVT");
			this->node_state = espbt::ClientState::ESTABLISHED;
			if (param->reg_for_notify.status != ESP_GATT_OK) {
				ESP_LOGW(TAG, "Failed to register to notification, status=%u", param->reg_for_notify.status);
				disconnect();
				set_state(state_t::disconnecting);
				break;
			}
			set_state(state_t::authenticating);
			sesame.on_connected();
			break;
		}

		case ESP_GATTC_NOTIFY_EVT: {
			ESP_LOGD(TAG, "ESP_GATTC_NOTIFY_EVT");
			if (param->notify.handle == rx_handle) {
				ESP_LOGV(TAG, "received %ub", param->notify.value_len);
				sesame.on_received(reinterpret_cast<std::byte*>(param->notify.value), param->notify.value_len);
			}
			break;
		}

		default:
			ESP_LOGD(TAG, "GATTC event = %u", static_cast<uint8_t>(event));
			break;
	}
}

void
SesameLock::init(model_t model, const char* pubkey, const char* secret, const char* tag) {
	log_tag_string = get_name();
	TAG = log_tag_string.c_str();
	ESP_LOGD(TAG, "init()");

	if (model == model_t::sesame_bot) {
		traits.set_supports_open(true);
	}
	default_history_tag = tag;

	if (!sesame.begin(model)) {
		ESP_LOGE(TAG, "Invalid model.");
		mark_failed();
		return;
	}
	if (!sesame.set_keys(pubkey, secret)) {
		ESP_LOGE(TAG, "Failed to set keys. Invalid pubkey or secret.");
		mark_failed();
		return;
	}
	sesame.set_status_callback([this](auto& client, auto status) {
		sesame_status = status;
		taskManager.schedule(onceMillis(0), [this]() { reflect_sesame_status(); });
	});
	if (handle_history()) {
		recv_history_tag.reserve(Sesame::MAX_HISTORY_TAG_SIZE + 1);
		sesame.set_history_callback([this](auto& client, const auto& history) {
			recv_history_type = history.type;
			recv_history_tag.assign(history.tag, history.tag_len);
			ESP_LOGD(TAG, "hist: type=%u, str=(%u)%.*s", static_cast<uint8_t>(history.type), history.tag_len, history.tag_len,
			         history.tag);
			taskManager.schedule(onceMillis(0), [this]() { publish_lock_history_state(); });
		});
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
SesameLock::set_state(state_t next_state, bool force) {
	if (!force && state == next_state) {
		return;
	}
	if (state == state_t::wait_reboot) {
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
SesameLock::component_loop() {
	taskManager.runLoop();
	if (state == state_t::discovering && parent()->state() == espbt::ClientState::CONNECTING) {
		set_state(state_t::connecting);
	}
	auto now = esphome::millis();
	switch (state) {
		case state_t::connect_again:
			if (connect_limit) {
				if (connect_tried >= connect_limit) {
					ESP_LOGE(TAG, "Connect failed %u times, reboot after %u seconds", connect_tried, sec(REBOOT_DELAY));
					set_state(state_t::wait_reboot);
					break;
				}
			}
			if (auto elapsed = now - state_started; elapsed > CONNECT_INTERVAL) {
				connect();
				set_state(state_t::connecting);
			}
			break;
		case state_t::discovering:
			if (discover_timeout) {
				if (auto elapsed = now - state_started; elapsed > discover_timeout) {
					ESP_LOGE(TAG, "SESAME not discovered too long (%u), reboot after %u seconds", sec(elapsed), sec(REBOOT_DELAY));
					set_state(state_t::wait_reboot);
				}
			}
			break;
		case state_t::connecting:
			if (auto elapsed = now - state_started; elapsed > CONNECT_TIMEOUT) {
				ESP_LOGW(TAG, "Connection not established %u seconds. try again", sec(elapsed));
				disconnect();
				set_state(state_t::connect_again);
			}
			break;
		case state_t::disconnecting:
			if (auto elapsed = now - state_started; elapsed > DISCONNECT_TIMEOUT) {
				ESP_LOGW(TAG, "Disconnect takes too long (%u), connect again", sec(elapsed));
				set_state(state_t::connect_again);
			}
			break;
		case state_t::disconnected:
			ESP_LOGI(TAG, "SESAME Disconnected, connect again");
			set_state(state_t::connect_again);
			break;
		case state_t::authenticating:
			if (sesame.is_session_active()) {
				set_state(state_t::running);
				connect_tried = 0;
				break;
			}
			if (auto elapsed = now - state_started; elapsed > AUTH_TIMEOUT) {
				ESP_LOGW(TAG, "Authentication takes too long (%u), disconnect", sec(elapsed));
				disconnect();
				set_state(state_t::disconnecting);
			}
			break;
		case state_t::wait_reboot:
			if (auto elapsed = now - state_started; elapsed > REBOOT_DELAY) {
				reset();
				mark_failed();
				App.safe_reboot();
			}
			break;
		case state_t::running:
			if (!sesame.is_session_active()) {
				ESP_LOGW(TAG, "Session aborted, disconnecting");
				set_state(state_t::disconnecting);
				disconnect();
			}
			break;
		case state_t::none:
			break;
	}
}

bool
SesameLock::write_to_tx(const uint8_t* data, size_t size) {
	if (tx_handle == 0) {
		ESP_LOGE(TAG, "tx_handle not initialized");
		mark_failed();
		return false;
	}
	ESP_LOGV(TAG, "write to %u %ub", tx_handle, size);
	auto status = esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), tx_handle, size,
	                                       const_cast<uint8_t*>(data), ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
	if (status != ESP_OK) {
		ESP_LOGW(TAG, "Failed to write tx, status=%u", status);
		return false;
	}
	return true;
}

void
SesameLock::disconnect() {
	parent()->disconnect();
	reset();
}

void
SesameLock::connect() {
	parent()->connect();
	connect_tried++;
}

void
SesameLock::setup() {
	reset();
	set_state(state_t::discovering);
	connect_tried = 1;
}

void
SesameLock::reset() {
	rx_handle = 0;
	tx_handle = 0;
}

}  // namespace sesame_lock
}  // namespace esphome
