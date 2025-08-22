import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID

from . import zehnder_ns, ZehnderRF

DEPENDENCIES = ["zehnder"]

ZehnderRFStatusSensor = zehnder_ns.class_("ZehnderRFStatusSensor", binary_sensor.BinarySensor, cg.Component)

CONF_ZEHNDER_RF = "zehnder_rf"

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(ZehnderRFStatusSensor).extend(
    {
        cv.Required(CONF_ZEHNDER_RF): cv.use_id(ZehnderRF),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await binary_sensor.register_binary_sensor(var, config)

    parent = await cg.get_variable(config[CONF_ZEHNDER_RF])
    cg.add(var.set_parent(parent))