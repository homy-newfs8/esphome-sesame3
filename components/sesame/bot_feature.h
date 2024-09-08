#pragma once

#include <Sesame.h>
#include <esphome/components/binary_sensor/binary_sensor.h>
#include <optional>
#include "feature.h"
#include "sesame_component.h"

namespace esphome::sesame_lock {

class BotFeature : public Feature {
 public:
	BotFeature(SesameComponent* parent, libsesame3bt::Sesame::model_t) : parent_(parent), TAG(parent->TAG) {};
	void init() override {}
	void loop() override {}
	void reflect_status_changed() override;
	void publish_initial_state() override {};
	void run(std::optional<int> script_no = std::nullopt);
	void set_running_sensor(binary_sensor::BinarySensor* sensor) { running_sensor = sensor; }

 private:
	SesameComponent* parent_;
	const char* TAG;
	binary_sensor::BinarySensor* running_sensor = nullptr;
};

}  // namespace esphome::sesame_lock
