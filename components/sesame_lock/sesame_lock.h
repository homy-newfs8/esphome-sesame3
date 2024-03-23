#pragma once

#include <esphome/components/binary_sensor/binary_sensor.h>
#include <esphome/components/ble_client/ble_client.h>
#include <esphome/components/lock/lock.h>
#include <esphome/components/sensor/sensor.h>
#include <esphome/components/text_sensor/text_sensor.h>
#include <esphome/core/component.h>
#include <libsesame3bt/ClientCore.h>
#include <atomic>
#include <optional>

namespace esphome {
namespace sesame_lock {

using model_t = libsesame3bt::Sesame::model_t;

// prevent override BLEClientNode::loop()
class Component_W : public Component {
 public:
	virtual void loop() final override { component_loop(); }

 protected:
	virtual void component_loop() = 0;
};

enum class state_t { asis, registering, connect_again, wait_reboot, authenticating, connected, running };

class SesameLock : public lock::Lock,
                   public Component_W,
                   public ble_client::BLEClientNode,
                   private libsesame3bt::core::SesameClientBackend {
 public:
	SesameLock();
	void init(model_t model, const char* pubkey, const char* secret, const char* tag);
	virtual void setup() override;
	virtual void component_loop() override;
	virtual float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }
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
	void set_connection_sensor(binary_sensor::BinarySensor* sensor) { connection_sensor = sensor; }

	void set_connect_retry_limit(uint16_t retry_limit) { connect_limit = retry_limit; }
	void set_discover_timeout(uint32_t timeout) { discover_timeout = timeout; }
	void set_unknown_state_alternative(lock::LockState alternative) { unknown_state_alternative = alternative; }

 private:
	libsesame3bt::core::SesameClientCore sesame;
	std::optional<libsesame3bt::core::Status> sesame_status;
	std::string log_tag_string;
	const char* TAG;
	libsesame3bt::Sesame::history_type_t recv_history_type = libsesame3bt::Sesame::history_type_t::none;
	std::string recv_history_tag;
	const char* default_history_tag;

	sensor::Sensor* pct_sensor = nullptr;
	sensor::Sensor* voltage_sensor = nullptr;
	text_sensor::TextSensor* history_tag_sensor = nullptr;
	sensor::Sensor* history_type_sensor = nullptr;
	binary_sensor::BinarySensor* connection_sensor = nullptr;
	lock::LockState lock_state = lock::LockState::LOCK_STATE_NONE;
	lock::LockState unknown_state_alternative = lock::LockState::LOCK_STATE_NONE;

	uint32_t state_started = 0;
	uint32_t last_history_requested = 0;
	uint32_t last_notified = 0;
	uint32_t jam_detect_started = 0;
	state_t ex_state = state_t::asis;
	uint32_t discover_timeout = 0;
	uint16_t connect_limit = 0;
	uint16_t connect_tried = 0;
	uint16_t rx_handle = 0;
	uint16_t tx_handle = 0;
	bool once_found = false;

	esphome::esp32_ble_tracker::ClientState last_node_state;

	void set_state(state_t new_state);
	void update_state();
	virtual void control(const lock::LockCall& call) override;
	virtual void open_latch() override;
	void reflect_sesame_status();
	void update_lock_state(lock::LockState, bool force_publish = false);
	bool operable_warn() const;
	void publish_lock_history_state();
	void publish_lock_state(bool force_publish = false);
	void publish_connection_state(bool connected);
	bool handle_history() const { return history_tag_sensor || history_type_sensor; }
	void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t* param) override;
	void reset();
	void connect();

	const char* state_str() const;

	virtual bool write_to_tx(const uint8_t* data, size_t size) override;
	virtual void disconnect() override;
};

}  // namespace sesame_lock
}  // namespace esphome
