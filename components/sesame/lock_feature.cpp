#include "lock_feature.h"
#include <esphome/core/hal.h>
#include <esphome/core/log.h>
#include "sesame_component.h"

using esphome::lock::LockState;
using model_t = libsesame3bt::Sesame::model_t;
using libsesame3bt::Sesame;
using libsesame3bt::SesameClient;

namespace {

constexpr uint32_t OPERATION_TIMEOUT = 3'000;
constexpr uint32_t JAMM_DETECTION_TIMEOUT = 3'000;
constexpr uint32_t HISTORY_TIMEOUT = 3'000;

}  // namespace

namespace esphome::sesame_lock {

static bool
is_bot(model_t model) {
	return model == model_t::sesame_bot || model == model_t::sesame_bot_2;
}

SesameLock::SesameLock(SesameComponent* parent, model_t model, const char* tag) : parent_(parent), TAG(parent->TAG) {
	if (is_bot(model)) {
		traits.set_supports_open(true);
	}
	default_history_tag = tag;
}

void
SesameLock::init() {
	ESP_LOGD(TAG, "lock init");
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
			} else if (unknown_state_timeout && now - unknown_state_started > unknown_state_timeout) {
				reflect_status_changed();
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
SesameLock::open(std::string_view) {
	if (!operable_warn()) {
		return;
	}
	parent_->sesame.click();
}

void
SesameLock::reflect_status_changed() {
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
SesameLock::update_lock_state(lock::LockState new_state) {
	if (lock_state == new_state) {
		return;
	}
	lock_state = new_state;
	if (lock_state != LockState::LOCK_STATE_NONE && handle_history()) {
		if (!parent_->sesame.request_history()) {
			ESP_LOGW(TAG, "Failed to request history");
			publish_lock_state();
		}
	} else {
		publish_lock_state();
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
	if (is_bot(parent_->sesame.get_model())) {
		parent_->sesame.click();
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
