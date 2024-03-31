import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, lock, sensor, text_sensor
from esphome.const import (
    CONF_ID,
    CONF_ADDRESS,
    CONF_MODEL,
    CONF_TAG,
    CONF_TIMEOUT,
    UNIT_EMPTY,
    UNIT_PERCENT,
    UNIT_VOLT,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_EMPTY,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_NONE,
    STATE_CLASS_MEASUREMENT,
)
import string

DEPENDENCIES = ["sensor", "text_sensor", "binary_sensor"]
CONFLICTS_WITH = ["esp32_ble"]

lock_ns = cg.esphome_ns.namespace("lock")
LockState_t = lock_ns.enum("LockState", False)
LOCK_STATES = {
    "NONE": LockState_t.LOCK_STATE_NONE,
    "LOCKED": LockState_t.LOCK_STATE_LOCKED,
    "UNLOCKED": LockState_t.LOCK_STATE_UNLOCKED,
    "JAMMED": LockState_t.LOCK_STATE_JAMMED,
    "LOCKING": LockState_t.LOCK_STATE_LOCKING,
    "UNLOCKING": LockState_t.LOCK_STATE_UNLOCKING,
}

sesame_lock_ns = cg.esphome_ns.namespace("sesame_lock")
SesameLock = sesame_lock_ns.class_("SesameLock", lock.Lock, cg.Component)

CONF_PUBLIC_KEY = "public_key"
CONF_SECRET = "secret"
CONF_BATTERY_PCT = "battery_pct"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_HISTORY_TAG = "history_tag"
CONF_HISTORY_TYPE = "history_type"
CONF_CONNECT_RETRY_LIMIT = "connect_retry_limit"
CONF_UNKNOWN_STATE_ALTERNATIVE = "unknown_state_alternative"
CONF_CONNECTION_SENSOR = "connection_sensor"
CONF_UNKNOWN_STATE_TIMEOUT = "unknown_state_timeout"

SesameModel_t = cg.global_ns.enum("libsesame3bt::Sesame::model_t", True)
SESAME_MODELS = {
    "sesame_3": SesameModel_t.sesame_3,
    "sesame_bot": SesameModel_t.sesame_bot,
    "sesame_bike": SesameModel_t.sesame_bike,
    "sesame_cycle": SesameModel_t.sesame_bike,
    "sesame_4": SesameModel_t.sesame_4,
    "sesame_5": SesameModel_t.sesame_5,
    "sesame_5_pro": SesameModel_t.sesame_5_pro,
}


def is_hex_string(str, valid_len):
    return len(str) == valid_len and all(c in string.hexdigits for c in str)


def valid_hexstring(key, valid_len):
    def func(str):
        if is_hex_string(str, valid_len):
            return str
        else:
            raise cv.Invalid(f"'{key}' must be a {valid_len} bytes hex string")

    return func


def validate_pubkey(config):
    if config[CONF_MODEL] not in ("sesame_5", "sesame_5_pro"):
        if not config[CONF_PUBLIC_KEY]:
            raise cv.RequiredFieldInvalid("'public_key' is required for SESAME 3 / SESAME 4 / SESAME bot / SESAME Bike")
        valid_hexstring(CONF_PUBLIC_KEY, 128)(config[CONF_PUBLIC_KEY])
    return config


CONFIG_SCHEMA = cv.All(
    lock.LOCK_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(SesameLock),
            cv.Required(CONF_MODEL): cv.enum(SESAME_MODELS),
            cv.Optional(CONF_PUBLIC_KEY, default=""): cv.string,
            cv.Required(CONF_SECRET): valid_hexstring(CONF_SECRET, 32),
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
            cv.Optional(CONF_CONNECT_RETRY_LIMIT): cv.int_range(min=0, max=65535),
            cv.Optional(CONF_UNKNOWN_STATE_ALTERNATIVE): cv.enum(LOCK_STATES),
            cv.Optional(CONF_CONNECTION_SENSOR): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
            ),
            cv.Optional(CONF_TIMEOUT, default="5s"): cv.All(cv.positive_time_period_seconds, cv.Range(max=cv.TimePeriod(seconds=255))),
            cv.Optional(CONF_UNKNOWN_STATE_TIMEOUT, default="15s"): cv.All(cv.positive_time_period_seconds, cv.Range(max=cv.TimePeriod(seconds=255))),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_pubkey,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await lock.register_lock(var, config)
    if CONF_BATTERY_PCT in config:
        s = await sensor.new_sensor(config[CONF_BATTERY_PCT])
        cg.add(var.set_battery_pct_sensor(s))
    if CONF_BATTERY_VOLTAGE in config:
        s = await sensor.new_sensor(config[CONF_BATTERY_VOLTAGE])
        cg.add(var.set_battery_voltage_sensor(s))
    if CONF_CONNECTION_SENSOR in config:
        s = await binary_sensor.new_binary_sensor(config[CONF_CONNECTION_SENSOR])
        cg.add(var.set_connection_sensor(s))
    if CONF_HISTORY_TAG in config:
        s = await text_sensor.new_text_sensor(config[CONF_HISTORY_TAG])
        cg.add(var.set_history_tag_sensor(s))
    if CONF_HISTORY_TYPE in config:
        s = await sensor.new_sensor(config[CONF_HISTORY_TYPE])
        cg.add(var.set_history_type_sensor(s))
    if CONF_CONNECT_RETRY_LIMIT in config:
        cg.add(var.set_connect_retry_limit(config[CONF_CONNECT_RETRY_LIMIT]))
    if CONF_UNKNOWN_STATE_ALTERNATIVE in config:
        cg.add(var.set_unknown_state_alternative(config[CONF_UNKNOWN_STATE_ALTERNATIVE]))
    if CONF_TIMEOUT in config:
        cg.add(var.set_connection_timeout_sec(config[CONF_TIMEOUT].total_seconds))
    if CONF_UNKNOWN_STATE_TIMEOUT in config:
        cg.add(var.set_connection_timeout_sec(config[CONF_UNKNOWN_STATE_TIMEOUT].total_seconds))
    cg.add(
        var.init(
            config[CONF_MODEL],
            config.get(CONF_PUBLIC_KEY),
            config[CONF_SECRET],
            str(config[CONF_ADDRESS]),
            config[CONF_TAG],
        )
    )
