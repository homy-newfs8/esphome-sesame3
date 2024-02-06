import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import lock
from esphome.components import sensor
from esphome.components import text_sensor
from esphome.const import (
    CONF_ID,
    CONF_ADDRESS,
    CONF_MODEL,
    CONF_TAG,
    UNIT_EMPTY,
    UNIT_PERCENT,
    UNIT_VOLT,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_EMPTY,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_NONE,
    STATE_CLASS_MEASUREMENT,
)

DEPENDENCIES = ["sensor", "text_sensor"]

sesame_lock_ns = cg.esphome_ns.namespace("sesame_lock")
SesameLock = sesame_lock_ns.class_("SesameLock", lock.Lock, cg.Component)

CONF_PUBLIC_KEY = "public_key"
CONF_SECRET = "secret"
CONF_BATTERY_PCT = "battery_pct"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_HISTORY_TAG = "history_tag"
CONF_HISTORY_TYPE = "history_type"

SesameModel_t = sesame_lock_ns.enum("model_t", True)
SESAME_MODELS = {
    "sesame_3": SesameModel_t.sesame_3,
    "sesame_bot": SesameModel_t.sesame_bot,
    "sesame_bike": SesameModel_t.sesame_bike,
    "sesame_cycle": SesameModel_t.sesame_bike,
    "sesame_4": SesameModel_t.sesame_4,
    "sesame_5": SesameModel_t.sesame_5,
    "sesame_5_pro": SesameModel_t.sesame_5_pro,
}

CONFIG_SCHEMA = lock.LOCK_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(SesameLock),
        cv.Required(CONF_MODEL): cv.enum(SESAME_MODELS),
        cv.Optional(CONF_PUBLIC_KEY, default=""): cv.string,
        cv.Required(CONF_SECRET): cv.string,
        cv.Required(CONF_ADDRESS): cv.mac_address,
        cv.Optional(CONF_TAG, default="ESPHome"): cv.string,
        cv.Optional(CONF_BATTERY_PCT): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            device_class=DEVICE_CLASS_BATTERY,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
        ),
        cv.Optional(CONF_BATTERY_VOLTAGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
        ),
        cv.Optional(CONF_HISTORY_TAG): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_HISTORY_TYPE): sensor.sensor_schema(
            unit_of_measurement=UNIT_EMPTY,
            device_class=DEVICE_CLASS_EMPTY,
            state_class=STATE_CLASS_NONE,
            accuracy_decimals=0,
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    if (
        config[CONF_MODEL] not in ("sesame_5", "sesame_5_pro")
        and not config[CONF_PUBLIC_KEY]
    ):
        raise cv.RequiredFieldInvalid(
            "public_key is required for SESAME 3 / SESAME 4 / SESAME bot / SESAME Bike"
        )
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await lock.register_lock(var, config)
    if CONF_BATTERY_PCT in config:
        s = await sensor.new_sensor(config[CONF_BATTERY_PCT])
        cg.add(var.set_battery_pct_sensor(s))
    if CONF_BATTERY_VOLTAGE in config:
        s = await sensor.new_sensor(config[CONF_BATTERY_VOLTAGE])
        cg.add(var.set_battery_voltage_sensor(s))
    if CONF_HISTORY_TAG in config:
        s = await text_sensor.new_text_sensor(config[CONF_HISTORY_TAG])
        cg.add(var.set_history_tag_sensor(s))
    if CONF_HISTORY_TYPE in config:
        s = await sensor.new_sensor(config[CONF_HISTORY_TYPE])
        cg.add(var.set_history_type_sensor(s))
    cg.add(
        var.init(
            config[CONF_MODEL],
            config.get(CONF_PUBLIC_KEY),
            config[CONF_SECRET],
            str(config[CONF_ADDRESS]),
            config[CONF_TAG],
        )
    )
