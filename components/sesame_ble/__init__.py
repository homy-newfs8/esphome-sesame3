import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32_ble_tracker
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32_ble_tracker"]

sesame_ble_ns = cg.esphome_ns.namespace("sesame_ble")
SesameBleListener = sesame_ble_ns.class_("SesameBleListener", esp32_ble_tracker.ESPBTDeviceListener)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SesameBleListener),
    }
).extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await esp32_ble_tracker.register_ble_device(var, config)
    cg.add_library(None, None, "https://github.com/homy-newfs8/libsesame3bt#0.21.0")
