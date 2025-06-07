#pragma once

#include <Sesame.h>
#include <SesameClient.h>
#include <esphome/components/lock/lock.h>
#include <esphome/components/sensor/sensor.h>
#include <esphome/components/text_sensor/text_sensor.h>
#include <esphome/core/component.h>
#include <optional>
#include <string_view>
#include "feature.h"

namespace esphome {
namespace sesame_lock {

class SesameComponent;
class SesameLock : public lock::Lock, public Feature {
	friend class SesameComponent;

 public:
	SesameLock(SesameComponent* parent, libsesame3bt::Sesame::model_t model, const char* tag);
	virtual void init() override;
	using lock::Lock::lock;
	using lock::Lock::open;
	using lock::Lock::unlock;
	void lock(std::string_view tag);
	void unlock(std::string_view tag);
	void open(std::string_view tag);
	void set_history_tag_sensor(text_sensor::TextSensor* sensor) { history_tag_sensor = sensor; }
	void set_history_type_sensor(sensor::Sensor* sensor) { history_type_sensor = sensor; }
	void set_trigger_type_sensor(sensor::Sensor* sensor) { trigger_type_sensor = sensor; }
	void set_unknown_state_alternative(lock::LockState alternative) { unknown_state_alternative = alternative; }
	void set_unknown_state_timeout(uint32_t timeout) { unknown_state_timeout = timeout; }
	void set_fast_notify(bool fast_notify) { this->fast_notify = fast_notify; }
	virtual void loop() override;
	virtual void publish_initial_state() override;
	virtual void reflect_status_changed() override;

 private:
	SesameComponent* parent_;
	const char* TAG;
	const char* default_history_tag = "";
	uint32_t jam_detection_started = 0;
	uint32_t last_history_requested = 0;
	text_sensor::TextSensor* history_tag_sensor = nullptr;
	sensor::Sensor* history_type_sensor = nullptr;
	sensor::Sensor* trigger_type_sensor = nullptr;
	libsesame3bt::Sesame::history_type_t recv_history_type = libsesame3bt::Sesame::history_type_t::none;
	std::optional<libsesame3bt::trigger_type_t> recv_trigger_type = std::nullopt;
	std::string recv_history_tag;
	lock::LockState lock_state = lock::LockState::LOCK_STATE_NONE;
	lock::LockState unknown_state_alternative = lock::LockState::LOCK_STATE_NONE;
	uint32_t unknown_state_started = 0;
	uint32_t unknown_state_timeout = 20'000;
	bool motor_moved = false;
	bool fast_notify = false;

	virtual void control(const lock::LockCall& call) override;
	virtual void open_latch() override;
	bool operable_warn() const;
	bool using_history() const { return history_tag_sensor || history_type_sensor; }
	void test_timeout();
	void test_unknown_state();
	void publish_lock_state(bool force_publish = false);
	void update_lock_state(lock::LockState);
	void publish_lock_history_state();
	bool history_type_matched(lock::LockState, libsesame3bt::Sesame::history_type_t);
	void clear_history();
	void handle_bot_history(const libsesame3bt::SesameClient::History& history);
	bool is_bot1() const;
};

}  // namespace sesame_lock
}  // namespace esphome
