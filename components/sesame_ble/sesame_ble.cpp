#include "sesame_ble.h"
#include <libsesame3bt/ScannerCore.h>
#include <algorithm>
#include <unordered_map>
#include "esphome/core/log.h"

namespace esphome {
namespace sesame_ble {

using esphome::esp32_ble::ESPBTUUID;
using esphome::esp32_ble_tracker::ServiceData;
using libsesame3bt::Sesame;

namespace {

constexpr const char* TAG = "sesame_ble";

constexpr const char*
model_str(Sesame::model_t model) {
	switch (model) {
		case Sesame::model_t::sesame_3:
			return "3";
		case Sesame::model_t::wifi_2:
			return "Wi-Fi Module 2";
		case Sesame::model_t::sesame_bot:
			return "bot";
		case Sesame::model_t::sesame_bike:
			return "bike";
		case Sesame::model_t::sesame_4:
			return "4";
		case Sesame::model_t::sesame_5:
			return "5";
		case Sesame::model_t::sesame_5_pro:
			return "5 PRO";
		case Sesame::model_t::open_sensor_1:
			return "Open Sensor";
		case Sesame::model_t::sesame_touch_pro:
			return "Touch PRO";
		case Sesame::model_t::sesame_touch:
			return "Touch";
		default:
			return "UNKNOWN";
	}
}

static std::unordered_map<std::string, uint32_t> uniq_addrs;
static const ESPBTUUID SESAME_SRV_UUID = ESPBTUUID::from_raw(Sesame::SESAME3_SRV_UUID);

}  // namespace

bool
esphome::sesame_ble::SesameBleListener::parse_device(const esp32_ble_tracker::ESPBTDevice& device) {
	if (auto last = uniq_addrs[device.address_str()]; last && esphome::millis() - last < 10'000) {
		return false;
	}
	if (const auto& services = device.get_service_uuids();
	    std::find(std::cbegin(services), std::cend(services), SESAME_SRV_UUID) == std::cend(services)) {
		return false;
	}
	const ServiceData* found = nullptr;
	for (const auto& svd : device.get_manufacturer_datas()) {
		const auto& uuid = svd.uuid.get_uuid();
		if (uuid.len == ESP_UUID_LEN_16 && uuid.uuid.uuid16 == 0x055a) {
			found = &svd;
			break;
		}
	}
	if (!found) {
		return false;
	}
	auto manu_data = std::string{0x5a, 0x05} + std::string(reinterpret_cast<const char*>(found->data.data()), found->data.size());
	uint8_t uuid_bin[16];
	auto [model, flag_byte, is_valid] = libsesame3bt::core::parse_advertisement(manu_data, device.get_name(), uuid_bin);
	if (is_valid) {
		std::reverse(std::begin(uuid_bin), std::end(uuid_bin));
		auto uuid = ESPBTUUID::from_raw(uuid_bin);
		ESP_LOGI(TAG, "%s SESAME %s UUID=%s", device.address_str().c_str(), model_str(model), uuid.to_string().c_str());
		uniq_addrs[device.address_str()] = esphome::millis();
	}

	return is_valid;
}

}  // namespace sesame_ble
}  // namespace esphome
