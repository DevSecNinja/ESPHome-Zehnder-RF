import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import fan, binary_sensor
from esphome.const import CONF_ID, CONF_UPDATE_INTERVAL

from esphome.components.nrf905 import nRF905Component


DEPENDENCIES = ["nrf905"]

zehnder_ns = cg.esphome_ns.namespace("zehnder")
ZehnderRF = zehnder_ns.class_("ZehnderRF", fan.FanState)

CONF_NRF905 = "nrf905"
CONF_STATUS_SENSOR = "status_sensor"

CONFIG_SCHEMA = fan.FAN_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(ZehnderRF),
        cv.Required(CONF_NRF905): cv.use_id(nRF905Component),
        cv.Optional(CONF_UPDATE_INTERVAL, default="30s"): cv.update_interval,
        cv.Optional(CONF_STATUS_SENSOR): binary_sensor.binary_sensor_schema(
            icon="mdi:wifi-check"
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await fan.register_fan(var, config)

    nrf905 = await cg.get_variable(config[CONF_NRF905])
    cg.add(var.set_rf(nrf905))

    cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))

    if CONF_STATUS_SENSOR in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_STATUS_SENSOR])
        cg.add(var.set_status_sensor(sens))
