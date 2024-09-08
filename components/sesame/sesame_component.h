#pragma once

#include <SesameClient.h>
#include <esphome/components/binary_sensor/binary_sensor.h>
#include <esphome/components/sensor/sensor.h>
#include <esphome/core/component.h>
#include <mutex>
#include <optional>
#include "feature.h"

namespace esphome {
namespace sesame_lock {

enum class state_t : int8_t { not_connected, connecting, authenticating, running, wait_reboot };

class SesameLock;
class BotFeature;
class SesameComponent : public PollingComponent {
	friend class SesameLock;
	friend class BotFeature;

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
	void set_feature(Feature* feature) { this->feature = feature; }
	void set_always_connect(bool always) { this->always_connect = always; }
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
	Feature* feature = nullptr;
	binary_sensor::BinarySensor* connection_sensor = nullptr;
	libsesame3bt::SesameClient::state_t sesame_state = libsesame3bt::SesameClient::state_t::idle;
	state_t my_state = state_t::not_connected;
	uint16_t connect_limit = 0;
	uint16_t connect_tried = 0;
	uint8_t connection_timeout_sec = 10;
	bool always_connect = true;
	union {
		uint8_t value;
		struct {
			bool update_status : 1;
		};
	} operation_requested{};
	static_assert(sizeof(operation_requested.value) == sizeof(operation_requested));

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

}  // namespace sesame_lock
}  // namespace esphome
