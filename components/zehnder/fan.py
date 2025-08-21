import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import fan, sensor, text_sensor
from esphome.const import (
    CONF_ID,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_DURATION,
    UNIT_HOUR,
    UNIT_PERCENT,
    ICON_TIMER,
    ICON_PERCENT,
)

from esphome.components.nrf905 import nRF905Component


DEPENDENCIES = ["nrf905"]

zehnder_ns = cg.esphome_ns.namespace("zehnder")
ZehnderRF = zehnder_ns.class_("ZehnderRF", fan.FanState)

CONF_NRF905 = "nrf905"
CONF_FILTER_REMAINING = "filter_remaining"
CONF_FILTER_RUNTIME = "filter_runtime"
CONF_ERROR_COUNT = "error_count"
CONF_ERROR_CODE = "error_code"

CONFIG_SCHEMA = fan.fan_schema(ZehnderRF).extend(
    {
        cv.Required(CONF_NRF905): cv.use_id(nRF905Component),
        cv.Optional(CONF_UPDATE_INTERVAL, default="30s"): cv.update_interval,

        # Filter status sensors
        cv.Optional(CONF_FILTER_REMAINING): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            icon=ICON_PERCENT,
        ),
        cv.Optional(CONF_FILTER_RUNTIME): sensor.sensor_schema(
            unit_of_measurement=UNIT_HOUR,
            device_class=DEVICE_CLASS_DURATION,
            icon=ICON_TIMER,
        ),

        # Error sensors
        cv.Optional(CONF_ERROR_COUNT): sensor.sensor_schema(
            icon="mdi:alert-circle",  # Using string directly instead of constant
        ),
        cv.Optional(CONF_ERROR_CODE): text_sensor.text_sensor_schema(
            icon="mdi:alert",  # Using string directly instead of constant
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

    # Register sensors if defined
    if CONF_FILTER_REMAINING in config:
        sens = await sensor.new_sensor(config[CONF_FILTER_REMAINING])
        cg.add(var.set_filter_remaining_sensor(sens))

    if CONF_FILTER_RUNTIME in config:
        sens = await sensor.new_sensor(config[CONF_FILTER_RUNTIME])
        cg.add(var.set_filter_runtime_sensor(sens))

    if CONF_ERROR_COUNT in config:
        sens = await sensor.new_sensor(config[CONF_ERROR_COUNT])
        cg.add(var.set_error_count_sensor(sens))

    if CONF_ERROR_CODE in config:
        sens = await text_sensor.new_text_sensor(config[CONF_ERROR_CODE])
        cg.add(var.set_error_code_sensor(sens))
