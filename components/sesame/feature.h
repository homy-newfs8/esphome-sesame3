#pragma once

namespace esphome::sesame_lock {

class Feature {
 public:
	virtual void init() = 0;
	virtual void loop() = 0;
	virtual void publish_initial_state() = 0;
	virtual void reflect_status_changed() = 0;
};

}  // namespace esphome::sesame_lock
