#pragma once

#include <Sesame.h>
#include <SesameClient.h>
#include <esphome/components/lock/lock.h>
#include <esphome/components/sensor/sensor.h>
#include <esphome/components/text_sensor/text_sensor.h>
#include <esphome/core/component.h>
#include <cmath>
#include <optional>
#include <string_view>
#include "feature.h"

namespace esphome {
namespace sesame_lock {

struct history_set {
	text_sensor::TextSensor* history_tag_sensor = nullptr;
	sensor::Sensor* history_type_sensor = nullptr;
	sensor::Sensor* history_tag_type_sensor = nullptr;
	sensor::Sensor* history_scaled_voltage_sensor = nullptr;
	sensor::Sensor* history_battery_pct_sensor = nullptr;
	sensor::Sensor* history_scaled_voltage2_sensor = nullptr;
	sensor::Sensor* history_battery_pct2_sensor = nullptr;
	text_sensor::TextSensor* history_extra_sensor = nullptr;

	libsesame3bt::Sesame::history_type_t recv_history_type = libsesame3bt::Sesame::history_type_t::none;
	std::optional<libsesame3bt::history_tag_type_t> recv_history_tag_type = std::nullopt;
	float recv_scaled_voltage = NAN;
	float recv_scaled_voltage2 = NAN;
	std::string recv_extra;
	std::string recv_history_tag;

	bool using_history() const {
		return history_tag_sensor || history_type_sensor || history_tag_type_sensor || history_scaled_voltage_sensor ||
		       history_battery_pct_sensor || history_scaled_voltage2_sensor || history_battery_pct2_sensor || history_extra_sensor;
	}
	void reserve_tag_buffer() {
		if (using_history()) {
			recv_history_tag.reserve(libsesame3bt::SesameClient::MAX_CMD_TAG_SIZE + 1);
		}
	}
	void save_received_values(libsesame3bt::Sesame::history_type_t type,
	                          std::optional<libsesame3bt::history_tag_type_t> tag_type,
	                          std::string_view tag,
	                          float scaled_voltage,
	                          float scaled_voltage2,
	                          std::string_view extra) {
		recv_history_type = type;
		recv_history_tag_type = tag_type;
		recv_history_tag.assign(tag);
		recv_scaled_voltage = scaled_voltage;
		recv_scaled_voltage2 = scaled_voltage2;
		recv_extra = extra;
	}
	void set_history_sensors();
	void publish_history_sensors();
	void set_battery_pct_sensor(sensor::Sensor* sensor, float scaled_voltage);
	void clear_received_values();
};

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
	void lock(StringRef tag) { lock(std::string_view{tag.c_str(), tag.size()}); }
	void unlock(std::string_view tag);
	void unlock(StringRef tag) { unlock(std::string_view{tag.c_str(), tag.size()}); }
	void lock(float history_tag_type, std::string_view tag);
	void lock(float history_tag_type, StringRef tag) { lock(history_tag_type, std::string_view{tag.c_str(), tag.size()}); }
	void unlock(float history_tag_type, std::string_view tag);
	void unlock(float history_tag_type, StringRef tag) { unlock(history_tag_type, std::string_view{tag.c_str(), tag.size()}); }
	void open(std::string_view tag);
	void open(StringRef tag) { open(std::string_view{tag.c_str(), tag.size()}); }
	void set_history_tag_sensor(text_sensor::TextSensor* sensor) { set_history_tag_sensor(get_history_set(), sensor); }
	void set_history_type_sensor(sensor::Sensor* sensor) { set_history_type_sensor(get_history_set(), sensor); }
	void set_history_tag_type_sensor(sensor::Sensor* sensor) { set_history_tag_type_sensor(get_history_set(), sensor); }
	void set_history_scaled_voltage_sensor(sensor::Sensor* sensor) { set_history_scaled_voltage_sensor(get_history_set(), sensor); }
	void set_history_battery_pct_sensor(sensor::Sensor* sensor) { set_history_battery_pct_sensor(get_history_set(), sensor); }
	void set_history_scaled_voltage2_sensor(sensor::Sensor* sensor) { set_history_scaled_voltage2_sensor(get_history_set(), sensor); }
	void set_history_battery_pct2_sensor(sensor::Sensor* sensor) { set_history_battery_pct2_sensor(get_history_set(), sensor); }
	void set_history_extra_sensor(text_sensor::TextSensor* sensor) { set_history_extra_sensor(get_history_set(), sensor); }
	void set_all_history_tag_sensor(text_sensor::TextSensor* sensor) { set_history_tag_sensor(get_all_history_set(), sensor); }
	void set_all_history_type_sensor(sensor::Sensor* sensor) { set_history_type_sensor(get_all_history_set(), sensor); }
	void set_all_history_tag_type_sensor(sensor::Sensor* sensor) { set_history_tag_type_sensor(get_all_history_set(), sensor); }
	void set_all_history_scaled_voltage_sensor(sensor::Sensor* sensor) {
		set_history_scaled_voltage_sensor(get_all_history_set(), sensor);
	}
	void set_all_history_battery_pct_sensor(sensor::Sensor* sensor) { set_history_battery_pct_sensor(get_all_history_set(), sensor); }
	void set_all_history_scaled_voltage2_sensor(sensor::Sensor* sensor) {
		set_history_scaled_voltage2_sensor(get_all_history_set(), sensor);
	}
	void set_all_history_battery_pct2_sensor(sensor::Sensor* sensor) {
		set_history_battery_pct2_sensor(get_all_history_set(), sensor);
	}
	void set_all_history_extra_sensor(text_sensor::TextSensor* sensor) { set_history_extra_sensor(get_all_history_set(), sensor); }
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
	uint32_t history_timeout_started = 0;
	history_set hset[2];
	lock::LockState lock_state = lock::LockState::LOCK_STATE_NONE;
	lock::LockState unknown_state_alternative = lock::LockState::LOCK_STATE_NONE;
	uint32_t unknown_state_started = 0;
	uint32_t unknown_state_timeout = 20'000;
	bool motor_moved = false;
	bool fast_notify = false;

	virtual void control(const lock::LockCall& call) override;
	virtual void open_latch() override;
	bool operable_warn() const;
	bool using_history() const { return get_history_set().using_history() || get_all_history_set().using_history(); }
	void test_timeout();
	void test_unknown_state();
	void publish_lock_state(bool force_publish = false);
	void update_lock_state(lock::LockState);
	void publish_lock_history_state();
	void publish_all_history_state();
	bool history_type_matched(lock::LockState, libsesame3bt::Sesame::history_type_t);
	void clear_history();
	void handle_bot_history(const libsesame3bt::SesameClient::History& history);
	bool is_bot1() const;
	void set_battery_pct_sensor(sensor::Sensor* sensor, float scaled_voltage);
	void set_history_sensors();
	void publish_history_sensors();
	void set_history_tag_sensor(history_set& hset, text_sensor::TextSensor* sensor) { hset.history_tag_sensor = sensor; }
	void set_history_type_sensor(history_set& hset, sensor::Sensor* sensor) { hset.history_type_sensor = sensor; }
	void set_history_tag_type_sensor(history_set& hset, sensor::Sensor* sensor) { hset.history_tag_type_sensor = sensor; }
	void set_history_scaled_voltage_sensor(history_set& hset, sensor::Sensor* sensor) { hset.history_scaled_voltage_sensor = sensor; }
	void set_history_battery_pct_sensor(history_set& hset, sensor::Sensor* sensor) { hset.history_battery_pct_sensor = sensor; }
	void set_history_scaled_voltage2_sensor(history_set& hset, sensor::Sensor* sensor) {
		hset.history_scaled_voltage2_sensor = sensor;
	}
	void set_history_battery_pct2_sensor(history_set& hset, sensor::Sensor* sensor) { hset.history_battery_pct2_sensor = sensor; }
	void set_history_extra_sensor(history_set& hset, text_sensor::TextSensor* sensor) { hset.history_extra_sensor = sensor; }
	history_set& get_history_set() { return hset[0]; }
	history_set& get_all_history_set() { return hset[1]; }
	const history_set& get_history_set() const { return hset[0]; }
	const history_set& get_all_history_set() const { return hset[1]; }
};

}  // namespace sesame_lock
}  // namespace esphome
