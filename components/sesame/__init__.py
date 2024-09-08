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
    DEVICE_CLASS_RUNNING,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_NONE,
    STATE_CLASS_MEASUREMENT,
)
import string

AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor", "lock"]
DEPENDENCIES = ["sensor", "text_sensor", "binary_sensor"]
CONFLICTS_WITH = ["esp32_ble"]
MULTI_CONF = True

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
SesameComponent = sesame_lock_ns.class_("SesameComponent", cg.PollingComponent)
SesameLock = sesame_lock_ns.class_("SesameLock", lock.Lock)
BotFeature = sesame_lock_ns.class_("BotFeature")

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
CONF_LOCK = "lock"
CONF_BOT = "bot"
CONF_RUNNING_SENSOR = "running_sensor"
CONF_ALWAYS_CONNECT = "always_connect"

SesameModel_t = cg.global_ns.enum("libsesame3bt::Sesame::model_t", True)
SESAME_MODELS = {
    "sesame_3": SesameModel_t.sesame_3,
    "sesame_bot": SesameModel_t.sesame_bot,
    "sesame_bike": SesameModel_t.sesame_bike,
    "sesame_cycle": SesameModel_t.sesame_bike,
    "sesame_4": SesameModel_t.sesame_4,
    "sesame_5": SesameModel_t.sesame_5,
    "sesame_bike_2": SesameModel_t.sesame_bike_2,
    "sesame_5_pro": SesameModel_t.sesame_5_pro,
    "open_sensor": SesameModel_t.open_sensor_1,
    "sesame_touch_pro": SesameModel_t.sesame_touch_pro,
    "sesame_touch": SesameModel_t.sesame_touch,
    "sesame_bot_2": SesameModel_t.sesame_bot_2,
}


def is_os3_model(model):
    return model not in ("sesame_3", "sesame_bot", "sesame_bike", "sesame_cycle", "sesame_4")


def is_lockable_model(model):
    return model not in ("open_sensor", "sesame_touch_pro", "sesame_touch", "sesame_bot_2")


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
    if not is_os3_model(config[CONF_MODEL]):
        if not config[CONF_PUBLIC_KEY]:
            raise cv.RequiredFieldInvalid("'public_key' is required for SESAME 3 / SESAME 4 / SESAME bot / SESAME Bike")
        valid_hexstring(CONF_PUBLIC_KEY, 128)(config[CONF_PUBLIC_KEY])
    return config


def validate_lockable(config):
    if not is_lockable_model(config[CONF_MODEL]):
        if CONF_LOCK in config:
            raise cv.Invalid(f"Cannot define 'lock' for {config[CONF_MODEL]}")
    return config


def validate_always_connect(config):
    if CONF_ALWAYS_CONNECT and not config[CONF_ALWAYS_CONNECT]:
        if CONF_LOCK in config or CONF_BOT in config:
            raise cv.Invalid("When using `lock` or `bot`, `always_connect` must be True")
    return config


def validate_bot_features(config):
    if CONF_LOCK in config and CONF_BOT in config:
        raise cv.Invalid("Cannot define both `lock` and `bot` on one Bot device")
    if CONF_BOT in config and config[CONF_MODEL] not in ("sesame_bot", "sesame_bot_2"):
        raise cv.Invalid("`bot` can be defined in Bot device")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SesameComponent),
            cv.Required(CONF_MODEL): cv.enum(SESAME_MODELS),
            cv.Optional(CONF_PUBLIC_KEY, default=""): cv.string,
            cv.Required(CONF_SECRET): valid_hexstring(CONF_SECRET, 32),
            cv.Required(CONF_ADDRESS): cv.mac_address,
            cv.Optional(CONF_LOCK): lock.LOCK_SCHEMA.extend(
                {
                    cv.GenerateID(): cv.declare_id(SesameLock),
                    cv.Optional(CONF_TAG, default="ESPHome"): cv.string,
                    cv.Optional(CONF_HISTORY_TAG): text_sensor.text_sensor_schema(),
                    cv.Optional(CONF_HISTORY_TYPE): sensor.sensor_schema(
                        unit_of_measurement=UNIT_EMPTY,
                        device_class=DEVICE_CLASS_EMPTY,
                        state_class=STATE_CLASS_NONE,
                        accuracy_decimals=0,
                    ),
                    cv.Optional(CONF_UNKNOWN_STATE_ALTERNATIVE): cv.enum(LOCK_STATES),
                    cv.Optional(CONF_UNKNOWN_STATE_TIMEOUT, default="20s"): cv.All(
                        cv.positive_time_period_seconds, cv.Range(max=cv.TimePeriod(seconds=255))
                    ),
                }
            ),
            cv.Optional(CONF_BOT): cv.Schema(
                {
                    cv.GenerateID(): cv.declare_id(BotFeature),
                    cv.Optional(CONF_RUNNING_SENSOR): binary_sensor.binary_sensor_schema(
                        device_class=DEVICE_CLASS_RUNNING,
                    ),
                }
            ),
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
            cv.Optional(CONF_CONNECT_RETRY_LIMIT): cv.int_range(min=0, max=65535),
            cv.Optional(CONF_CONNECTION_SENSOR): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
            ),
            cv.Optional(CONF_TIMEOUT, default="10s"): cv.All(cv.positive_time_period_seconds, cv.Range(max=cv.TimePeriod(seconds=255))),
            cv.Optional(CONF_ALWAYS_CONNECT, default=True): cv.boolean,
        }
    ).extend(cv.polling_component_schema("never")),
    validate_pubkey,
    validate_lockable,
    validate_always_connect,
    validate_bot_features,
    cv.only_with_arduino,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID], str(config[CONF_ID]))
    await cg.register_component(var, config)
    if CONF_BATTERY_PCT in config:
        s = await sensor.new_sensor(config[CONF_BATTERY_PCT])
        cg.add(var.set_battery_pct_sensor(s))
    if CONF_BATTERY_VOLTAGE in config:
        s = await sensor.new_sensor(config[CONF_BATTERY_VOLTAGE])
        cg.add(var.set_battery_voltage_sensor(s))
    if CONF_CONNECTION_SENSOR in config:
        s = await binary_sensor.new_binary_sensor(config[CONF_CONNECTION_SENSOR])
        cg.add(var.set_connection_sensor(s))
    if CONF_CONNECT_RETRY_LIMIT in config:
        cg.add(var.set_connect_retry_limit(config[CONF_CONNECT_RETRY_LIMIT]))
    if CONF_TIMEOUT in config:
        cg.add(var.set_connection_timeout_sec(config[CONF_TIMEOUT].total_seconds))
    if CONF_ALWAYS_CONNECT in config:
        cg.add(var.set_always_connect(config[CONF_ALWAYS_CONNECT]))

    if CONF_LOCK in config:
        lconfig = config[CONF_LOCK]
        lck = cg.new_Pvariable(lconfig[CONF_ID], var, config[CONF_MODEL], lconfig[CONF_TAG])
        await lock.register_lock(lck, config[CONF_LOCK])
        if CONF_HISTORY_TAG in lconfig:
            s = await text_sensor.new_text_sensor(lconfig[CONF_HISTORY_TAG])
            cg.add(lck.set_history_tag_sensor(s))
        if CONF_HISTORY_TYPE in lconfig:
            s = await sensor.new_sensor(lconfig[CONF_HISTORY_TYPE])
            cg.add(lck.set_history_type_sensor(s))
        if CONF_UNKNOWN_STATE_ALTERNATIVE in lconfig:
            cg.add(lck.set_unknown_state_alternative(lconfig[CONF_UNKNOWN_STATE_ALTERNATIVE]))
        if CONF_UNKNOWN_STATE_TIMEOUT in lconfig:
            cg.add(lck.set_unknown_state_timeout_sec(lconfig[CONF_UNKNOWN_STATE_TIMEOUT].total_seconds))
        cg.add(var.set_feature(lck))
        cg.add(lck.init())
    if CONF_BOT in config:
        bconfig = config[CONF_BOT]
        bot = cg.new_Pvariable(bconfig[CONF_ID], var, config[CONF_MODEL])
        if CONF_RUNNING_SENSOR in bconfig:
            s = await binary_sensor.new_binary_sensor(bconfig[CONF_RUNNING_SENSOR])
            cg.add(bot.set_running_sensor(s))
        cg.add(var.set_feature(bot))
        cg.add(bot.init())
    cg.add(var.init(config[CONF_MODEL], config.get(CONF_PUBLIC_KEY), config[CONF_SECRET], str(config[CONF_ADDRESS])))
