#pragma once

#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

namespace esphome {
namespace sesame_ble {

class SesameBleListener : public esp32_ble_tracker::ESPBTDeviceListener {
 public:
	bool parse_device(const esp32_ble_tracker::ESPBTDevice& device) override;
};

}  // namespace sesame_ble
}  // namespace esphome
