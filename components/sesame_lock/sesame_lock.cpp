#include "sesame_lock.h"
#include <esphome/core/application.h>

namespace {

constexpr uint32_t CONNECT_TIMEOUT = 10'000;
constexpr uint32_t DISCONNECT_TIMEOUT = 2'000;
constexpr uint32_t AUTH_TIMEOUT = 5'000;
constexpr uint32_t REBOOT_DELAY = 3'000;
constexpr uint32_t CONNECT_INTERVAL = 5'000;
constexpr uint32_t HISTORY_TIMEOUT = 2'000;

constexpr uint32_t
sec(uint32_t ms) {
	return ms / 1'000;
}

}  // namespace

using esphome::esp32_ble::ESPBTUUID;
using esphome::lock::LockState;
using libsesame3bt::Sesame;
using libsesame3bt::core::SesameClientCore;
namespace espbt = esphome::esp32_ble_tracker;

namespace esphome {
namespace sesame_lock {

SesameLock::SesameLock() : sesame(*this) {}

const char*
SesameLock::state_str() const {
	using espbt::ClientState;
	switch (ex_state) {
		case state_t::authenticating:
			return "authenticating";
		case state_t::connect_again:
			return "connect_again";
		case state_t::connected:
			return "connected";
		case state_t::registering:
			return "registering";
		case state_t::wait_reboot:
			return "wait_reboot";
		case state_t::running:
			return "running";
		case state_t::asis:
			switch (node_state) {
				case ClientState::CONNECTED:
					return "CONNECTED";
				case ClientState::CONNECTING:
					return "CONNECTING";
				case ClientState::DISCONNECTING:
					return "DISCONNECTING";
				case ClientState::DISCOVERED:
					return "DISCOVERED";
				case ClientState::ESTABLISHED:
					return "ESTABLISHED";
				case ClientState::IDLE:
					return "IDLE";
				case ClientState::INIT:
					return "INIT";
				case ClientState::READY_TO_CONNECT:
					return "READY_TO_CONNECT";
				case ClientState::SEARCHING:
					return "SEARCHING";
				default:
					return "UNKNOWN";
			}
			break;
		default:
			return "unknown";
	}
}

void
SesameLock::update_state() {
	if (ex_state == state_t::wait_reboot) {
		return;
	}
	if (node_state == last_node_state) {
		return;
	}
	last_node_state = node_state;
	ex_state = state_t::asis;
	ESP_LOGD(TAG, "state=%s", state_str());
	state_started = esphome::millis();
}

void
SesameLock::set_state(state_t new_state) {
	if (ex_state == state_t::wait_reboot) {
		return;
	}
	if (new_state == state_t::asis) {
		if (new_state != ex_state || last_node_state != node_state) {
			state_started = esphome::millis();
			ex_state = new_state;
			last_node_state = node_state;
			ESP_LOGD(TAG, "state=%s", state_str());
		}
	} else {
		if (new_state != ex_state) {
			state_started = esphome::millis();
			ex_state = new_state;
			ESP_LOGD(TAG, "state=%s", state_str());
		}
	}
}

void
SesameLock::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t* param) {
	switch (event) {
		case ESP_GATTC_SEARCH_CMPL_EVT: {
			set_state(state_t::connected);
			break;
		}

		case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
			if (param->reg_for_notify.status == ESP_GATT_OK) {
				// notification handler ready
				this->node_state = espbt::ClientState::ESTABLISHED;
				set_state(state_t::asis);
			} else {
				ESP_LOGW(TAG, "Failed to register notification, disconnect");
				disconnect();
			}
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
SesameLock::component_loop() {
	update_state();

	auto now = esphome::millis();
	auto elapsed = now - state_started;
	switch (ex_state) {
		case state_t::registering:
			break;
		case state_t::connect_again:
			if (connect_limit) {
				if (connect_tried >= connect_limit) {
					ESP_LOGE(TAG, "Connect failed %u times, reboot after %lu seconds", connect_tried, sec(REBOOT_DELAY));
					set_state(state_t::wait_reboot);
					break;
				}
			}
			if (elapsed > CONNECT_INTERVAL) {
				connect();
				set_state(state_t::asis);
			}
			break;
		case state_t::wait_reboot:
			if (elapsed > REBOOT_DELAY) {
				mark_failed();
				App.safe_reboot();
			}
			break;
		case state_t::authenticating:
			if (AUTH_TIMEOUT) {
				if (elapsed > AUTH_TIMEOUT) {
					ESP_LOGW(TAG, "Failed to authenticate to SESAME");
					disconnect();
					break;
				}
			}
			if (sesame.get_state() == libsesame3bt::core::state_t::active) {
				connect_tried = 0;
				set_state(state_t::running);
				status_clear_warning();
			}
			break;
		case state_t::connected: {
			auto* srv = this->parent()->get_service(ESPBTUUID::from_raw(Sesame::SESAME3_SRV_UUID));
			if (srv == nullptr) {
				ESP_LOGE(TAG, "Connected device does not have SESAME Service, confirm device MAC ADDRESS");
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
				disconnect();
				status_set_warning();
				break;
			}
			set_state(state_t::registering);
			break;
		}
		case state_t::running:
			if (sesame.get_state() != libsesame3bt::core::state_t::active) {
				ESP_LOGW(TAG, "Sesame client state changed, disconnect");
				disconnect();
			}
			if (HISTORY_TIMEOUT) {
				if (last_history_requested && now - last_history_requested > HISTORY_TIMEOUT) {
					ESP_LOGW(TAG, "History not received");
					publish_lock_state();
					last_history_requested = 0;
				}
			}
			break;
		case state_t::asis:
			switch (node_state) {
				using espbt::ClientState;

				case ClientState::INIT:
					break;
				case ClientState::DISCONNECTING:
					if (once_found && DISCONNECT_TIMEOUT) {
						if (elapsed > DISCONNECT_TIMEOUT) {
							ESP_LOGW(TAG, "Disconnecting last %lu seconds, connect again", sec(elapsed));
							set_state(state_t::connect_again);
						}
					}
					break;
				case ClientState::IDLE:
					if (once_found) {
						sesame.on_disconnected();
						reset();
						update_lock_state(unknown_state_alternative);
						set_state(state_t::connect_again);
					} else {
						if (discover_timeout) {
							if (elapsed > discover_timeout) {
								ESP_LOGW(TAG, "SESAME not found %lu seconds, rebooting after %lu seconds", sec(elapsed), sec(REBOOT_DELAY));
								set_state(state_t::wait_reboot);
							}
						}
					}
					break;
				case ClientState::SEARCHING:
					if (discover_timeout) {
						if (elapsed > discover_timeout) {
							ESP_LOGW(TAG, "SESAME not found %lu seconds, rebooting after %lu seconds", sec(elapsed), sec(REBOOT_DELAY));
							set_state(state_t::wait_reboot);
						}
					}
					break;
				case ClientState::DISCOVERED:
					// may be start connecting
					once_found = true;
					break;
				case ClientState::READY_TO_CONNECT:
					break;
				case ClientState::CONNECTING:
					if (once_found && CONNECT_TIMEOUT) {
						if (elapsed > CONNECT_TIMEOUT) {
							ESP_LOGW(TAG, "Cannot connect for %lu seconds, disconnect", sec(elapsed));
							disconnect();
						}
					}
					break;
				case ClientState::CONNECTED:
					// start search service
					break;
				case ClientState::ESTABLISHED:
					// start authentication
					sesame.on_connected();
					set_state(state_t::authenticating);
					break;
			}
			break;
	}
}

bool
SesameLock::write_to_tx(const uint8_t* data, size_t size) {
	if (tx_handle == 0) {
		ESP_LOGW(TAG, "tx_handle not initialized yet");
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
SesameLock::init(model_t model, const char* pubkey, const char* secret, const char* tag) {
	log_tag_string = get_name();
	TAG = log_tag_string.c_str();

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
		set_timeout(0, [this]() { reflect_sesame_status(); });
	});
	if (handle_history()) {
		recv_history_tag.reserve(Sesame::MAX_HISTORY_TAG_SIZE + 1);
		sesame.set_history_callback([this](auto& client, const auto& history) {
			recv_history_type = history.type;
			recv_history_tag.assign(history.tag, history.tag_len);
			ESP_LOGD(TAG, "hist: type=%u, str=(%u)%.*s", static_cast<uint8_t>(history.type), history.tag_len, history.tag_len,
			         history.tag);
			set_timeout(0, [this]() { publish_lock_history_state(); });
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
	LockState new_lock_state;
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
SesameLock::publish_lock_state(bool force_publish) {
	auto st = lock_state;
	if (st == LockState::LOCK_STATE_NONE) {
		st = unknown_state_alternative;
	}
	if (state == st && force_publish) {
		ESP_LOGD(TAG, "Force publish state = %u", static_cast<uint8_t>(lock_state));
		state_callback_.call();
	}
	publish_state(st);
}

void
SesameLock::update_lock_state(LockState new_state, bool force_publish) {
	if (!force_publish && lock_state == new_state) {
		return;
	}
	lock_state = new_state;
	if (!force_publish && lock_state != LockState::LOCK_STATE_NONE && handle_history()) {
		if (!sesame.request_history()) {
			ESP_LOGW(TAG, "Failed to request history");
			publish_lock_state();
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

bool
SesameLock::operable_warn() const {
	if (ex_state != state_t::running) {
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
SesameLock::disconnect() {
	parent()->disconnect();
	reset();
	update_lock_state(LockState::LOCK_STATE_NONE, true);
}

void
SesameLock::connect() {
	parent()->connect();
	connect_tried++;
}

void
SesameLock::setup() {
	update_lock_state(unknown_state_alternative, true);
	reset();
	connect_tried = 1;
	status_set_warning();
}

void
SesameLock::reset() {
	rx_handle = 0;
	tx_handle = 0;
}

}  // namespace sesame_lock
}  // namespace esphome
