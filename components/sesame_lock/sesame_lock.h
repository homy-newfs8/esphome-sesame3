#pragma once

#include <SesameClient.h>
#include <esphome/components/lock/lock.h>
#include <esphome/components/sensor/sensor.h>
#include <esphome/components/text_sensor/text_sensor.h>
#include <esphome/core/component.h>
#include <atomic>
#include <optional>

namespace esphome {
namespace sesame_lock {

using libsesame3bt::Sesame;
using libsesame3bt::SesameClient;
using model_t = Sesame::model_t;

class SesameLock : public lock::Lock, public Component {
 public:
	SesameLock() { set_setup_priority(setup_priority::AFTER_WIFI); }
	void init(model_t model, const char* pubkey, const char* secret, const char* btaddr, const char* tag);
	void setup() override {}
	void loop() override;
	using lock::Lock::lock;
	using lock::Lock::open;
	using lock::Lock::unlock;
	void lock(const char* tag);
	void unlock(const char* tag);
	void open(const char* tag);
	void set_battery_pct_sensor(sensor::Sensor* sensor) { pct_sensor = sensor; }
	void set_battery_voltage_sensor(sensor::Sensor* sensor) { voltage_sensor = sensor; }
	void set_history_tag_sensor(text_sensor::TextSensor* sensor) { history_tag_sensor = sensor; }
	void set_history_type_sensor(sensor::Sensor* sensor) { history_type_sensor = sensor; }

 private:
	enum class state_t : int8_t { not_connected, connecting, authenticating, running, wait_reboot };
	SesameClient sesame;
	std::optional<SesameClient::Status> sesame_status;
	uint32_t last_connect_attempted;
	uint32_t state_started;
	TaskHandle_t ble_connect_task_id;
	std::optional<bool> ble_connect_result;
	std::string log_tag_string;
	Sesame::history_type_t recv_history_type;
	std::string recv_history_tag;
	const char* TAG;
	const char* default_history_tag;
	sensor::Sensor* pct_sensor = nullptr;
	sensor::Sensor* voltage_sensor = nullptr;
	text_sensor::TextSensor* history_tag_sensor = nullptr;
	sensor::Sensor* history_type_sensor = nullptr;
	SesameClient::state_t sesame_state;
	lock::LockState lock_state;
	state_t state;
	int8_t connect_tried;

	static bool initialized;
	static inline SemaphoreHandle_t ble_connect_mux;
	static inline StaticSemaphore_t ble_connect_mux_;
	static inline std::atomic<uint16_t> instances;

	void control(const lock::LockCall& call) override;
	void open_latch() override;
	void set_state(state_t);
	void reflect_sesame_status();
	void update_lock_state(lock::LockState);
	void ble_connect_task();
	bool operable_warn() const;
	void publish_lock_history_state();
	bool handle_history() const { return history_tag_sensor || history_type_sensor; }

	static bool static_init();
};

}  // namespace sesame_lock
}  //namespace esphome
