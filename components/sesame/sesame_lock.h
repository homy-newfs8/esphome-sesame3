#pragma once

#include <SesameClient.h>
#include <esphome/components/binary_sensor/binary_sensor.h>
#include <esphome/components/lock/lock.h>
#include <esphome/components/sensor/sensor.h>
#include <esphome/components/text_sensor/text_sensor.h>
#include <esphome/core/component.h>
#include <mutex>
#include <optional>
#include <string_view>

namespace esphome {
namespace sesame_lock {

enum class state_t : int8_t { not_connected, connecting, authenticating, running, wait_reboot };

class SesameLock;
class SesameComponent : public PollingComponent {
	friend class SesameLock;

 public:
	SesameComponent(const char* id);
	void init(libsesame3bt::Sesame::model_t model, const char* pubkey, const char* secret, const char* btaddr);
	void setup() override;
	void loop() override;
	void set_battery_pct_sensor(sensor::Sensor* sensor) { pct_sensor = sensor; }
	void set_battery_voltage_sensor(sensor::Sensor* sensor) { voltage_sensor = sensor; }
	void set_connection_sensor(binary_sensor::BinarySensor* sensor) { connection_sensor = sensor; }
	void set_connect_retry_limit(uint16_t retry_limit) { connect_limit = retry_limit; }
	void set_connection_timeout_sec(uint8_t timeout) { connection_timeout_sec = timeout; }
	void set_lock(SesameLock* lock) { this->lock = lock; }
	virtual float get_setup_priority() const override { return setup_priority::AFTER_WIFI; };

 private:
	libsesame3bt::SesameClient sesame;
	std::optional<libsesame3bt::SesameClient::Status> sesame_status;
	uint32_t last_connect_attempted = 0;
	uint32_t state_started = 0;
	std::string log_tag_string;
	const char* TAG = "";
	sensor::Sensor* pct_sensor = nullptr;
	sensor::Sensor* voltage_sensor = nullptr;
	SesameLock* lock = nullptr;
	binary_sensor::BinarySensor* connection_sensor = nullptr;
	libsesame3bt::SesameClient::state_t sesame_state = libsesame3bt::SesameClient::state_t::idle;
	state_t my_state = state_t::not_connected;
	uint16_t connect_limit = 0;
	uint16_t connect_tried = 0;
	uint8_t connection_timeout_sec = 10;

	static bool initialized;

	void set_state(state_t);
	void reflect_sesame_status();
	void publish_connection_state(bool connected);
	void connect();
	void disconnect();
	virtual void update() override;

	static inline std::mutex ble_connecting_mux{};
	static inline SesameComponent* ble_connecting_client = nullptr;
	static inline TaskHandle_t ble_connect_task_id;

	static bool static_init();
	static void connect_task(void*);
	static bool enqueue_connect(SesameComponent*);
};

class SesameLock : public lock::Lock {
	friend class SesameComponent;

 public:
	SesameLock(SesameComponent* parent, libsesame3bt::Sesame::model_t model, const char* tag);
	void init();
	using lock::Lock::lock;
	using lock::Lock::open;
	using lock::Lock::unlock;
	void lock(std::string_view tag);
	void unlock(std::string_view tag);
	void open(std::string_view tag);
	void set_history_tag_sensor(text_sensor::TextSensor* sensor) { history_tag_sensor = sensor; }
	void set_history_type_sensor(sensor::Sensor* sensor) { history_type_sensor = sensor; }
	void set_unknown_state_alternative(lock::LockState alternative) { unknown_state_alternative = alternative; }
	void set_unknown_state_timeout_sec(uint8_t sec) { unknown_state_timeout_sec = sec; }

 private:
	SesameComponent* parent_;
	const char* TAG;
	uint32_t jam_detection_started = 0;
	uint32_t last_history_requested = 0;
	text_sensor::TextSensor* history_tag_sensor = nullptr;
	sensor::Sensor* history_type_sensor = nullptr;
	libsesame3bt::Sesame::history_type_t recv_history_type;
	std::string recv_history_tag;
	const char* default_history_tag = "";
	lock::LockState lock_state = lock::LockState::LOCK_STATE_NONE;
	lock::LockState unknown_state_alternative = lock::LockState::LOCK_STATE_NONE;
	uint32_t unknown_state_started = 0;
	uint8_t unknown_state_timeout_sec = 20;

	virtual void control(const lock::LockCall& call) override;
	virtual void open_latch() override;
	bool operable_warn() const;
	bool handle_history() const { return history_tag_sensor || history_type_sensor; }
	void reflect_sesame_status();
	void test_timeout();
	void test_unknown_state();
	void publish_lock_state(bool force_publish = false);
	void update_lock_state(lock::LockState, bool force_publish = false);
	void publish_lock_history_state();
};

}  // namespace sesame_lock
}  // namespace esphome
