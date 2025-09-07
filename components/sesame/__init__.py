import string

from esphome import core
import esphome.codegen as cg
from esphome.components import binary_sensor, lock, sensor, text_sensor
import esphome.config as esp_config
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_ID,
    CONF_MODEL,
    CONF_TAG,
    CONF_TIMEOUT,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_EMPTY,
    DEVICE_CLASS_RUNNING,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_NONE,
    UNIT_EMPTY,
    UNIT_PERCENT,
    UNIT_VOLT,
)
from esphome.cpp_generator import MockObjClass
import esphome.final_validate as fv

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

sesame_server_ns = cg.esphome_ns.namespace("sesame_server")
SesameServerComponent = sesame_server_ns.class_("SesameServerComponent")

CONF_PUBLIC_KEY = "public_key"
CONF_SECRET = "secret"
CONF_BATTERY_PCT = "battery_pct"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_BATTERY_CRITICAL = "battery_critical"
CONF_HISTORY_TAG = "history_tag"
CONF_HISTORY_TYPE = "history_type"
CONF_TRIGGER_TYPE = "trigger_type"
CONF_CONNECT_RETRY_LIMIT = "connect_retry_limit"
CONF_UNKNOWN_STATE_ALTERNATIVE = "unknown_state_alternative"
CONF_CONNECTION_SENSOR = "connection_sensor"
CONF_UNKNOWN_STATE_TIMEOUT = "unknown_state_timeout"
CONF_LOCK = "lock"
CONF_BOT = "bot"
CONF_RUNNING_SENSOR = "running_sensor"
CONF_ALWAYS_CONNECT = "always_connect"
CONF_FAST_NOTIFY = "fast_notify"
CONF_SERVER_ID = "server_id"

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
    "remote": SesameModel_t.remote,
    "sesame_face_pro": SesameModel_t.sesame_face_pro,
    "sesame_face": SesameModel_t.sesame_face,
}


def is_os3_model(model):
    return model not in ("sesame_3", "sesame_bot", "sesame_bike", "sesame_cycle", "sesame_4")


def is_lockable_model(model):
    return model not in ("open_sensor", "sesame_touch_pro", "sesame_touch", "sesame_bot_2", "remote", "sesame_face_pro", "sesame_face")


def is_connectable_trigger_mode(model):
    return model in ("sesame_touch_pro", "sesame_touch", "remote", "sesame_face_pro", "sesame_face")


def add_sesame_server_references(config: esp_config.Config):
    """Add a reference to the Sesame server component if it exists in the config."""
    if not is_connectable_trigger_mode(config[CONF_MODEL]):
        return
    server_id = None
    for id, _ in esp_config.iter_ids(fv.full_config.get()):
        if id is None or not isinstance(id.type, MockObjClass):
            continue
        if id.is_declaration and id.type.inherits_from(SesameServerComponent):
            if server_id is not None:
                raise cv.Invalid("Only one Sesame server can be defined in the configuration")
            server_id = id
    if server_id is None:
        return
    if config[CONF_ALWAYS_CONNECT]:
        raise cv.Invalid("If SESAME Server co-exists in this device, `always_connect` must be False for Sesame Touch Pro / Sesame Touch / Remote")
    config[CONF_SERVER_ID] = core.ID(server_id.id, False, SesameServerComponent, False)


FINAL_VALIDATE_SCHEMA = add_sesame_server_references


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


cv.All(cv.version_number, cv.validate_esphome_version)("2025.5.0")

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SesameComponent),
            cv.Required(CONF_MODEL): cv.enum(SESAME_MODELS),
            cv.Optional(CONF_PUBLIC_KEY, default=""): cv.string,
            cv.Required(CONF_SECRET): valid_hexstring(CONF_SECRET, 32),
            cv.Required(CONF_ADDRESS): cv.mac_address,
            cv.Optional(CONF_LOCK): lock.lock_schema().extend(
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
                    cv.Optional(CONF_TRIGGER_TYPE): sensor.sensor_schema(
                        unit_of_measurement=UNIT_EMPTY,
                        device_class=DEVICE_CLASS_EMPTY,
                        state_class=STATE_CLASS_NONE,
                        accuracy_decimals=0,
                    ),
                    cv.Optional(CONF_UNKNOWN_STATE_ALTERNATIVE): cv.enum(LOCK_STATES),
                    cv.Optional(CONF_UNKNOWN_STATE_TIMEOUT, default="20s"): cv.positive_time_period_milliseconds,
                    cv.Optional(CONF_FAST_NOTIFY, default=False): cv.boolean,
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
                accuracy_decimals=2,
            ),
            cv.Optional(CONF_BATTERY_CRITICAL): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_BATTERY,
            ),
            cv.Optional(CONF_CONNECT_RETRY_LIMIT): cv.int_range(min=0, max=65535),
            cv.Optional(CONF_CONNECTION_SENSOR): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
            ),
            cv.Optional(CONF_TIMEOUT, default="10s"): cv.positive_time_period_milliseconds,
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
    if CONF_BATTERY_CRITICAL in config:
        s = await binary_sensor.new_binary_sensor(config[CONF_BATTERY_CRITICAL])
        cg.add(var.set_battery_critical_sensor(s))
    if CONF_CONNECT_RETRY_LIMIT in config:
        cg.add(var.set_connect_retry_limit(config[CONF_CONNECT_RETRY_LIMIT]))
    if CONF_TIMEOUT in config:
        cg.add(var.set_connection_timeout(config[CONF_TIMEOUT].total_milliseconds))
    if CONF_ALWAYS_CONNECT in config:
        cg.add(var.set_always_connect(config[CONF_ALWAYS_CONNECT]))
    if CONF_SERVER_ID in config:
        server = await cg.get_variable(config[CONF_SERVER_ID])
        cg.add(var.set_sesame_server(server))

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
        if CONF_TRIGGER_TYPE in lconfig:
            s = await sensor.new_sensor(lconfig[CONF_TRIGGER_TYPE])
            cg.add(lck.set_trigger_type_sensor(s))
        if CONF_UNKNOWN_STATE_ALTERNATIVE in lconfig:
            cg.add(lck.set_unknown_state_alternative(lconfig[CONF_UNKNOWN_STATE_ALTERNATIVE]))
        if CONF_UNKNOWN_STATE_TIMEOUT in lconfig:
            cg.add(lck.set_unknown_state_timeout(lconfig[CONF_UNKNOWN_STATE_TIMEOUT].total_milliseconds))
        if CONF_FAST_NOTIFY in lconfig:
            cg.add(lck.set_fast_notify(lconfig[CONF_FAST_NOTIFY]))
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
    cg.add_library("libsesame3bt", None, "https://github.com/homy-newfs8/libsesame3bt#0.27.0")
    # cg.add_library("libsesame3bt", None, "symlink://../../../../../../PlatformIO/Projects/libsesame3bt")
    # cg.add_library("libsesame3bt-core", None, "symlink://../../../../../../PlatformIO/Projects/libsesame3bt-core")
    # cg.add_library("libsesame3bt-server", None, "symlink://../../../../../../PlatformIO/Projects/libsesame3bt-server")
    # cg.add_platformio_option("lib_ldf_mode", "deep")
