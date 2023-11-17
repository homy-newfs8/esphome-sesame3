#pragma once

#include <SesameClient.h>
#include <esphome/components/lock/lock.h>
#include <esphome/core/component.h>
#include <atomic>
#include <optional>

using libsesame3bt::SesameClient;

namespace esphome {
namespace sesame_lock {

using model_t = libsesame3bt::Sesame::model_t;

class SesameLock : public lock::Lock, public Component {
 public:
	// void write_state(bool state) override;
	// void dump_config() override;
	void init(model_t model, const char* pubkey, const char* secret, const char* btaddr, const char* tag);
	void setup() override {}
	void loop() override;
	float get_setup_priority() const override {
		// After the Wi-Fi has been done setup
		return setup_priority::AFTER_WIFI;
	}
	using lock::Lock::lock;
	using lock::Lock::open;
	using lock::Lock::unlock;
	void lock(const char* tag);
	void unlock(const char* tag);
	void open(const char* tag);
	float get_battery_pct() const;
	float get_battery_voltage() const;

 private:
	enum class state_t : int8_t { not_connected, connecting, authenticating, running, wait_reboot };
	SesameClient sesame;
	std::optional<SesameClient::Status> sesame_status;
	uint32_t last_connect_attempted;
	uint32_t state_started;
	TaskHandle_t ble_connect_task_id;
	std::optional<bool> ble_connect_result;
	std::string tag_string;
	const char* TAG;
	const char* default_history_tag;
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
	void reflect_lock_state();
	void update_lock_state(lock::LockState);
	void ble_connect_task();
	bool operable_warn() const;

	static bool static_init();
};

}  // namespace sesame_lock
}  //namespace esphome
