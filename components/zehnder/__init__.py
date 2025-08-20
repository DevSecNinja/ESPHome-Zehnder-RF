import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["nrf905"]

zehnder_ns = cg.esphome_ns.namespace("zehnder")
ZehnderRF = zehnder_ns.class_("ZehnderRF")

# Service schema for setVoltage
SERVICE_SET_VOLTAGE = "set_voltage"
SERVICE_RESET_TO_AUTO = "reset_to_auto_mode"

SET_VOLTAGE_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(ZehnderRF),
    cv.Required("voltage"): cv.templatable(cv.int_range(min=0, max=100)),
    cv.Optional("timer", default=0): cv.templatable(cv.int_range(min=0, max=255)),
})

RESET_TO_AUTO_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(ZehnderRF),
})

CONFIG_SCHEMA = cv.Schema({})


def to_code(config):
    pass
