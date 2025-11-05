#include "lock_feature.h"
#include <esphome/core/hal.h>
#include <esphome/core/log.h>
#include <cmath>
#include "sesame_component.h"

using esphome::lock::LockState;
using model_t = libsesame3bt::Sesame::model_t;
using libsesame3bt::Sesame;
using libsesame3bt::SesameClient;

namespace {

constexpr uint32_t OPERATION_TIMEOUT = 3'000;
constexpr uint32_t JAMM_DETECTION_TIMEOUT = 3'000;
constexpr uint32_t HISTORY_TIMEOUT = 4'000;

}  // namespace

namespace esphome::sesame_lock {

SesameLock::SesameLock(SesameComponent* parent, model_t model, const char* tag)
    : parent_(parent), TAG(parent->TAG), default_history_tag(tag) {
	if (model == model_t::sesame_bot) {
		traits.set_supports_open(true);
	}
}

void
SesameLock::init() {
	ESP_LOGD(TAG, "lock init");
	if (using_history()) {
		recv_history_tag.reserve(SesameClient::MAX_CMD_TAG_SIZE + 1);
		parent_->sesame.set_history_callback([this](auto& client, const auto& history) {
			ESP_LOGD(TAG, "hist: r=%u, id=%ld, type=%u, str=(%u)%.*s", static_cast<uint8_t>(history.result), history.record_id,
			         static_cast<uint8_t>(history.type), history.tag_len, history.tag_len, history.tag);
			if (is_bot1()) {
				handle_bot_history(history);
				return;
			}
			if (history.result == Sesame::result_code_t::success) {
				if (history.type != Sesame::history_type_t::drive_locked && history.type != Sesame::history_type_t::drive_unlocked &&
				    history.type != Sesame::history_type_t::drive_clicked) {
					recv_history_tag_type = history.history_tag_type;
					recv_history_type = history.type;
					recv_history_tag.assign(history.tag, history.tag_len);
				}
				if (last_history_requested > 0 && history_type_matched(lock_state, recv_history_type)) {
					last_history_requested = 0;
					parent_->defer([this]() { publish_lock_history_state(); });
					return;
				}
			}
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
			} else if (unknown_state_timeout && now - unknown_state_started > unknown_state_timeout) {
				update_lock_state(lock::LOCK_STATE_NONE);
				if (history_tag_sensor) {
					history_tag_sensor->publish_state("");
				}
				if (history_type_sensor) {
					history_type_sensor->publish_state(NAN);
				}
				unknown_state_started = 0;
			}
		}
	}
}

void
SesameLock::handle_bot_history(const SesameClient::History& history) {
	if (history.result == Sesame::result_code_t::success) {
		if (history.type == Sesame::history_type_t::drive_locked || history.type == Sesame::history_type_t::drive_unlocked ||
		    history.type == Sesame::history_type_t::drive_clicked) {
		} else {
			recv_history_tag_type = history.history_tag_type;
			recv_history_type = history.type;
			recv_history_tag.assign(history.tag, history.tag_len);
		}
		if (last_history_requested > 0) {
			last_history_requested = 0;
			parent_->defer([this]() { publish_lock_history_state(); });
			return;
		}
	} else if (history.result == Sesame::result_code_t::not_found) {
		if (last_history_requested > 0) {
			last_history_requested = 0;
			parent_->defer([this]() { publish_lock_history_state(); });
			return;
		}
	} else {
		if (last_history_requested > 0) {
			parent_->set_timeout(300, [this]() {
				parent_->sesame.request_history();
				ESP_LOGD(TAG, "re request history");
			});
		}
	}
}

bool
SesameLock::is_bot1() const {
	return parent_->sesame.get_model() == libsesame3bt::Sesame::model_t::sesame_bot;
}

void
SesameLock::test_timeout() {
	auto now = esphome::millis();
	if (HISTORY_TIMEOUT) {
		if (last_history_requested && now - last_history_requested > HISTORY_TIMEOUT) {
			ESP_LOGW(TAG, "History receive timeout");
			last_history_requested = 0;
			clear_history();
			publish_lock_history_state();
		}
	}
	if (JAMM_DETECTION_TIMEOUT) {
		if (jam_detection_started && now - jam_detection_started > JAMM_DETECTION_TIMEOUT) {
			ESP_LOGW(TAG, "Locking state not determined too long, treat as jammed");
			jam_detection_started = 0;
			update_lock_state(LockState::LOCK_STATE_JAMMED);
		}
	}
}

void
SesameLock::publish_lock_state(bool force_publish) {
	auto st = lock_state;
	if (st == LockState::LOCK_STATE_NONE) {
		st = unknown_state_alternative;
	}
	if (state == st && force_publish) {
		ESP_LOGD(TAG, "'%s': (Force) Sending state %s", this->name_.c_str(), lock_state_to_string(state));
		state_callback_.call();
	}
	publish_state(st);
	motor_moved = false;
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

static int8_t
nibble(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	return -1;
}
static bool
hex2bin(std::string_view str, std::byte* out, size_t limit) {
	if (str.length() != limit * 2) {
		return false;
	}
	size_t olen = str.length() / 2;
	for (int i = 0; i < olen; i++) {
		int8_t n1 = nibble(str[i * 2]);
		int8_t n2 = nibble(str[i * 2 + 1]);
		if (n1 < 0 || n2 < 0) {
			return false;
		}
		out[i] = std::byte{static_cast<uint8_t>((n1 << 4) + n2)};
	}
	return true;
}

void
SesameLock::lock(float history_tag_type, std::string_view tag) {
	if (!operable_warn()) {
		return;
	}
	if (std::isnan(history_tag_type)) {
		parent_->sesame.lock(tag);
		return;
	}
	std::array<std::byte, libsesame3bt::HISTORY_TAG_UUID_SIZE> uuid;
	if (hex2bin(tag, uuid.data(), uuid.size()) == false) {
		ESP_LOGW(TAG, "Invalid history tag format, must be hex string");
		return;
	}
	parent_->sesame.lock(static_cast<libsesame3bt::history_tag_type_t>(history_tag_type), uuid);
}

void
SesameLock::unlock(float history_tag_type, std::string_view tag) {
	if (!operable_warn()) {
		return;
	}
	if (std::isnan(history_tag_type)) {
		parent_->sesame.lock(tag);
		return;
	}
	std::array<std::byte, libsesame3bt::HISTORY_TAG_UUID_SIZE> uuid;
	if (hex2bin(tag, uuid.data(), uuid.size()) == false) {
		ESP_LOGW(TAG, "Invalid history tag format, must be hex string");
		return;
	}
	parent_->sesame.unlock(static_cast<libsesame3bt::history_tag_type_t>(history_tag_type), uuid);
}

void
SesameLock::reflect_status_changed() {
	const auto& sesame_status = parent_->sesame_status;
	if (!sesame_status) {
		// update_lock_state(LockState::LOCK_STATE_NONE);
		return;
	}
	if (is_bot1()) {
		if (sesame_status->motor_status() != Sesame::motor_status_t::idle &&
		    sesame_status->motor_status() != Sesame::motor_status_t::holding) {
			motor_moved = true;
			if (using_history()) {
				ESP_LOGD(TAG, "History requested for bot");
				parent_->sesame.request_history();
			}
			return;
		}
	} else {
		if (using_history()) {
			if (!parent_->sesame.request_history()) {
				ESP_LOGW(TAG, "Failed to request history");
			}
		}
	}
	if (sesame_status->in_lock() == sesame_status->in_unlock()) {
		if (!jam_detection_started && lock_state != LockState::LOCK_STATE_JAMMED) {
			jam_detection_started = esphome::millis();
		}
		return;
	}
	jam_detection_started = 0;
	lock::LockState new_lock_state;
	if (sesame_status->is_critical()) {
		new_lock_state = lock::LOCK_STATE_JAMMED;
	} else if (sesame_status->in_unlock()) {
		new_lock_state = lock::LOCK_STATE_UNLOCKED;
	} else {
		new_lock_state = lock::LOCK_STATE_LOCKED;
	}
	update_lock_state(new_lock_state);
}

void
SesameLock::update_lock_state(lock::LockState new_state) {
	if (lock_state == new_state && !motor_moved) {
		return;
	}
	lock_state = new_state;
	if (lock_state == LockState::LOCK_STATE_NONE || lock_state == LockState::LOCK_STATE_JAMMED) {
		publish_lock_state(is_bot1());
	} else {
		if (!using_history() || fast_notify) {
			publish_lock_state(is_bot1());
		}
		if (using_history()) {
			if (parent_->sesame.request_history()) {
				ESP_LOGD(TAG, "History requested");
				last_history_requested = millis();
			} else {
				ESP_LOGW(TAG, "Failed to request history");
				clear_history();
				publish_lock_history_state();
			}
		}
	}
}

void
SesameLock::publish_lock_history_state() {
	if (history_tag_sensor) {
		history_tag_sensor->publish_state(recv_history_tag);
	}
	if (history_tag_type_sensor) {
		history_tag_type_sensor->publish_state(recv_history_tag_type.has_value() ? static_cast<uint8_t>(*recv_history_tag_type) : NAN);
	}
	if (history_type_sensor) {
		history_type_sensor->publish_state(static_cast<uint8_t>(recv_history_type));
	}
	if (!fast_notify) {
		publish_lock_state(is_bot1());
	}
}

static bool
is_lock_type(Sesame::history_type_t type) {
	using h = Sesame::history_type_t;
	return type == h::autolock || type == h::manual_locked || type == h::ble_lock || type == h::wm2_lock || type == h::web_lock ||
	       type == h::drive_locked;
}

static bool
is_unlock_type(Sesame::history_type_t type) {
	using h = Sesame::history_type_t;
	return type == h::manual_unlocked || type == h::ble_unlock || type == h::wm2_unlock || type == h::web_unlock ||
	       type == h::drive_unlocked;
}

bool
SesameLock::history_type_matched(lock::LockState state, Sesame::history_type_t type) {
	if (state == lock::LOCK_STATE_LOCKED) {
		return is_lock_type(type);
	} else if (state == lock::LOCK_STATE_UNLOCKED) {
		return is_unlock_type(type);
	}
	return false;
}

void
SesameLock::clear_history() {
	recv_history_tag_type = std::nullopt;
	recv_history_type = Sesame::history_type_t::none;
	recv_history_tag.clear();
}

void
SesameLock::loop() {
	test_unknown_state();
	if (parent_->my_state == state_t::running) {
		test_timeout();
	}
}

void
SesameLock::publish_initial_state() {
	publish_lock_state(true);
}

void
SesameLock::open_latch() {
	if (!operable_warn()) {
		return;
	}
	if (is_bot1()) {
		parent_->sesame.click(default_history_tag);
	} else {
		unlock();
	}
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

}  // namespace esphome::sesame_lock
