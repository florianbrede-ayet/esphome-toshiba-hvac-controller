import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, select, sensor, uart
from esphome.const import (
    CONF_ID,
    CONF_UART_ID,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)

AUTO_LOAD = ["sensor", "switch", "select"]
DEPENDENCIES = ["uart"]

# ToshibaController leeft in de esphome:: namespace
ToshibaController = cg.esphome_ns.class_(
    "ToshibaController", climate.Climate, cg.Component
)

CONF_TEMPERATURE_SENSOR = "temperature_sensor"
CONF_SPECIAL_MODE_SELECT = "special_mode_select"
CONF_SILENT_MODE_SELECT = "silent_mode_select"
CONF_FIREPLACE_SELECT = "fireplace_select"
CONF_SWING_MODE_SELECT = "swing_mode_select"
CONF_POWER_SELECT = "power_select"
CONF_SMART_THERMOSTAT_MULTIPLIER = "smart_thermostat_multiplier"
CONF_SMART_THERMOSTAT_RUNAWAY_PROTECTION = "smart_thermostat_runaway_protection"
CONF_DISABLE_COOLING_MODES = "disable_cooling_modes"

# Sensor output configs
CONF_OUTDOOR_TEMPERATURE = "outdoor_temperature"
CONF_FCU_AIR_TEMP = "fcu_air_temp"
CONF_FCU_SETPOINT = "fcu_setpoint"
CONF_FCU_TC_TEMP = "fcu_tc_temp"
CONF_FCU_TCJ_TEMP = "fcu_tcj_temp"
CONF_FCU_FAN_RPM = "fcu_fan_rpm"
CONF_CDU_TD_TEMP = "cdu_td_temp"
CONF_CDU_TS_TEMP = "cdu_ts_temp"
CONF_CDU_TE_TEMP = "cdu_te_temp"
CONF_CDU_LOAD = "cdu_load"
CONF_CDU_IAC = "cdu_iac"

CONFIG_SCHEMA = (
    climate.climate_schema(ToshibaController)
    .extend(
        {
            cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
            cv.Required(CONF_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),
            cv.Required(CONF_SPECIAL_MODE_SELECT): cv.use_id(select.Select),
            cv.Optional(CONF_SILENT_MODE_SELECT): cv.use_id(select.Select),
            cv.Optional(CONF_FIREPLACE_SELECT): cv.use_id(select.Select),
            cv.Required(CONF_SWING_MODE_SELECT): cv.use_id(select.Select),
            cv.Required(CONF_POWER_SELECT): cv.use_id(select.Select),
            cv.Optional(CONF_SMART_THERMOSTAT_MULTIPLIER, default=3.0): cv.float_range(
                min=1.0, max=10.0
            ),
            cv.Optional(
                CONF_SMART_THERMOSTAT_RUNAWAY_PROTECTION, default=True
            ): cv.boolean,
            cv.Optional(CONF_DISABLE_COOLING_MODES, default=False): cv.boolean,
            # Sensor outputs
            cv.Optional(CONF_OUTDOOR_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:home-thermometer-outline",
            ),
            cv.Optional(CONF_FCU_AIR_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:thermometer",
            ),
            cv.Optional(CONF_FCU_SETPOINT): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:thermometer",
            ),
            cv.Optional(CONF_FCU_TC_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:thermometer",
            ),
            cv.Optional(CONF_FCU_TCJ_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:thermometer",
            ),
            cv.Optional(CONF_FCU_FAN_RPM): sensor.sensor_schema(
                unit_of_measurement="rpm",
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:fan",
            ),
            cv.Optional(CONF_CDU_TD_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:thermometer",
            ),
            cv.Optional(CONF_CDU_TS_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:thermometer",
            ),
            cv.Optional(CONF_CDU_TE_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:thermometer",
            ),
            cv.Optional(CONF_CDU_LOAD): sensor.sensor_schema(
                unit_of_measurement="%",
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:heat-pump-outline",
            ),
            cv.Optional(CONF_CDU_IAC): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    uart_var = await cg.get_variable(config[CONF_UART_ID])
    temp_sensor = await cg.get_variable(config[CONF_TEMPERATURE_SENSOR])
    special_mode = await cg.get_variable(config[CONF_SPECIAL_MODE_SELECT])
    swing_mode = await cg.get_variable(config[CONF_SWING_MODE_SELECT])
    power_select = await cg.get_variable(config[CONF_POWER_SELECT])

    var = cg.new_Pvariable(
        config[CONF_ID],
        uart_var,
        temp_sensor,
        special_mode,
        swing_mode,
        power_select,
    )
    await cg.register_component(var, config)
    await climate.register_climate(var, config)

    cg.add(
        cg.RawExpression(
            f"{var}->config_settings().smart_thermostat_multiplier = "
            f"{config[CONF_SMART_THERMOSTAT_MULTIPLIER]}"
        )
    )
    cg.add(
        cg.RawExpression(
            f"{var}->config_settings().smart_thermostat_runaway_protection = "
            f"{'true' if config[CONF_SMART_THERMOSTAT_RUNAWAY_PROTECTION] else 'false'}"
        )
    )
    cg.add(
        cg.RawExpression(
            f"{var}->config_settings().disable_cooling_modes = "
            f"{'true' if config[CONF_DISABLE_COOLING_MODES] else 'false'}"
        )
    )

    # Wire up optional extra selects
    if CONF_SILENT_MODE_SELECT in config:
        silent_sel = await cg.get_variable(config[CONF_SILENT_MODE_SELECT])
        cg.add(var.set_silent_mode_select_ptr(silent_sel))
    if CONF_FIREPLACE_SELECT in config:
        fireplace_sel = await cg.get_variable(config[CONF_FIREPLACE_SELECT])
        cg.add(var.set_fireplace_select_ptr(fireplace_sel))

    # Wire up optional sensor outputs
    sensor_map = [
        (CONF_OUTDOOR_TEMPERATURE, "set_outdoor_temperature_sensor"),
        (CONF_FCU_AIR_TEMP,        "set_fcu_air_temp_sensor"),
        (CONF_FCU_SETPOINT,        "set_fcu_setpoint_sensor"),
        (CONF_FCU_TC_TEMP,         "set_fcu_tc_temp_sensor"),
        (CONF_FCU_TCJ_TEMP,        "set_fcu_tcj_temp_sensor"),
        (CONF_FCU_FAN_RPM,         "set_fcu_fan_rpm_sensor"),
        (CONF_CDU_TD_TEMP,         "set_cdu_td_temp_sensor"),
        (CONF_CDU_TS_TEMP,         "set_cdu_ts_temp_sensor"),
        (CONF_CDU_TE_TEMP,         "set_cdu_te_temp_sensor"),
        (CONF_CDU_LOAD,            "set_cdu_load_sensor"),
        (CONF_CDU_IAC,             "set_cdu_iac_sensor"),
    ]
    for conf_key, setter in sensor_map:
        if conf_key in config:
            sens = await sensor.new_sensor(config[conf_key])
            cg.add(getattr(var, setter)(sens))
