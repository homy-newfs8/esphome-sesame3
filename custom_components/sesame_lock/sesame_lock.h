#pragma once

#include <SesameClient.h>
#include <esphome/components/lock/lock.h>
#include <esphome/core/component.h>
#include <atomic>
#include <optional>

using libsesame3bt::SesameClient;

namespace esphome {
namespace sesame_lock {

class SesameLock : public lock::Lock, public Component {
 public:
	// void write_state(bool state) override;
	// void dump_config() override;
	void init(const char* pubkey, const char* secret, const char* btaddr);
	void setup() override {}
	void loop() override;

 private:
	enum class state_t : int8_t { not_connected, connecting, authenticating, running, wait_reboot };
	SesameClient sesame;
	SesameClient::Status sesame_status;
	uint32_t last_connect_attempted;
	uint32_t state_started;
	uint32_t op_started;
	uint32_t jam_detected;
	TaskHandle_t ble_connect_task_id;
	std::optional<bool> ble_connect_result;
	std::string tag_string;
	const char* TAG;
	SesameClient::state_t sesame_state;
	lock::LockState lock_state;
	state_t state;
	int8_t connect_tried;

	static bool initialized;
	static inline SemaphoreHandle_t ble_connect_mux;
	static inline StaticSemaphore_t ble_connect_mux_;
	static inline std::atomic<uint16_t> instances;

	void control(const lock::LockCall& call) override;
	void set_state(state_t);
	void reflect_lock_state();
	void update_lock_state(lock::LockState);
	void ble_connect_task();

	static bool static_init();
};

}  // namespace sesame_lock
}  //namespace esphome
