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

using esphome::esp32_ble::ESPBTUUID;
using libsesame3bt::Sesame;
using libsesame3bt::core::SesameClientCore;
namespace espbt = esphome::esp32_ble_tracker;

namespace esphome {
namespace sesame_lock {

SesameLock::SesameLock() : sesame(*this) {}

void
SesameLock::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t* param) {
	ESP_LOGV(TAG, "GATTC event = %u", static_cast<uint8_t>(event));
	switch (event) {
		case ESP_GATTC_OPEN_EVT: {
			if (param->open.status == ESP_GATT_OK) {
				ESP_LOGI(TAG, "Connected");
			}
			break;
		}

		case ESP_GATTC_DISCONNECT_EVT: {
			ESP_LOGI(TAG, "Disconnected");
			reset();
			sesame.on_disconnected();
			break;
		}

		case ESP_GATTC_SEARCH_CMPL_EVT: {
			ESP_LOGD(TAG, "Initializing");
			auto* srv = this->parent()->get_service(ESPBTUUID::from_raw(Sesame::SESAME3_SRV_UUID));
			if (srv == nullptr) {
				ESP_LOGW(TAG, "No SESAME Service registered, confirm device MAC ADDRESS.");
				mark_failed();
				return;
			}
			auto* tx = srv->get_characteristic(ESPBTUUID::from_raw(Sesame::TxUUID));
			if (tx == nullptr) {
				ESP_LOGW(TAG, "Failed to get Tx char");
				mark_failed();
				return;
			}
			tx_handle = tx->handle;

			auto* rx = srv->get_characteristic(ESPBTUUID::from_raw(Sesame::RxUUID));
			if (rx == nullptr) {
				ESP_LOGW(TAG, "Failed to get Rx char");
				mark_failed();
				return;
			}
			rx_handle = rx->handle;
			auto* desc = rx->get_descriptor(ESPBTUUID::from_uint16(0x2902));
			if (desc == nullptr) {
				ESP_LOGW(TAG, "Failed to get Rx descr");
				mark_failed();
				return;
			}
			auto reg_status =
			    esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(), this->parent()->get_remote_bda(), rx->handle);
			if (reg_status) {
				ESP_LOGW(TAG, "esp_ble_gattc_register_for_notify failed, status=%d", reg_status);
				mark_failed();
				return;
			}

			uint16_t req = 1;
			auto status =
			    esp_ble_gattc_write_char_descr(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), desc->handle, sizeof(req),
			                                   reinterpret_cast<uint8_t*>(&req), ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
			if (status != ESP_OK) {
				ESP_LOGW(TAG, "Error sending subscribe request, status=%d", status);
				mark_failed();
				return;
			}
			this->node_state = espbt::ClientState::ESTABLISHED;
			break;
		}

		case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
			if (param->reg_for_notify.status != ESP_GATT_OK) {
				ESP_LOGW(TAG, "Failed to register to notification, status=%u", param->reg_for_notify.status);
				mark_failed();
				return;
			}
			sesame.on_connected();
			break;
		}

		case ESP_GATTC_NOTIFY_EVT: {
			if (param->notify.handle == rx_handle) {
				ESP_LOGV(TAG, "received %ub", param->notify.value_len);
				sesame.on_received(reinterpret_cast<std::byte*>(param->notify.value), param->notify.value_len);
			}
			break;
		}

		default:
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
	sesame.set_state_callback([this](auto& client, auto state) { sesame_state = state; });
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
	set_state(state_t::not_connected);
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
	// switch (state) {
	// 	case state_t::not_connected:
	// 		if (connect_tried >= CONNECT_RETRY_LIMIT) {
	// 			ESP_LOGE(TAG, "Cannot connect to SESAME %d times, reboot after %u secs", connect_tried, REBOOT_DELAY_SEC);
	// 			set_state(state_t::wait_reboot);
	// 			break;
	// 		}
	// 		if (!last_connect_attempted || esphome::millis() - last_connect_attempted >= CONNECT_RETRY_INTERVAL) {
	// 			last_connect_attempted = esphome::millis();
	// 			++connect_tried;
	// 			ble_connect_result.reset();
	// 			set_state(state_t::connecting);
	// 		}
	// 		break;
	// 	case state_t::connecting:
	// 		if (esphome::millis() - state_started > CONNECT_TIMEOUT_SEC * 1000 * instances + CONNECT_STATE_TIMEOUT_MARGIN) {
	// 			ESP_LOGE(TAG, "Connect attempt not finished within expected time, reboot after %u secs", REBOOT_DELAY_SEC);
	// 			set_state(state_t::wait_reboot);
	// 			break;
	// 		}
	// 		if (ble_connect_result.has_value()) {
	// 			if (*ble_connect_result) {
	// 				ESP_LOGI(TAG, "Conncted to SESAME");
	// 				set_state(state_t::authenticating);
	// 			} else {
	// 				ESP_LOGW(TAG, "Failed to connect to SESAME");
	// 				set_state(state_t::not_connected);
	// 			}
	// 		}
	// 		break;
	// 	case state_t::authenticating:
	// 		if (sesame_state == SesameClient::state_t::idle || esphome::millis() - state_started > AUTHENTICATE_TIMEOUT) {
	// 			set_state(state_t::not_connected);
	// 			break;
	// 		}
	// 		connect_tried = 0;
	// 		if (sesame_state == SesameClient::state_t::active) {
	// 			last_connect_attempted = 0;
	// 			set_state(state_t::running);
	// 			ESP_LOGI(TAG, "Authenticated by SESAME");
	// 		}
	// 		break;
	// 	case state_t::running:
	// 		if (sesame_state != SesameClient::state_t::active) {
	// 			set_state(state_t::not_connected);
	// 			break;
	// 		}
	// 		break;
	// 	case state_t::wait_reboot:
	// 		if (esphome::millis() - state_started > REBOOT_DELAY_SEC * 1000) {
	// 			mark_failed();
	// 			App.safe_reboot();
	// 		}
	// 		break;
	// }
}

bool
SesameLock::write_to_tx(const uint8_t* data, size_t size) {
	if (tx_handle == 0) {
		ESP_LOGW(TAG, "tx_handle not initialized");
		mark_failed();
		return false;
	}
	ESP_LOGD(TAG, "write to %u %ub", tx_handle, size);
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
}

void
SesameLock::reset() {
	rx_handle = 0;
	tx_handle = 0;
}

}  // namespace sesame_lock
}  // namespace esphome
