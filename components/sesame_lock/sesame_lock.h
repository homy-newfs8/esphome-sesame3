#pragma once

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

class SesameLock : public lock::Lock,
                   public Component,
                   public ble_client::BLEClientNode,
                   private libsesame3bt::core::SesameClientBackend {
 public:
	SesameLock();
	void init(model_t model, const char* pubkey, const char* secret, const char* tag);
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
	libsesame3bt::core::SesameClientCore sesame;
	std::optional<libsesame3bt::core::Status> sesame_status;
	uint32_t last_connect_attempted;
	uint32_t state_started;
	std::optional<bool> ble_connect_result;
	std::string log_tag_string;
	libsesame3bt::Sesame::history_type_t recv_history_type;
	std::string recv_history_tag;
	const char* TAG;
	const char* default_history_tag;
	sensor::Sensor* pct_sensor = nullptr;
	sensor::Sensor* voltage_sensor = nullptr;
	text_sensor::TextSensor* history_tag_sensor = nullptr;
	sensor::Sensor* history_type_sensor = nullptr;
	libsesame3bt::core::SesameClientCore::state_t sesame_state;
	lock::LockState lock_state;
	state_t state;
	int8_t connect_tried;
	uint16_t rx_handle;
	uint16_t tx_handle;

	void control(const lock::LockCall& call) override;
	void open_latch() override;
	void set_state(state_t);
	void reflect_sesame_status();
	void update_lock_state(lock::LockState);
	bool operable_warn() const;
	void publish_lock_history_state();
	bool handle_history() const { return history_tag_sensor || history_type_sensor; }
	void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t* param) override;
	void reset();

	virtual bool write_to_tx(const uint8_t* data, size_t size) override;
	virtual void disconnect() override;
};

}  // namespace sesame_lock
}  //namespace esphome
