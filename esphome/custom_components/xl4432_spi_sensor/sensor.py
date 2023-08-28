import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import spi, sensor
from esphome.const import CONF_ID, ICON_EMPTY, UNIT_EMPTY

DEPENDENCIES = ['spi']

xl4432_spi_sensor_ns = cg.esphome_ns.namespace('xl4432_spi_sensor')
Xl4432SPISensor = xl4432_spi_sensor_ns.class_('Xl4432SPISensor', cg.PollingComponent,
                                  spi.SPIDevice)

CONFIG_SCHEMA = sensor.sensor_schema(unit_of_measurement=UNIT_EMPTY,icon=ICON_EMPTY, accuracy_decimals=1).extend({
    cv.GenerateID(): cv.declare_id(Xl4432SPISensor),
}).extend(cv.polling_component_schema('5s')).extend(spi.spi_device_schema())


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield sensor.register_sensor(var, config)
    yield spi.register_spi_device(var, config)
    