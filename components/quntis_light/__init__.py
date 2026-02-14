import esphome.codegen as cg
from esphome.components import light

CODEOWNERS = ["@bluemaex"]
DEPENDENCIES = []
AUTO_LOAD = ["light"]

quntis_light_ns = cg.esphome_ns.namespace("quntis_light")
QuntisLight = quntis_light_ns.class_(
    "QuntisLight", light.LightOutput, cg.Component
)
