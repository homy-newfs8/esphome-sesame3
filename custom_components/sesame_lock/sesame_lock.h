#pragma once

#include <SesameClient.h>
#include <esphome/components/lock/lock.h>
#include <esphome/core/component.h>

using libsesame3bt::SesameClient;

namespace esphome {
namespace sesame_lock {

class SesameLock : public lock::Lock, public Component {
 public:
	// void setup() override;
	// void write_state(bool state) override;
	// void dump_config() override;
	void init(const char* pubkey, const char* secret, const char* btaddr);
	void setup() override {}
	void loop() override;

 private:
	void control(const lock::LockCall& call) override;

 private:
	enum class state_t { not_connected, connecting, running, wait_reboot };
	SesameClient sesame;
	SesameClient::state_t sesame_state;
	SesameClient::Status sesame_status;
	state_t state;
	uint32_t last_connect_attempted;
	uint32_t state_started;
	uint32_t op_started;
	uint32_t jam_detected;
	lock::LockState lock_state;

	void set_state(state_t);
	void reflect_lock_state();
	void update_lock_state(lock::LockState);
};

}  // namespace sesame_lock
}  //namespace esphome
