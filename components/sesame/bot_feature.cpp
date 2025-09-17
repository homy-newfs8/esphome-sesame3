#include "bot_feature.h"
#include <esphome/core/log.h>

using libsesame3bt::Sesame;

namespace esphome::sesame_lock {

void
BotFeature::reflect_status_changed() {
	if (!running_sensor) {
		return;
	}
	const auto& sesame_status = parent_->sesame_status;
	if (!sesame_status) {
		running_sensor->invalidate_state();
		return;
	}
	auto model = parent_->sesame.get_model();
	if (model == Sesame::model_t::sesame_bot) {
		running_sensor->publish_state(sesame_status->motor_status() != Sesame::motor_status_t::idle);
	} else {
		running_sensor->publish_state(!sesame_status->stopped());
	}
}

void
BotFeature::run(std::optional<int> script_no) {
	if (!parent_->sesame.click(script_no)) {
		ESP_LOGW(TAG, "click() failed");
	}
}

}  // namespace esphome::sesame_lock
