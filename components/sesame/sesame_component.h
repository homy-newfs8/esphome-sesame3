#pragma once

#include <SesameClient.h>
#include <esphome/components/binary_sensor/binary_sensor.h>
#include <esphome/components/sensor/sensor.h>
#include <esphome/core/component.h>
#include <mutex>
#include <string_view>
#include <vector>
#include "feature.h"

namespace esphome {

namespace sesame_server {

class SesameServerComponent;

}  // namespace sesame_server

namespace sesame_lock {

class BinarySensorWithInvalidate : public binary_sensor::BinarySensor {
 public:
	void set_state_internal(esphome::optional<bool> state) {
		this->state_ = state;
		if (state.has_value()) {
			this->state = *state;
		}
	}
};

enum class state_t : int8_t {
	not_connected,
	wait_connect,
	connecting,
	authenticating,
	running,
	wait_reboot,
	wait_server_disconnect
};

class SesameLock;
class BotFeature;
class SesameComponent : public PollingComponent {
	friend class SesameLock;
	friend class BotFeature;

 public:
	SesameComponent(const char* id);
	void init(libsesame3bt::Sesame::model_t model,
	          std::string_view pubkey,
	          std::string_view secret,
	          std::string_view btaddr,
	          std::string_view uuid);
	void setup() override;
	void loop() override;
	void set_battery_pct_sensor(sensor::Sensor* sensor) { pct_sensor = sensor; }
	void set_battery_voltage_sensor(sensor::Sensor* sensor) { voltage_sensor = sensor; }
	void set_connection_sensor(binary_sensor::BinarySensor* sensor) { connection_sensor = sensor; }
	void set_battery_critical_sensor(BinarySensorWithInvalidate* sensor) { battery_critical_sensor = sensor; }
	void set_connect_retry_limit(uint16_t retry_limit) { connect_limit = retry_limit; }
	void set_connection_timeout(uint32_t timeout) { connection_timeout = timeout; }
	void set_feature(Feature* feature) { this->feature = feature; }
	void set_always_connect(bool always) { this->always_connect = always; }
	virtual float get_setup_priority() const override { return setup_priority::AFTER_WIFI; };
	void set_sesame_server(sesame_server::SesameServerComponent* server) { this->server = server; }
	virtual void update() override;
	void make_unknown();

 private:
	libsesame3bt::SesameClient sesame;
	esphome::optional<libsesame3bt::SesameClient::Status> sesame_status;
	NimBLEAddress ble_address;
	uint32_t last_connect_attempted = 0;
	uint32_t state_started = 0;
	std::string log_tag_string;
	const char* TAG = "";
	sensor::Sensor* pct_sensor = nullptr;
	sensor::Sensor* voltage_sensor = nullptr;
	BinarySensorWithInvalidate* battery_critical_sensor = nullptr;
	Feature* feature = nullptr;
	binary_sensor::BinarySensor* connection_sensor = nullptr;
	sesame_server::SesameServerComponent* server = nullptr;
	state_t my_state = state_t::not_connected;
	uint16_t connect_limit = 0;
	uint16_t connect_tried = 0;
	uint32_t connection_timeout = 10'000;
	bool always_connect = true;
	union {
		uint8_t value;
		struct {
			bool update_status : 1;
		};
	} operation_requested{};
	static_assert(sizeof(operation_requested.value) == sizeof(operation_requested));

	static inline int instance_count = 0;
	static inline std::mutex ble_connecting_mux{};
	static inline std::vector<SesameComponent*> connect_queue{};
	static inline bool global_initialized{};

	void set_state(state_t);
	void reflect_sesame_status();
	void publish_connection_state(bool connected);
	void disconnect();
	int get_last_error() const { return sesame.get_ble_client() ? sesame.get_ble_client()->getLastError() : -1; }

	static void global_init();
	static bool enqueue_connect(SesameComponent*);
	static bool can_connect(SesameComponent*);
	static void connect_done(SesameComponent*);
};

}  // namespace sesame_lock
}  // namespace esphome
