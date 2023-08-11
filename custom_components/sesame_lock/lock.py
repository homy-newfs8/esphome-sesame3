import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import lock
from esphome.const import CONF_ID, CONF_ADDRESS

sesame_lock_ns = cg.esphome_ns.namespace('sesame_lock')
SesameLock = sesame_lock_ns.class_('SesameLock', lock.Lock, cg.Component)

CONF_PUBLIC_KEY = "public_key"
CONF_SECRET = "secret"

CONFIG_SCHEMA = lock.LOCK_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(SesameLock),
    cv.Required(CONF_PUBLIC_KEY): cv.string,
    cv.Required(CONF_SECRET): cv.string,
    cv.Required(CONF_ADDRESS): cv.mac_address
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await lock.register_lock(var, config)
    cg.add(var.init(config[CONF_PUBLIC_KEY], config[CONF_SECRET], str(config[CONF_ADDRESS])))
