import esphome.codegen as cg
from esphome.components import esp32, esp32_ble_tracker
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

DEPENDENCIES = ["esp32_ble_tracker"]
AUTO_LOAD = ["esp32_ble_tracker"]

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
    cg.add_library("libsesame3bt", None, "https://github.com/homy-newfs8/libsesame3bt#0.30.1")

    if CORE.using_esp_idf:
        esp32.add_idf_component(name="h2zero/esp-nimble-cpp", ref="2.3.2")
        CORE.add_platformio_option("lib_ignore", "NimBLE-Arduino")
