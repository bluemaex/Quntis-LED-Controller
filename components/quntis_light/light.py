import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import CONF_OUTPUT_ID, CONF_DEFAULT_TRANSITION_LENGTH, CONF_GAMMA_CORRECT

from . import quntis_light_ns, QuntisLight

CONF_CE_PIN = "ce_pin"
CONF_CS_PIN = "cs_pin"
CONF_SPI_CLK_PIN = "spi_clk_pin"
CONF_SPI_MOSI_PIN = "spi_mosi_pin"
CONF_SPI_MISO_PIN = "spi_miso_pin"
CONF_DEVICE_ADDRESS = "device_address"
CONF_DEVICE_PAYLOAD = "device_payload"
CONF_BRIGHTNESS_STEPS = "brightness_steps"
CONF_COLOR_TEMP_STEPS = "color_temp_steps"
CONF_MIN_MIREDS = "min_mireds"
CONF_MAX_MIREDS = "max_mireds"
CONF_STEP_DELAY = "step_delay"

CONFIG_SCHEMA = (
    light.LIGHT_SCHEMA.extend(
        {
            # esphome
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(QuntisLight),
            cv.Optional(CONF_DEFAULT_TRANSITION_LENGTH, default="0s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_GAMMA_CORRECT, default=1.0): cv.positive_float,
            
            # ESP32 SPI pins
            cv.Required(CONF_CE_PIN): cv.int_range(min=0, max=48),
            cv.Required(CONF_CS_PIN): cv.int_range(min=0, max=48),
            cv.Required(CONF_SPI_CLK_PIN): cv.int_range(min=0, max=48),
            cv.Required(CONF_SPI_MOSI_PIN): cv.int_range(min=0, max=48),
            cv.Required(CONF_SPI_MISO_PIN): cv.int_range(min=0, max=48),

            # Light parameters
            cv.Optional(CONF_BRIGHTNESS_STEPS, default=75): cv.int_range(min=1, max=255),
            cv.Optional(CONF_COLOR_TEMP_STEPS, default=30): cv.int_range(min=1, max=255),
            cv.Optional(CONF_MIN_MIREDS, default=153): cv.int_range(min=1, max=1000),
            cv.Optional(CONF_MAX_MIREDS, default=500): cv.int_range(min=1, max=1000),
            cv.Optional(CONF_STEP_DELAY, default="50ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_DEVICE_PAYLOAD, default=[0x00, 0x76, 0x9A, 0x31]): cv.All(
                cv.ensure_list(cv.hex_uint8_t), 
                cv.Length(min=4, max=4)
            ),
            cv.Required(CONF_DEVICE_ADDRESS): cv.All(
                cv.ensure_list(cv.hex_uint8_t),
                cv.Length(min=5, max=5)
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    # This links to the patched RF24 library which supports the XN297 chip used in the Quntis remote
    # (enabling access to the private write_register function), see original README for details.
    cg.add_library("https://github.com/bluemaex/RF24-patched-xn297#37ab5462cbb4cf3d97aba71fb363f41e0b0d74b9", None)

    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await light.register_light(var, config)

    cg.add(var.set_ce_pin(config[CONF_CE_PIN]))
    cg.add(var.set_cs_pin(config[CONF_CS_PIN]))
    cg.add(var.set_spi_clk_pin(config[CONF_SPI_CLK_PIN]))
    cg.add(var.set_spi_mosi_pin(config[CONF_SPI_MOSI_PIN]))
    cg.add(var.set_spi_miso_pin(config[CONF_SPI_MISO_PIN]))

    addr = config[CONF_DEVICE_ADDRESS]
    cg.add(var.set_device_address(addr))

    payload = config[CONF_DEVICE_PAYLOAD]
    cg.add(var.set_device_payload(payload))

    cg.add(var.set_brightness_steps(config[CONF_BRIGHTNESS_STEPS]))
    cg.add(var.set_color_temp_steps(config[CONF_COLOR_TEMP_STEPS]))
    cg.add(var.set_min_mireds(config[CONF_MIN_MIREDS]))
    cg.add(var.set_max_mireds(config[CONF_MAX_MIREDS]))
    cg.add(var.set_step_delay(config[CONF_STEP_DELAY].total_milliseconds))
