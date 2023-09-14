import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import lock
from esphome.const import CONF_ID, CONF_ADDRESS, CONF_MODEL

sesame_lock_ns = cg.esphome_ns.namespace('sesame_lock')
SesameLock = sesame_lock_ns.class_('SesameLock', lock.Lock, cg.Component)

CONF_PUBLIC_KEY = "public_key"
CONF_SECRET = "secret"

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

CONFIG_SCHEMA = lock.LOCK_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(SesameLock),
    cv.Required(CONF_MODEL): cv.enum(SESAME_MODELS),
    cv.Optional(CONF_PUBLIC_KEY, default=""): cv.string,
    cv.Required(CONF_SECRET): cv.string,
    cv.Required(CONF_ADDRESS): cv.mac_address
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    if config[CONF_MODEL] not in ("sesame_5", "sesame_5_pro") and not config[CONF_PUBLIC_KEY]:
        raise cv.RequiredFieldInvalid("public_key is required for SESAME 3 / SESAME 4 / SESAME Bot / SESAME Bike")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await lock.register_lock(var, config)
    cg.add(var.init(config[CONF_MODEL], config.get(CONF_PUBLIC_KEY), config[CONF_SECRET], str(config[CONF_ADDRESS])))
