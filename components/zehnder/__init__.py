import esphome.codegen as cg
from esphome.components import fan

zehnder_ns = cg.esphome_ns.namespace("zehnder")
ZehnderRF = zehnder_ns.class_("ZehnderRF", fan.Fan)
