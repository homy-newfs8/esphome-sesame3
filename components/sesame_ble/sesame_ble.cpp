#include "sesame_ble.h"
#include <SesameScanner.h>
#include <unordered_map>
#include "esphome/core/log.h"

namespace esphome {
namespace sesame_ble {

using esphome::esp32_ble::ESPBTUUID;
using esphome::esp32_ble_tracker::ServiceData;
using libsesame3bt::Sesame;
using libsesame3bt::SesameScanner;

namespace {

const ESPBTUUID SESAME_SRV_UUID = ESPBTUUID::from_raw("0000fd81-0000-1000-8000-00805f9b34fb");
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

}  // namespace

// static std::unordered_set<std::string> uniq_addrs;
// if (const auto& addr = x.address_str(); uniq_addrs.insert(addr).second) {
// 	if (const auto& uuids = x.get_service_uuids(); std::find(uuids.begin(), uuids.end(), SESAME_UUID) != uuids.end()) {
// 		ESP_LOGE(TAG, "address=%s", addr.c_str());
// 		std::string manu_data;
// 		for (const auto& sv : x.get_manufacturer_datas()) {
// 			const auto& uuid = sv.uuid.get_uuid();
// 			ESP_LOGE(TAG, "sv uuid.len=%02x", uuid.uuid.uuid16);
// 			if (uuid.len == ESP_UUID_LEN_16 && uuid.uuid.uuid16 == 0x055a) {
// 				ESP_LOGE(TAG, "found");
// 				std::vector<uint8_t> manu_vec(2 + sv.data.size());
// 				manu_vec[0] = 0x5a;
// 				manu_vec[1] = 0x05;
// 				std::copy(std::cbegin(sv.data), std::cend(sv.data), &manu_vec[2]);
// 				manu_data = std::string(reinterpret_cast<const char*>(manu_vec.data()), manu_vec.size());
// 				goto manu_done;
// 			}
// 			ESP_LOGE(TAG, "sv uuid=%s", sv.uuid.to_string().c_str());
// 			ESP_LOGE(TAG, "sv data=%s",
// 			         libsesame3bt::util::bin2hex(reinterpret_cast<const std::byte*>(sv.data.data()), sv.data.size()).c_str());
// 		}
// 	manu_done:;
// 		uint8_t uuid_bytes[16];
// 		auto [model, flags, is_sesame] = SesameScanner::parseAdvertisement(manu_data, x.get_name(), uuid_bytes);
// 		if (is_sesame) {
// 			ESP_LOGE(TAG, "sesame %u, %s", static_cast<uint8_t>(model),
// 			         BLEUUID{uuid_bytes, std::size(uuid_bytes), true}.toString().c_str());
// 		}
// 	}
// }

static std::unordered_map<std::string, uint32_t> uniq_addrs;

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
	auto [model, flag_byte, is_supported] = SesameScanner::parse_advertisement(manu_data, device.get_name(), uuid_bin);
	if (is_supported) {
		BLEUUID uuid{uuid_bin, std::size(uuid_bin), true};
		ESP_LOGI(TAG, "%s SESAME %s UUID=%s", device.address_str().c_str(), model_str(model), uuid.toString().c_str());
		uniq_addrs[device.address_str()] = esphome::millis();
	}

	return is_supported;
}

}  // namespace sesame_ble
}  // namespace esphome
