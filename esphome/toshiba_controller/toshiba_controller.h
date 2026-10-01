/**
 * @file toshiba-controller.h
 * @brief Toshiba AC controller component for ESPHome
 *
 */
#include <cmath>
#include <deque>

#include "esphome/components/climate/climate.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"  // ESPHOME_VERSION_CODE / VERSION_CODE (pulls in macros.h)

static const char* const TAG = "toshiba-controller";

#define MIN_TEMP_SETPOINT_HEATING 5
#define MIN_TEMP_SETPOINT_COOLING 17
#define MAX_TEMP_SETPOINT 30

namespace esphome {

struct ConfigSettings {
    double smart_thermostat_multiplier = 4.0;
    bool smart_thermostat_runaway_protection = false;
    bool disable_cooling_modes = false;
};

enum CustomSwitchType { SWITCH_INTERNAL_THERMISTOR = 0, SWITCH_IONIZER = 1 };

class ToshibaController;

// Custom switch. Notifies the controller when state changes in HA.
class CustomSwitch final : public switch_::Switch {
    ToshibaController* controller_{nullptr};
    CustomSwitchType type_{SWITCH_INTERNAL_THERMISTOR};

public:
    CustomSwitch() = default;

    void set_controller(ToshibaController* c) { controller_ = c; }
    void set_type(CustomSwitchType t) { type_ = t; }

    void write_state(bool state) override;

    void restore_and_set_mode(switch_::SwitchRestoreMode mode) {
        set_restore_mode(mode);
        if (auto state = get_initial_state_with_restore_mode()) {
            write_state(*state);
        }
    }
};

static constexpr const char* CUSTOM_FAN_MODE_LOW_MEDIUM = "low medium";
static constexpr const char* CUSTOM_FAN_MODE_MEDIUM_HIGH = "medium high";

enum ToshibaSwingMode {
    SWING_MODE_OFF = 0x31,
    SWING_MODE_SWING_VERTICAL = 0x41,
    SWING_MODE_SWING_HORIZONTAL = 0x42,
    SWING_MODE_SWING_VERTICAL_AND_HORIZONTAL = 0x43,
    SWING_MODE_FIXED_1 = 0x50,
    SWING_MODE_FIXED_2 = 0x51,
    SWING_MODE_FIXED_3 = 0x52,
    SWING_MODE_FIXED_4 = 0x53,
    SWING_MODE_FIXED_5 = 0x54,
    SWING_MODE_NONE = 0x00,
};

enum ToshibaCommand {
    POWER_STATE = 0x80,
    POWER_SELECT = 0x87,
    FAN_MODE = 0xA0,
    SWING_MODE = 0xA3,
    MODE = 0xB0,
    TARGET_TEMPERATURE = 0xB3,
    ROOM_TEMPERATURE = 0xBB,
    OUTDOOR_TEMPERATURE = 0xBE,
    IONIZER = 0xC7,
    WIFILED1 = 0xDE,  // variant 1: on=0x05, off=0x00
    WIFILED2 = 0xDF,  // variant 2: on=0x00, off=0x80
    SPECIAL_MODE = 0xF7,
    IDU_STATUS = 0xE4,
    ODU_STATUS = 0xE5,
};

enum ToshibaSpecialModes {
    SPECIAL_MODE_STANDARD = 0x00,
    SPECIAL_MODE_HIGH_POWER = 0x01,
    SPECIAL_MODE_ECO = 0x03,
    SPECIAL_MODE_EIGHT_DEGREES = 0x04,
    SPECIAL_MODE_FIREPLACE_1 = 0x20,
    SPECIAL_MODE_FIREPLACE_2 = 0x30,
    SPECIAL_MODE_SILENT_1 = 0x02,
    SPECIAL_MODE_SILENT_2 = 0x0A,
    SPECIAL_MODE_SLEEP_CARE = 0x05,
    SPECIAL_MODE_FLOOR = 0x06,
    SPECIAL_MODE_COMFORT = 0x07,
};

enum ToshibaState {
    STATE_ON = 0x30,
    STATE_OFF = 0x31,
};

enum ToshibaMode {
    MODE_HEAT_COOL = 0x41,
    MODE_COOL = 0x42,
    MODE_HEAT = 0x43,
    MODE_DRY = 0x44,
    MODE_FAN_ONLY = 0x45,
};

enum ToshibaFanMode {
    FAN_QUIET = 0x31,
    FAN_LOW = 0x32,
    FAN_LOW_MEDIUM = 0x33,
    FAN_MEDIUM = 0x34,
    FAN_MEDIUM_HIGH = 0x35,
    FAN_HIGH = 0x36,
    FAN_AUTO = 0x41,
};

enum ToshibaPowerSelection {
    POWER_50 = 0x32,
    POWER_75 = 0x4B,
    POWER_100 = 0x64,
};

enum ToshibaIonizer { IONIZER_ON = 0x18, IONIZER_OFF = 0x10 };

enum ToshibaSelfCleaning { SELF_CLEANING_ON = 0x18, SELF_CLEANING_OFF = 0x10 };

static const std::vector<std::vector<uint8_t>> IDU_HANDSHAKE = {
    {0x02, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x02},
    {0x02, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x01, 0x02, 0xFE},
    {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x02, 0xFA},
    {0x02, 0x00, 0x01, 0x81, 0x01, 0x00, 0x02, 0x00, 0x00, 0x7B},
    {0x02, 0x00, 0x01, 0x02, 0x00, 0x00, 0x02, 0x00, 0x00, 0xFE},
    {0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0xFE}};

static const std::vector<std::vector<uint8_t>> IDU_POST_HANDSHAKE = {
    {0x02, 0x00, 0x02, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0xFB},
    //{0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0xFC}, // works as well, did not observe different
    //behaviour
    {0x02, 0x00, 0x02, 0x02, 0x00, 0x00, 0x02, 0x00, 0x00, 0xFA}};

class ToshibaController final : public climate::Climate, public Component {
    climate::ClimateTraits supported_traits_;

    esphome::uart::UARTComponent* serial_{nullptr};
    esphome::sensor::Sensor* temperature_sensor_{nullptr};
    esphome::select::Select* swing_mode_select_{nullptr};
    esphome::select::Select* special_mode_select_{nullptr};
    esphome::select::Select* silent_mode_select_{nullptr};
    esphome::select::Select* fireplace_select_{nullptr};
    esphome::select::Select* power_selection_select_{nullptr};

    uint32_t last_partial_register_request_millis_ = 0;
    uint32_t last_full_register_request_millis_ = 0;
    uint32_t last_external_temperature_sensor_control_millis_ = 0;

    CustomSwitch* switch_internal_thermistor_{nullptr};
    CustomSwitch* switch_ionizer_{nullptr};

    uint8_t recv_buf_[256] = {};
    uint32_t recv_buf_len_ = 0;
    uint32_t last_recv_millis_ = 0;

    std::vector<std::vector<uint8_t>> send_msg_queue_;
    uint32_t last_sent_millis_ = 0;

    ConfigSettings config_settings_;

    bool updating_from_ac_ = false;  // suppress on_value callbacks during AC-initiated state updates

    ToshibaState internal_power_state_ = ToshibaState::STATE_OFF;
    ToshibaFanMode internal_fan_mode_ = ToshibaFanMode::FAN_MEDIUM;
    ToshibaSwingMode internal_swing_mode_ = ToshibaSwingMode::SWING_MODE_OFF;
    ToshibaSpecialModes internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_STANDARD;
    ToshibaPowerSelection internal_power_selection_ = ToshibaPowerSelection::POWER_100;
    uint8_t internal_target_temperature_ = 20;

    bool is_initialized_ = false;

    int8_t internal_idu_room_temperature_ = 0;

    sensor::Sensor* sensor_outdoor_temperature_{nullptr};
    sensor::Sensor* sensor_cdu_td_temp_{nullptr};
    sensor::Sensor* sensor_cdu_ts_temp_{nullptr};
    sensor::Sensor* sensor_cdu_te_temp_{nullptr};
    sensor::Sensor* sensor_cdu_load_{nullptr};
    sensor::Sensor* sensor_cdu_iac_{nullptr};
    sensor::Sensor* sensor_fcu_air_temp_{nullptr};
    sensor::Sensor* sensor_fcu_setpoint_temp_{nullptr};
    sensor::Sensor* sensor_fcu_tc_temp_{nullptr};
    sensor::Sensor* sensor_fcu_tcj_temp_{nullptr};
    sensor::Sensor* sensor_fcu_fan_rpm_{nullptr};

    uint64_t loop_cnt_ = 0;

    uint8_t calc_checksum(uint8_t* data, uint8_t length) {
        uint8_t sum = 0;
        for (size_t i = 1; i < length; i++) {
            sum += data[i];
        }
        return (-sum) & 0xFF;
    }

    void process_uart_tx() {
        if (millis() - last_sent_millis_ < 50) {
            return;
        }

        if (recv_buf_len_ > 0 || millis() - last_recv_millis_ < 50) {
            return;
        }

        if (send_msg_queue_.size() == 0) {
            return;
        }

        ESP_LOGD(TAG, "sending: %s", format_hex_pretty(send_msg_queue_.front()).c_str());
        last_sent_millis_ = millis();
        serial_->write_array(send_msg_queue_.front());
        send_msg_queue_.erase(send_msg_queue_.begin());
        ESP_LOGD(TAG, "finished sending");
    }

    void handle_register_mode(ToshibaMode value) {
        if (this->internal_power_state_ == ToshibaState::STATE_OFF) {
            ESP_LOGE(TAG, "[REGISTER] received mode %s, but IDU is powered off",
                     format_hex_pretty((uint8_t)value).c_str());
            this->mode = climate::CLIMATE_MODE_OFF;
            this->publish_state();
            return;
        }

        // if cooling mode is disabled, switch to fan only mode for unsupported modes
        // we don't turn off the unit here, because it collides with the "power state before mode change" logic
        if (this->config_settings_.disable_cooling_modes &&
            (value == ToshibaMode::MODE_COOL || value == ToshibaMode::MODE_DRY ||
             value == ToshibaMode::MODE_HEAT_COOL)) {
            ESP_LOGI(TAG, "[REGISTER] received mode: %s, but cooling mode is disabled for this unit",
                     format_hex_pretty((uint8_t)value).c_str());
            this->mode = climate::CLIMATE_MODE_FAN_ONLY;
            this->publish_state();
            request_write_register_(ToshibaCommand::MODE, ToshibaMode::MODE_FAN_ONLY);
            return;
        }

        switch (value) {
            case ToshibaMode::MODE_HEAT_COOL:
                ESP_LOGI(TAG, "[REGISTER] received mode: %s", "HEAT_COOL");
                this->mode = climate::CLIMATE_MODE_HEAT_COOL;
                break;
            case ToshibaMode::MODE_COOL:
                ESP_LOGI(TAG, "[REGISTER] received mode: %s", "COOL");
                this->mode = climate::CLIMATE_MODE_COOL;
                break;
            case ToshibaMode::MODE_HEAT:
                ESP_LOGI(TAG, "[REGISTER] received mode: %s", "HEAT");
                this->mode = climate::CLIMATE_MODE_HEAT;
                break;
            case ToshibaMode::MODE_DRY:
                ESP_LOGI(TAG, "[REGISTER] received mode: %s", "DRY");
                this->mode = climate::CLIMATE_MODE_DRY;
                break;
            case ToshibaMode::MODE_FAN_ONLY:
                ESP_LOGI(TAG, "[REGISTER] received mode: %s", "FAN_ONLY");
                this->mode = climate::CLIMATE_MODE_FAN_ONLY;
                break;
            default:
                ESP_LOGE(TAG, "[REGISTER] received unknown mode: %s", format_hex_pretty((uint8_t)value).c_str());
                this->mode = climate::CLIMATE_MODE_OFF;
                break;
        }
        this->publish_state();
    }

    void handle_register_target_temperature(uint8_t value, bool is_external_change) {
        ESP_LOGI(TAG, "[REGISTER] received target temperature: %d (external change: %s)", value,
                 is_external_change ? "true" : "false");

        // this is unreliable across units, so we discard the 15 byte "external change" temperature messages.
        // instead we frequently poll the IDU target temperature and synchronize it with our internal state
        if (is_external_change) {
            ESP_LOGI(TAG, "ignoring unreliable external change message");
            return;
        }

        if (this->internal_special_mode_ == ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES && value - 16 < MIN_TEMP_SETPOINT_HEATING) {
            ESP_LOGE(TAG, "received target temperature %d in SPECIAL_MODE_EIGHT_DEGREES - this is below the raw minimum of %d degrees",
                     value, MIN_TEMP_SETPOINT_HEATING+16);
            return;
        }
        if (this->internal_special_mode_ != ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES && value > MAX_TEMP_SETPOINT) {
            ESP_LOGE(TAG, "received target temperature %d - this is above the maximum of %d degrees",
                     value, MAX_TEMP_SETPOINT);
            return;
        }

        if (millis()-this->last_partial_register_request_millis_>=2500) {
            ESP_LOGE(TAG, "received target temperature %d after %d ms - possibly out of sync, skipping update", value, millis()-this->last_partial_register_request_millis_);
            return;
        }
        
        const uint8_t old_internal_target_temperature = this->internal_target_temperature_;

        if (this->internal_special_mode_ == ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES) {
            this->internal_target_temperature_ = value - 16;
        } else {
            this->internal_target_temperature_ = value;
        }
        if (sensor_fcu_setpoint_temp_) sensor_fcu_setpoint_temp_->publish_state(this->internal_target_temperature_);

        if (switch_internal_thermistor_ && switch_internal_thermistor_->state) {
            // only sync target_temperature from AC when using internal thermistor
            // with external sensor, target_temperature is the user's setpoint and must not be overwritten by smart thermostat adjustments
            this->target_temperature = this->internal_target_temperature_;
            this->publish_state();
        }
    }

    void handle_register_power_state(ToshibaState value) {
        switch (value) {
            case ToshibaState::STATE_ON:
                ESP_LOGI(TAG, "[REGISTER] received power state: %s", "ON");
                if (this->internal_power_state_ == ToshibaState::STATE_OFF) {
                    request_read_register_(ToshibaCommand::MODE);
                    request_read_register_(ToshibaCommand::TARGET_TEMPERATURE);
                }
                break;
            case ToshibaState::STATE_OFF:
                ESP_LOGI(TAG, "[REGISTER] received power state: %s", "OFF");
                this->mode = climate::CLIMATE_MODE_OFF;
                this->publish_state();
                break;
            default:
                ESP_LOGE(TAG, "[REGISTER] received unknown power state: %s", format_hex_pretty((uint8_t)value).c_str());
                break;
        }
        this->internal_power_state_ = value;
    }

    void handle_register_fan_mode(ToshibaFanMode value) {
        switch (value) {
            case ToshibaFanMode::FAN_AUTO:
                ESP_LOGI(TAG, "[REGISTER] received fan mode: %s", "FAN_MODE_AUTO");
                this->set_fan_mode_(climate::CLIMATE_FAN_AUTO);
                break;
            case ToshibaFanMode::FAN_QUIET:
                ESP_LOGI(TAG, "[REGISTER] received fan mode: %s", "FAN_MODE_QUIET");
                this->set_fan_mode_(climate::CLIMATE_FAN_QUIET);
                break;
            case ToshibaFanMode::FAN_LOW:
                ESP_LOGI(TAG, "[REGISTER] received fan mode: %s", "FAN_MODE_LOW");
                this->set_fan_mode_(climate::CLIMATE_FAN_LOW);
                break;
            case ToshibaFanMode::FAN_LOW_MEDIUM:
                ESP_LOGI(TAG, "[REGISTER] received fan mode: %s", "CUSTOM_FAN_MODE_LOW_MEDIUM");
                this->set_custom_fan_mode_(CUSTOM_FAN_MODE_LOW_MEDIUM);
                break;
            case ToshibaFanMode::FAN_MEDIUM:
                ESP_LOGI(TAG, "[REGISTER] received fan mode: %s", "FAN_MODE_MEDIUM");
                this->set_fan_mode_(climate::CLIMATE_FAN_MEDIUM);
                break;
            case ToshibaFanMode::FAN_MEDIUM_HIGH:
                ESP_LOGI(TAG, "[REGISTER] received fan mode: %s", "CUSTOM_FAN_MODE_MEDIUM_HIGH");
                this->set_custom_fan_mode_(CUSTOM_FAN_MODE_MEDIUM_HIGH);
                break;
            case ToshibaFanMode::FAN_HIGH:
                ESP_LOGI(TAG, "[REGISTER] received fan mode: %s", "FAN_MODE_HIGH");
                this->set_fan_mode_(climate::CLIMATE_FAN_HIGH);
                break;
            default:
                ESP_LOGE(TAG, "[REGISTER] received unknown fan mode: %s", format_hex_pretty((uint8_t)value).c_str());
                break;
        }

        this->publish_state();
        this->internal_fan_mode_ = value;
    }

    void handle_register_swing_mode(ToshibaSwingMode value) {
        switch (value) {
            case ToshibaSwingMode::SWING_MODE_OFF:
                ESP_LOGI(TAG, "[REGISTER] received swing mode: %s", "OFF");
                this->swing_mode = climate::CLIMATE_SWING_OFF;
                break;
            case ToshibaSwingMode::SWING_MODE_SWING_VERTICAL:
                ESP_LOGI(TAG, "[REGISTER] received swing mode: %s", "SWING_VERTICAL");
                this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
                break;
            case ToshibaSwingMode::SWING_MODE_SWING_HORIZONTAL:
                ESP_LOGI(TAG, "[REGISTER] received swing mode: %s", "SWING_HORIZONTAL");
                this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
                break;
            case ToshibaSwingMode::SWING_MODE_SWING_VERTICAL_AND_HORIZONTAL:
                ESP_LOGI(TAG, "[REGISTER] received swing mode: %s", "SWING_VERTICAL_AND_HORIZONTAL");
                this->swing_mode = climate::CLIMATE_SWING_BOTH;
                break;
            case ToshibaSwingMode::SWING_MODE_FIXED_1:
                ESP_LOGI(TAG, "[REGISTER] received swing mode: %s", "FIXED_1");
                this->swing_mode = climate::CLIMATE_SWING_OFF;
                break;
            case ToshibaSwingMode::SWING_MODE_FIXED_2:
                ESP_LOGI(TAG, "[REGISTER] received swing mode: %s", "FIXED_2");
                this->swing_mode = climate::CLIMATE_SWING_OFF;
                break;
            case ToshibaSwingMode::SWING_MODE_FIXED_3:
                ESP_LOGI(TAG, "[REGISTER] received swing mode: %s", "FIXED_3");
                this->swing_mode = climate::CLIMATE_SWING_OFF;
                break;
            case ToshibaSwingMode::SWING_MODE_FIXED_4:
                ESP_LOGI(TAG, "[REGISTER] received swing mode: %s", "FIXED_4");
                this->swing_mode = climate::CLIMATE_SWING_OFF;
                break;
            case ToshibaSwingMode::SWING_MODE_FIXED_5:
                ESP_LOGI(TAG, "[REGISTER] received swing mode: %s", "FIXED_5");
                this->swing_mode = climate::CLIMATE_SWING_OFF;
                break;
            default:
                ESP_LOGE(TAG, "[REGISTER] received unknown swing mode: %s", format_hex_pretty((uint8_t)value).c_str());
                break;
        }
        this->publish_state();
        this->internal_swing_mode_ = value;
    }

    void handle_register_special_mode(ToshibaSpecialModes value) {
        updating_from_ac_ = true;
        // Reset all selects to their neutral value first, then set the active one
        bool is_silent = (value == ToshibaSpecialModes::SPECIAL_MODE_SILENT_1 ||
                          value == ToshibaSpecialModes::SPECIAL_MODE_SILENT_2);
        bool is_fireplace = (value == ToshibaSpecialModes::SPECIAL_MODE_FIREPLACE_1 ||
                             value == ToshibaSpecialModes::SPECIAL_MODE_FIREPLACE_2);

        if (!is_silent && silent_mode_select_)
            silent_mode_select_->publish_state("Off");
        if (!is_fireplace && fireplace_select_)
            fireplace_select_->publish_state("Off");

        switch (value) {
            case ToshibaSpecialModes::SPECIAL_MODE_STANDARD:
                ESP_LOGI(TAG, "[REGISTER] received special mode: %s", "STANDARD");
                this->special_mode_select_->publish_state("Standard");
                break;
            case ToshibaSpecialModes::SPECIAL_MODE_HIGH_POWER:
                ESP_LOGI(TAG, "[REGISTER] received special mode: %s", "HIGH_POWER");
                this->special_mode_select_->publish_state("High Power");
                break;
            case ToshibaSpecialModes::SPECIAL_MODE_ECO:
                ESP_LOGI(TAG, "[REGISTER] received special mode: %s", "ECO");
                this->special_mode_select_->publish_state("ECO");
                break;
            case ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES:
                ESP_LOGI(TAG, "[REGISTER] received special mode: %s", "EIGHT_DEGREES");
                this->special_mode_select_->publish_state("8 Degrees");
                break;
            case ToshibaSpecialModes::SPECIAL_MODE_SLEEP_CARE:
                ESP_LOGI(TAG, "[REGISTER] received special mode: %s", "SLEEP_CARE");
                this->special_mode_select_->publish_state("Sleep Care");
                break;
            case ToshibaSpecialModes::SPECIAL_MODE_FLOOR:
                ESP_LOGI(TAG, "[REGISTER] received special mode: %s", "FLOOR");
                this->special_mode_select_->publish_state("Floor");
                break;
            case ToshibaSpecialModes::SPECIAL_MODE_COMFORT:
                ESP_LOGI(TAG, "[REGISTER] received special mode: %s", "COMFORT");
                this->special_mode_select_->publish_state("Comfort");
                break;
            case ToshibaSpecialModes::SPECIAL_MODE_SILENT_1:
                ESP_LOGI(TAG, "[REGISTER] received special mode: %s", "SILENT_1");
                this->special_mode_select_->publish_state("Standard");
                if (silent_mode_select_) silent_mode_select_->publish_state("Silent 1");
                break;
            case ToshibaSpecialModes::SPECIAL_MODE_SILENT_2:
                ESP_LOGI(TAG, "[REGISTER] received special mode: %s", "SILENT_2");
                this->special_mode_select_->publish_state("Standard");
                if (silent_mode_select_) silent_mode_select_->publish_state("Silent 2");
                break;
            case ToshibaSpecialModes::SPECIAL_MODE_FIREPLACE_1:
                ESP_LOGI(TAG, "[REGISTER] received special mode: %s", "FIREPLACE_1");
                this->special_mode_select_->publish_state("Standard");
                if (fireplace_select_) fireplace_select_->publish_state("Fireplace 1");
                break;
            case ToshibaSpecialModes::SPECIAL_MODE_FIREPLACE_2:
                ESP_LOGI(TAG, "[REGISTER] received special mode: %s", "FIREPLACE_2");
                this->special_mode_select_->publish_state("Standard");
                if (fireplace_select_) fireplace_select_->publish_state("Fireplace 2");
                break;
            default:
                ESP_LOGE(TAG, "[REGISTER] received unknown special mode: %s",
                         format_hex_pretty((uint8_t)value).c_str());
                break;
        }
        this->internal_special_mode_ = value;
        updating_from_ac_ = false;
    }

    void handle_register_ionizer(ToshibaIonizer value) {
        switch (value) {
            case ToshibaIonizer::IONIZER_ON:
                ESP_LOGI(TAG, "[REGISTER] received ionizer state: %s", "ON");
                if (switch_ionizer_) switch_ionizer_->publish_state(true);
                break;
            case ToshibaIonizer::IONIZER_OFF:
                ESP_LOGI(TAG, "[REGISTER] received ionizer state: %s", "OFF");
                if (switch_ionizer_) switch_ionizer_->publish_state(false);
                break;
            default:
                ESP_LOGE(TAG, "[REGISTER] received unknown ionizer state: %s",
                         format_hex_pretty((uint8_t)value).c_str());
                break;
        }
    }

    void handle_register_power_selection(ToshibaPowerSelection value) {
        // Update internal state BEFORE publish_state to prevent write-back loop:
        // publish_state triggers on_value -> set_power_select -> checks internal_power_selection_
        this->internal_power_selection_ = value;
        switch (value) {
            case ToshibaPowerSelection::POWER_50:
                ESP_LOGI(TAG, "[REGISTER] received power select: %s", "50%");
                power_selection_select_->publish_state("50%");
                break;
            case ToshibaPowerSelection::POWER_75:
                ESP_LOGI(TAG, "[REGISTER] received power select: %s", "75%");
                power_selection_select_->publish_state("75%");
                break;
            case ToshibaPowerSelection::POWER_100:
                ESP_LOGI(TAG, "[REGISTER] received power select: %s", "100%");
                power_selection_select_->publish_state("100%");
                break;
            default:
                ESP_LOGE(TAG, "[REGISTER] received unknown power select: %s",
                         format_hex_pretty((uint8_t)value).c_str());
                break;
        }
    }

    void handle_register_room_temperature(uint8_t value) {
        ESP_LOGI(TAG, "[REGISTER] received room temperature: %d", value);
        this->internal_idu_room_temperature_ = value;
        if (sensor_fcu_air_temp_) sensor_fcu_air_temp_->publish_state(value);

        if (switch_internal_thermistor_ && switch_internal_thermistor_->state) {
            this->current_temperature = (float)value;
            this->publish_state();
        }
    }

    void handle_register_outdoor_temperature(int8_t value) {
        if (value == 127) {
            ESP_LOGD(TAG, "[REGISTER] outdoor temperature unavailable (127), skipping");
            return;
        }
        ESP_LOGI(TAG, "[REGISTER] received outdoor temperature: %d", value);
        if (sensor_outdoor_temperature_) sensor_outdoor_temperature_->publish_state(value);
    }

    void handle_message() {
        if (recv_buf_len_ < 4) {
            ESP_LOGD(TAG, "handle message too short (%d)", recv_buf_len_);
            return;
        }
        if (recv_buf_len_ > 30) {
            ESP_LOGD(TAG, "handle message too long (%d)", recv_buf_len_);
            return;
        }

        ESP_LOGD(TAG, "handle message: %s", format_hex_pretty(recv_buf_, recv_buf_len_).c_str());
        if (recv_buf_[0] != 0x02 || recv_buf_[1] != 0x00 || recv_buf_[2] != 0x03) {
            if (recv_buf_[3] == 0x80) {
                ESP_LOGD(TAG, "received handshake reply: %s", format_hex_pretty(recv_buf_, recv_buf_len_).c_str());
            } else if (recv_buf_[3] == 0x82) {
                ESP_LOGD(TAG, "received post handshake reply: %s", format_hex_pretty(recv_buf_, recv_buf_len_).c_str());
            } else {
                ESP_LOGE(TAG, "invalid message header for: %s", format_hex_pretty(recv_buf_, recv_buf_len_).c_str());
            }
            return;
        }

        uint8_t checksum = calc_checksum(recv_buf_, recv_buf_len_ - 1);
        if (checksum != recv_buf_[recv_buf_len_ - 1]) {
            ESP_LOGE(TAG, "invalid calculated checksum %s for: %s", format_hex_pretty(checksum).c_str(),
                     format_hex_pretty(recv_buf_, recv_buf_len_).c_str());
            return;
        }

        // parse single register message
        if (recv_buf_len_ == 15 || recv_buf_len_ == 17) {
            uint8_t command = recv_buf_[recv_buf_len_ - 3];
            uint8_t value = recv_buf_[recv_buf_len_ - 2];
            ESP_LOGI(TAG, "received register message: %s with value %d", format_hex_pretty(command).c_str(), value);
            switch (command) {
                case ToshibaCommand::MODE:
                    ESP_LOGD(TAG, "received mode: %s", format_hex_pretty(value).c_str());
                    handle_register_mode(static_cast<ToshibaMode>(value));
                    break;
                case ToshibaCommand::POWER_STATE:
                    ESP_LOGD(TAG, "received power state: %s", format_hex_pretty(value).c_str());
                    handle_register_power_state(static_cast<ToshibaState>(value));
                    break;
                case ToshibaCommand::TARGET_TEMPERATURE:
                    ESP_LOGD(TAG, "received target temperature: %s", format_hex_pretty(value).c_str());
                    handle_register_target_temperature(value, recv_buf_len_ == 15 ? true : false);
                    break;
                case ToshibaCommand::FAN_MODE:
                    ESP_LOGD(TAG, "received fan mode: %s", format_hex_pretty(value).c_str());
                    handle_register_fan_mode(static_cast<ToshibaFanMode>(value));
                    break;
                case ToshibaCommand::SWING_MODE:
                    ESP_LOGD(TAG, "received swing mode: %s", format_hex_pretty(value).c_str());
                    handle_register_swing_mode(static_cast<ToshibaSwingMode>(value));
                    break;
                case ToshibaCommand::SPECIAL_MODE:
                    ESP_LOGD(TAG, "received special mode: %s", format_hex_pretty(value).c_str());
                    handle_register_special_mode(static_cast<ToshibaSpecialModes>(value));
                    break;
                case ToshibaCommand::IONIZER:
                    ESP_LOGD(TAG, "received ionizer state: %s", format_hex_pretty(value).c_str());
                    handle_register_ionizer(static_cast<ToshibaIonizer>(value));
                    break;
                case ToshibaCommand::POWER_SELECT:
                    ESP_LOGD(TAG, "received power select: %s", format_hex_pretty(value).c_str());
                    handle_register_power_selection(static_cast<ToshibaPowerSelection>(value));
                    break;
                case ToshibaCommand::ROOM_TEMPERATURE:
                    ESP_LOGD(TAG, "received room temperature: %s", format_hex_pretty(value).c_str());
                    handle_register_room_temperature(value);
                    break;
                case ToshibaCommand::OUTDOOR_TEMPERATURE:
                    ESP_LOGD(TAG, "received outdoor temperature: %s", format_hex_pretty(value).c_str());
                    handle_register_outdoor_temperature((int8_t)value);
                    break;
                default:
                    ESP_LOGE(TAG, "received unhandled register message: %s", format_hex_pretty(command).c_str());
                    break;
            }
        } else if (recv_buf_len_ == 22) {
            if (recv_buf_[12] == ToshibaCommand::ODU_STATUS) {
                if (sensor_cdu_td_temp_) sensor_cdu_td_temp_->publish_state(static_cast<int8_t>(recv_buf_[13]));
                if (sensor_cdu_ts_temp_) sensor_cdu_ts_temp_->publish_state(static_cast<int8_t>(recv_buf_[14]));
                if (sensor_cdu_te_temp_) sensor_cdu_te_temp_->publish_state(static_cast<int8_t>(recv_buf_[15]));
                if (sensor_cdu_load_) sensor_cdu_load_->publish_state(static_cast<float_t>(recv_buf_[16]) / 1.7f);
                if (sensor_cdu_iac_) sensor_cdu_iac_->publish_state(static_cast<uint8_t>(recv_buf_[19]));
                ESP_LOGI(TAG,
                    "[REGISTERS_ODU] STATUS: cduTdTemp = %d, cduTsTemp = %d, cduTeTemp = %d, cduLoad = %d, cduIac = %d",
                    sensor_cdu_td_temp_ ? (int32_t)sensor_cdu_td_temp_->state : -999,
                    sensor_cdu_ts_temp_ ? (int32_t)sensor_cdu_ts_temp_->state : -999,
                    sensor_cdu_te_temp_ ? (int32_t)sensor_cdu_te_temp_->state : -999,
                    sensor_cdu_load_ ? (int32_t)sensor_cdu_load_->state : -999,
                    sensor_cdu_iac_ ? (int32_t)sensor_cdu_iac_->state : -999);
            } else if (recv_buf_[12] == ToshibaCommand::IDU_STATUS) {
                if (sensor_fcu_tc_temp_) sensor_fcu_tc_temp_->publish_state(static_cast<int8_t>(recv_buf_[13]));
                if (sensor_fcu_tcj_temp_) sensor_fcu_tcj_temp_->publish_state(static_cast<int8_t>(recv_buf_[14]));
                if (sensor_fcu_fan_rpm_) sensor_fcu_fan_rpm_->publish_state(static_cast<uint8_t>(recv_buf_[15]));
                ESP_LOGI(TAG, "[REGISTERS_IDU] STATUS: fcuTcTemp = %d, fcuTcjTemp = %d, fcuFanRpm = %d",
                         sensor_fcu_tc_temp_ ? (int32_t)sensor_fcu_tc_temp_->state : -999,
                         sensor_fcu_tcj_temp_ ? (int32_t)sensor_fcu_tcj_temp_->state : -999,
                         sensor_fcu_fan_rpm_ ? (int32_t)sensor_fcu_fan_rpm_->state : -999);
            }
        } else if (recv_buf_len_ == 24) {
            if (recv_buf_[14] == ToshibaCommand::ODU_STATUS) {
                if (sensor_cdu_td_temp_) sensor_cdu_td_temp_->publish_state(static_cast<int8_t>(recv_buf_[15]));
                if (sensor_cdu_ts_temp_) sensor_cdu_ts_temp_->publish_state(static_cast<int8_t>(recv_buf_[16]));
                if (sensor_cdu_te_temp_) sensor_cdu_te_temp_->publish_state(static_cast<int8_t>(recv_buf_[17]));
                if (sensor_cdu_load_) sensor_cdu_load_->publish_state(static_cast<float_t>(recv_buf_[18]) / 1.7f);
                if (sensor_cdu_iac_) sensor_cdu_iac_->publish_state(static_cast<uint8_t>(recv_buf_[21]));
                ESP_LOGI(TAG,
                         "[REGISTERS_ODU_REQ] STATUS: cduTdTemp = %d, cduTsTemp = %d, cduTeTemp = %d, cduLoad = %d, cduIac = %d",
                         sensor_cdu_td_temp_ ? (int32_t)sensor_cdu_td_temp_->state : -999,
                         sensor_cdu_ts_temp_ ? (int32_t)sensor_cdu_ts_temp_->state : -999,
                         sensor_cdu_te_temp_ ? (int32_t)sensor_cdu_te_temp_->state : -999,
                         sensor_cdu_load_ ? (int32_t)sensor_cdu_load_->state : -999,
                         sensor_cdu_iac_ ? (int32_t)sensor_cdu_iac_->state : -999);
            } else if (recv_buf_[14] == ToshibaCommand::IDU_STATUS) {
                if (sensor_fcu_tc_temp_) sensor_fcu_tc_temp_->publish_state(static_cast<int8_t>(recv_buf_[15]));
                if (sensor_fcu_tcj_temp_) sensor_fcu_tcj_temp_->publish_state(static_cast<int8_t>(recv_buf_[16]));
                if (sensor_fcu_fan_rpm_) sensor_fcu_fan_rpm_->publish_state(static_cast<uint8_t>(recv_buf_[17]));
                ESP_LOGI(TAG, "[REGISTERS_IDU_REQ] STATUS: fcuTcTemp = %d, fcuTcjTemp = %d, fcuFanRpm = %d",
                         sensor_fcu_tc_temp_ ? (int32_t)sensor_fcu_tc_temp_->state : -999,
                         sensor_fcu_tcj_temp_ ? (int32_t)sensor_fcu_tcj_temp_->state : -999,
                         sensor_fcu_fan_rpm_ ? (int32_t)sensor_fcu_fan_rpm_->state : -999);
            }
        } else {
            ESP_LOGV(TAG, "Received unknown message with length: %d and value %s", recv_buf_len_,
                     format_hex_pretty(recv_buf_, recv_buf_len_).c_str());
            return;
        }
    }

    void process_uart_rx() {
        uint8_t cnt = 0;
        while (serial_->available() > 0 && cnt < 32) {
            if (!serial_->read_byte(&recv_buf_[recv_buf_len_])) {
                break;
            }
            last_recv_millis_ = millis();
            recv_buf_len_++;

            if (recv_buf_len_ > 255) {
                ESP_LOGE(TAG, "rx buffer overflow");
                this->recv_buf_len_ = 0;
            }

            if (recv_buf_len_ >= 7 && recv_buf_[6] + 8 == recv_buf_len_) {  // length + 6 + length byte + checksum
                ESP_LOGD(TAG, "received full message %d bytes", recv_buf_len_);
                handle_message();
                recv_buf_len_ = 0;
            }
            cnt++;
        }

        if (recv_buf_len_ > 0 && millis() - last_recv_millis_ >= 100) {
            ESP_LOGE(TAG, "discarded %d rx bytes due to timeout", recv_buf_len_);
            recv_buf_len_ = 0;
        }
    }

    void request_write_register_(ToshibaCommand command, uint8_t value) {
        std::vector<uint8_t> msg = {0x2,  0x0, 0x3, 0x10, 0x0, 0x0, 0x7, 0x1, 0x30, 0x1, 0x0, 0x2, uint8_t(command),
                                    value};

        msg.push_back(calc_checksum(&msg[0], msg.size()));
        this->send_msg_queue_.push_back(msg);

        ESP_LOGI(TAG, "requesting write register %s with value %s", format_hex_pretty((uint8_t)command).c_str(),
                 format_hex_pretty(value).c_str());
    }

    void request_read_register_(ToshibaCommand command) {
        std::vector<uint8_t> msg = {0x2, 0x0, 0x3, 0x10, 0x0, 0x0, 0x6, 0x1, 0x30, 0x1, 0x0, 0x1, uint8_t(command)};
        msg.push_back(calc_checksum(&msg[0], msg.size()));
        this->send_msg_queue_.push_back(msg);

        ESP_LOGI(TAG, "requesting read register %s", format_hex_pretty((uint8_t)command).c_str());
    }

    void configure_capabilities() {
        // default traits

        if (this->config_settings_.disable_cooling_modes) {
            supported_traits_.set_supported_modes({
                climate::CLIMATE_MODE_OFF,
                climate::CLIMATE_MODE_HEAT,
                climate::CLIMATE_MODE_FAN_ONLY,
            });
        } else {
            supported_traits_.set_supported_modes({
                climate::CLIMATE_MODE_OFF,
                climate::CLIMATE_MODE_COOL,
                climate::CLIMATE_MODE_HEAT,
                climate::CLIMATE_MODE_DRY,
                climate::CLIMATE_MODE_FAN_ONLY,
                climate::CLIMATE_MODE_HEAT_COOL,
            });
        }
        supported_traits_.set_supported_swing_modes({
            climate::CLIMATE_SWING_OFF,
            climate::CLIMATE_SWING_BOTH,
            climate::CLIMATE_SWING_VERTICAL,
            climate::CLIMATE_SWING_HORIZONTAL,
        });

        supported_traits_.add_supported_fan_mode(climate::CLIMATE_FAN_AUTO);
        supported_traits_.add_supported_fan_mode(climate::CLIMATE_FAN_QUIET);
        supported_traits_.add_supported_fan_mode(climate::CLIMATE_FAN_LOW);
        supported_traits_.add_supported_fan_mode(climate::CLIMATE_FAN_MEDIUM);
        supported_traits_.add_supported_fan_mode(climate::CLIMATE_FAN_HIGH);

        // Custom fan modes moved from ClimateTraits to the Climate entity in ESPHome 2026.4.0.
        // The ClimateTraits setter still exists on older versions (removal planned for 2026.11.0).
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 4, 0)
        this->set_supported_custom_fan_modes({CUSTOM_FAN_MODE_LOW_MEDIUM, CUSTOM_FAN_MODE_MEDIUM_HIGH});
#else
        supported_traits_.set_supported_custom_fan_modes({CUSTOM_FAN_MODE_LOW_MEDIUM, CUSTOM_FAN_MODE_MEDIUM_HIGH});
#endif

        // ClimateTraits::set_supports_* was removed in ESPHome 2026.5.0 in favor of feature flags,
        // which were introduced in 2025.11.0. two_point_target_temperature and action both default
        // to off, so only current_temperature needs to be set.
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2025, 11, 0)
        supported_traits_.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
#else
        supported_traits_.set_supports_current_temperature(true);
#endif
        supported_traits_.set_visual_min_temperature(
            (int)std::min(MIN_TEMP_SETPOINT_HEATING, MIN_TEMP_SETPOINT_COOLING));
        supported_traits_.set_visual_max_temperature(MAX_TEMP_SETPOINT);
        supported_traits_.set_visual_current_temperature_step(0.1);
        supported_traits_.set_visual_target_temperature_step(0.5);
    }

    void automatic_eight_degrees_switchover(uint8_t target_temperature) {
        if (this->internal_power_state_ == ToshibaState::STATE_OFF) {
            ESP_LOGE(TAG, "IDU is powered off, ignoring special mode");
            return;
        }
        if (this->mode == climate::CLIMATE_MODE_HEAT) {
            if (this->internal_special_mode_ == ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES &&
                target_temperature >= 17) {
                ESP_LOGE(TAG,
                         "Special mode EIGHT_DEGREES is only required for 5°C-16°C heating, switching to STANDARD");
                this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_STANDARD;
                request_write_register_(ToshibaCommand::SPECIAL_MODE, this->internal_special_mode_);
                updating_from_ac_ = true;
                this->special_mode_select_->publish_state("Standard");
                updating_from_ac_ = false;
            } else if (this->internal_special_mode_ != ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES &&
                       target_temperature < 17) {
                ESP_LOGE(TAG, "Special mode EIGHT_DEGREES is required for 5°C-16°C heating, enabling");
                this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES;
                request_write_register_(ToshibaCommand::SPECIAL_MODE, this->internal_special_mode_);
                updating_from_ac_ = true;
                this->special_mode_select_->publish_state("8 Degrees");
                updating_from_ac_ = false;
            }

        } else {
            if (this->internal_special_mode_ == ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES) {
                ESP_LOGE(TAG, "Special mode EIGHT_DEGREES is only available in heating mode, switching to STANDARD");
                this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_STANDARD;
                request_write_register_(ToshibaCommand::SPECIAL_MODE, this->internal_special_mode_);
                updating_from_ac_ = true;
                this->special_mode_select_->publish_state("Standard");
                updating_from_ac_ = false;
            }
        }
    }

public:
    ToshibaController() {
        configure_capabilities();
    }

    // Setters for dependency injection from ESPHome codegen
    void set_uart(esphome::uart::UARTComponent* serial) { serial_ = serial; }
    void set_temperature_sensor(esphome::sensor::Sensor* sensor) { temperature_sensor_ = sensor; }
    void set_special_mode_select(esphome::select::Select* sel) { special_mode_select_ = sel; }
    void set_swing_mode_select(esphome::select::Select* sel) { swing_mode_select_ = sel; }
    void set_power_select(esphome::select::Select* sel) { power_selection_select_ = sel; }
    void set_smart_thermostat_multiplier(float val) { config_settings_.smart_thermostat_multiplier = val; }
    void set_disable_cooling_modes(bool val) {
        config_settings_.disable_cooling_modes = val;
        configure_capabilities();
    }
    void set_smart_thermostat_runaway_protection(bool val) { config_settings_.smart_thermostat_runaway_protection = val; }

    // Switch setters for codegen (pointer-based wiring from switch.py)
    void set_internal_thermistor_switch_obj(CustomSwitch* sw) {
        switch_internal_thermistor_ = sw;
        sw->set_controller(this);
        sw->set_type(SWITCH_INTERNAL_THERMISTOR);
    }
    void set_ionizer_switch_obj(CustomSwitch* sw) {
        switch_ionizer_ = sw;
        sw->set_controller(this);
        sw->set_type(SWITCH_IONIZER);
    }

    float get_setup_priority() const override {
        return esphome::setup_priority::BUS;
    }

    ConfigSettings& config_settings() {
        return config_settings_;
    }

    void setup() override {
        auto restore = this->restore_state_();
        if (restore.has_value()) {
            restore->apply(this);
        } else {
            this->mode = climate::CLIMATE_MODE_OFF;
            this->target_temperature = 20;
            this->set_fan_mode_(climate::CLIMATE_FAN_MEDIUM);
            this->swing_mode = climate::CLIMATE_SWING_OFF;
            this->publish_state();
        }

        if (switch_internal_thermistor_) {
            switch_internal_thermistor_->restore_and_set_mode(esphome::switch_::SWITCH_RESTORE_DEFAULT_OFF);
        }

        ESP_LOGD(TAG, "setup before recv");
        while (serial_->available() > 0) {
            uint8_t b;
            serial_->read_byte(&b);
            ESP_LOGD(TAG, "read byte %s", format_hex_pretty(b).c_str());
        }

        ESP_LOGD(TAG, "setup before handshake");
        set_timeout("send_handshake", 10000, [this]() {
            ESP_LOGD(TAG, "sending handshake");
            for (const auto& msg : IDU_HANDSHAKE) {
                this->send_msg_queue_.push_back(msg);
            }
            set_timeout("send_post_handshake", 3000, [this]() {
                ESP_LOGD(TAG, "sending post handshake");

                for (const auto& msg : IDU_POST_HANDSHAKE) {
                    this->send_msg_queue_.push_back(msg);
                }

                set_timeout("request_initial_data", 3000, [this]() {
                    request_registers_(true);
                    is_initialized_ = true;
                });
            });
        });
    }

    climate::ClimateTraits traits() override {
        return supported_traits_;
    }

    ///////////////////////////////////////////
    // CLIMATE ENTITY CONTROL HANDLING
    ///////////////////////////////////////////
    void control_handle_mode(const climate::ClimateCall& call) {
        auto mode_val = *call.get_mode();
        this->mode = mode_val;
        if (mode_val == climate::CLIMATE_MODE_OFF) {
            this->request_write_register_(ToshibaCommand::POWER_STATE, ToshibaState::STATE_OFF);
            this->internal_power_state_ = ToshibaState::STATE_OFF;
            return;
        } else {
            if (internal_power_state_ == ToshibaState::STATE_OFF) {
                this->request_write_register_(ToshibaCommand::POWER_STATE, ToshibaState::STATE_ON);
                this->internal_power_state_ = ToshibaState::STATE_ON;
            }
        }

        switch (mode_val) {
            case climate::CLIMATE_MODE_COOL:
                this->request_write_register_(ToshibaCommand::MODE, ToshibaMode::MODE_COOL);
                break;
            case climate::CLIMATE_MODE_HEAT:
                this->request_write_register_(ToshibaCommand::MODE, ToshibaMode::MODE_HEAT);
                break;
            case climate::CLIMATE_MODE_DRY:
                this->request_write_register_(ToshibaCommand::MODE, ToshibaMode::MODE_DRY);
                break;
            case climate::CLIMATE_MODE_FAN_ONLY:
                this->request_write_register_(ToshibaCommand::MODE, ToshibaMode::MODE_FAN_ONLY);
                break;
            case climate::CLIMATE_MODE_HEAT_COOL:
                this->request_write_register_(ToshibaCommand::MODE, ToshibaMode::MODE_HEAT_COOL);
                break;
            default:
                ESP_LOGE(TAG, "received unknown mode: %d", (int)mode_val);
                break;
        }
    }

    void control_handle_target_temperature(const climate::ClimateCall& call) {
        this->target_temperature = std::round(*call.get_target_temperature() * 2.0) / 2.0;  // 0.5 deg precision

        if (this->target_temperature < std::min(MIN_TEMP_SETPOINT_HEATING, MIN_TEMP_SETPOINT_COOLING)) {
            this->target_temperature = std::min(MIN_TEMP_SETPOINT_HEATING, MIN_TEMP_SETPOINT_COOLING);
        } else if (this->target_temperature > MAX_TEMP_SETPOINT) {
            this->target_temperature = MAX_TEMP_SETPOINT;
        }

        if (this->internal_power_state_ == ToshibaState::STATE_OFF) {
            ESP_LOGE(TAG, "IDU is powered off, ignoring target temperature control command");
            return;
        }

        if (switch_internal_thermistor_ && switch_internal_thermistor_->state) {
            automatic_eight_degrees_switchover(this->target_temperature);
            this->internal_target_temperature_ = this->target_temperature;
            if (this->mode != climate::CLIMATE_MODE_HEAT) {
                this->request_write_register_(
                    ToshibaCommand::TARGET_TEMPERATURE,
                    std::max(this->internal_target_temperature_, (uint8_t)MIN_TEMP_SETPOINT_COOLING));
            } else if (this->internal_target_temperature_ < 17) {
                this->request_write_register_(ToshibaCommand::TARGET_TEMPERATURE,
                                              this->internal_target_temperature_ + 16);
            } else {
                this->request_write_register_(ToshibaCommand::TARGET_TEMPERATURE, this->internal_target_temperature_);
            }
            if (sensor_fcu_setpoint_temp_) sensor_fcu_setpoint_temp_->publish_state(this->internal_target_temperature_);
        } else {
            ESP_LOGD(TAG,
                     "internal thermistor is disabled, idu target temperature is updated by smart_thermostat_control");
        }
    }

    void control_handle_fan_mode(const climate::ClimateCall& call) {
        if (this->internal_power_state_ == ToshibaState::STATE_OFF) {
            ESP_LOGE(TAG, "IDU is powered off, ignoring fan mode control command");
            return;
        }

        auto fan_mode_val = *call.get_fan_mode();
        this->set_fan_mode_(fan_mode_val);

        if (fan_mode_val == climate::CLIMATE_FAN_AUTO) {
            this->request_write_register_(ToshibaCommand::FAN_MODE, ToshibaFanMode::FAN_AUTO);
            return;
        }
        if (fan_mode_val == climate::CLIMATE_FAN_QUIET) {
            this->request_write_register_(ToshibaCommand::FAN_MODE, ToshibaFanMode::FAN_QUIET);
            return;
        }
        if (fan_mode_val == climate::CLIMATE_FAN_LOW) {
            this->request_write_register_(ToshibaCommand::FAN_MODE, ToshibaFanMode::FAN_LOW);
            return;
        }
        if (fan_mode_val == climate::CLIMATE_FAN_MEDIUM) {
            this->request_write_register_(ToshibaCommand::FAN_MODE, ToshibaFanMode::FAN_MEDIUM);
            return;
        }
        if (fan_mode_val == climate::CLIMATE_FAN_HIGH) {
            this->request_write_register_(ToshibaCommand::FAN_MODE, ToshibaFanMode::FAN_HIGH);
            return;
        }
        ESP_LOGE(TAG, "received unknown fan mode: %d", (int)fan_mode_val);
    }

    void control_handle_custom_fan_mode(const climate::ClimateCall& call) {
        if (this->internal_power_state_ == ToshibaState::STATE_OFF) {
            ESP_LOGE(TAG, "IDU is powered off, ignoring custom fan mode control command");
            return;
        }

        auto cfm = call.get_custom_fan_mode();
        this->set_custom_fan_mode_(cfm.c_str());

        if (cfm == CUSTOM_FAN_MODE_LOW_MEDIUM) {
            this->request_write_register_(ToshibaCommand::FAN_MODE, ToshibaFanMode::FAN_LOW_MEDIUM);
            return;
        }
        if (cfm == CUSTOM_FAN_MODE_MEDIUM_HIGH) {
            this->request_write_register_(ToshibaCommand::FAN_MODE, ToshibaFanMode::FAN_MEDIUM_HIGH);
            return;
        }
        ESP_LOGE(TAG, "received unknown custom fan mode: %s", cfm.c_str());
    }

    void control_handle_swing_mode(const climate::ClimateCall& call) {
        if (this->internal_power_state_ == ToshibaState::STATE_OFF) {
            ESP_LOGE(TAG, "IDU is powered off, ignoring swing mode control command");
            return;
        }

        auto swing_mode_val = *call.get_swing_mode();
        this->swing_mode = swing_mode_val;

        if (swing_mode_val == climate::CLIMATE_SWING_OFF) {
            this->internal_swing_mode_ = ToshibaSwingMode::SWING_MODE_OFF;
            this->request_write_register_(ToshibaCommand::SWING_MODE, ToshibaSwingMode::SWING_MODE_OFF);
            return;
        }
        if (swing_mode_val == climate::CLIMATE_SWING_VERTICAL) {
            this->internal_swing_mode_ = ToshibaSwingMode::SWING_MODE_SWING_VERTICAL;
            this->request_write_register_(ToshibaCommand::SWING_MODE, ToshibaSwingMode::SWING_MODE_SWING_VERTICAL);
            return;
        }
        if (swing_mode_val == climate::CLIMATE_SWING_HORIZONTAL) {
            this->internal_swing_mode_ = ToshibaSwingMode::SWING_MODE_SWING_HORIZONTAL;
            this->request_write_register_(ToshibaCommand::SWING_MODE, ToshibaSwingMode::SWING_MODE_SWING_HORIZONTAL);
            return;
        }
        if (swing_mode_val == climate::CLIMATE_SWING_BOTH) {
            this->internal_swing_mode_ = ToshibaSwingMode::SWING_MODE_SWING_VERTICAL_AND_HORIZONTAL;
            this->request_write_register_(ToshibaCommand::SWING_MODE,
                                          ToshibaSwingMode::SWING_MODE_SWING_VERTICAL_AND_HORIZONTAL);
            return;
        }
        ESP_LOGE(TAG, "received unknown swing mode: %d", (int)swing_mode_val);
    }

    // Process changes from HA.
    void control(const climate::ClimateCall& call) override {
        ESP_LOGD(TAG, "climate entity control() called");

        if (!is_initialized_) {
            ESP_LOGE(TAG, "not initialized yet, ignoring control command");
            return;
        }

        if (call.get_mode().has_value()) {
            this->control_handle_mode(call);
        }
        if (call.get_target_temperature().has_value()) {
            this->control_handle_target_temperature(call);
        }
        if (call.get_fan_mode().has_value()) {
            this->control_handle_fan_mode(call);
        }
        if (!call.get_custom_fan_mode().empty()) {
            this->control_handle_custom_fan_mode(call);
        }
        if (call.get_swing_mode().has_value()) {
            this->control_handle_swing_mode(call);
        }
        this->publish_state();
    }

    ///////////////////////////////////////////
    // CUSTOM ENTITY SELECTS
    ///////////////////////////////////////////
    void set_power_select(int power) {
        if (!is_initialized_) {
            ESP_LOGE(TAG, "not initialized yet, ignoring power select command");
            return;
        }

        ToshibaPowerSelection new_selection;
        switch (power) {
            case 0: new_selection = ToshibaPowerSelection::POWER_50;  break;
            case 1: new_selection = ToshibaPowerSelection::POWER_75;  break;
            case 2: new_selection = ToshibaPowerSelection::POWER_100; break;
            default:
                ESP_LOGE(TAG, "Unexpected power selection: %d", power);
                return;
        }

        // Skip write if value unchanged (prevents write-back loop when AC
        // sends spontaneous power select push notifications)
        if (new_selection == this->internal_power_selection_) {
            ESP_LOGD(TAG, "power select unchanged, skipping write");
            return;
        }

        this->internal_power_selection_ = new_selection;
        this->request_write_register_(ToshibaCommand::POWER_SELECT, this->internal_power_selection_);
    }

    void set_swing_mode_select(int mode) {
        if (!is_initialized_) {
            ESP_LOGE(TAG, "not initialized yet, ignoring swing mode select command");
            return;
        }

        // implement the index function as switch
        switch (mode) {
            case 0:
                this->internal_swing_mode_ = ToshibaSwingMode::SWING_MODE_OFF;
                this->swing_mode = climate::CLIMATE_SWING_OFF;
                break;
            case 1:
                this->internal_swing_mode_ = ToshibaSwingMode::SWING_MODE_SWING_VERTICAL;
                this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
                break;
            case 2:
                this->internal_swing_mode_ = ToshibaSwingMode::SWING_MODE_SWING_HORIZONTAL;
                this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
                break;
            case 3:
                this->internal_swing_mode_ = ToshibaSwingMode::SWING_MODE_SWING_VERTICAL_AND_HORIZONTAL;
                this->swing_mode = climate::CLIMATE_SWING_BOTH;
                break;
            case 4:
                this->internal_swing_mode_ = ToshibaSwingMode::SWING_MODE_FIXED_1;
                this->swing_mode = climate::CLIMATE_SWING_OFF;
                break;
            case 5:
                this->internal_swing_mode_ = ToshibaSwingMode::SWING_MODE_FIXED_2;
                this->swing_mode = climate::CLIMATE_SWING_OFF;
                break;
            case 6:
                this->internal_swing_mode_ = ToshibaSwingMode::SWING_MODE_FIXED_3;
                this->swing_mode = climate::CLIMATE_SWING_OFF;
                break;
            case 7:
                this->internal_swing_mode_ = ToshibaSwingMode::SWING_MODE_FIXED_4;
                this->swing_mode = climate::CLIMATE_SWING_OFF;
                break;
            case 8:
                this->internal_swing_mode_ = ToshibaSwingMode::SWING_MODE_FIXED_5;
                this->swing_mode = climate::CLIMATE_SWING_OFF;
                break;
            default:
                ESP_LOGE(TAG, "Unexpected swing mode: %d", mode);
                return;
        }

        this->request_write_register_(ToshibaCommand::SWING_MODE, this->internal_swing_mode_);
        this->publish_state();
    }

    void set_special_mode_select(int mode) {
        if (updating_from_ac_) return;
        if (!is_initialized_) {
            ESP_LOGE(TAG, "not initialized yet, ignoring special mode select command");
            return;
        }

        ToshibaSpecialModes old_special_mode = this->internal_special_mode_;

        // Options: Standard(0), High Power(1), ECO(2), 8 Degrees(3), Sleep Care(4), Floor(5), Comfort(6)
        switch (mode) {
            case 0:
                this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_STANDARD;
                break;
            case 1:
                this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_HIGH_POWER;
                break;
            case 2:
                this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_ECO;
                break;
            case 3:
                this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES;
                break;
            case 4:
                this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_SLEEP_CARE;
                break;
            case 5:
                this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_FLOOR;
                break;
            case 6:
                this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_COMFORT;
                break;
            default:
                ESP_LOGE(TAG, "Unexpected special mode: %d", mode);
                return;
        }
        // Reset silent and fireplace selects when a special mode is chosen
        // Use flag to suppress on_value callbacks and prevent infinite recursion
        updating_from_ac_ = true;
        if (silent_mode_select_) silent_mode_select_->publish_state("Off");
        if (fireplace_select_) fireplace_select_->publish_state("Off");
        updating_from_ac_ = false;

        if (this->mode != climate::CLIMATE_MODE_HEAT &&
            this->internal_special_mode_ == ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES) {
            ESP_LOGE(TAG, "Special mode EIGHT_DEGREES is only available in heating mode, discarding");
            this->internal_special_mode_ = old_special_mode;
            return;
        }

        if (old_special_mode == ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES &&
            this->internal_special_mode_ != ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES) {
            // we are leaving 8 degrees mode, so we need to set the target temperature to the closest supported value
            if (this->internal_target_temperature_ < 17) {
                this->internal_target_temperature_ = 17;
                this->request_write_register_(ToshibaCommand::TARGET_TEMPERATURE, this->internal_target_temperature_);
                if (sensor_fcu_setpoint_temp_) sensor_fcu_setpoint_temp_->publish_state(this->internal_target_temperature_);

                if (switch_internal_thermistor_ && switch_internal_thermistor_->state) {
                    this->target_temperature = this->internal_target_temperature_;
                    this->publish_state();
                }
            }
        }

        if (old_special_mode != ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES &&
            this->internal_special_mode_ == ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES) {
            // we are entering 8 degrees mode, so we need to set the target temperature to the closest supported value
            if (this->internal_target_temperature_ > 16) {
                this->internal_target_temperature_ = 16;
                this->request_write_register_(ToshibaCommand::TARGET_TEMPERATURE,
                                              this->internal_target_temperature_ + 16);
                if (sensor_fcu_setpoint_temp_) sensor_fcu_setpoint_temp_->publish_state(this->internal_target_temperature_);

                if (switch_internal_thermistor_ && switch_internal_thermistor_->state) {
                    this->target_temperature = this->internal_target_temperature_;
                    this->publish_state();
                }
            }
        }

        this->request_write_register_(ToshibaCommand::SPECIAL_MODE, this->internal_special_mode_);
    }

    void set_silent_mode_select(int mode) {
        if (updating_from_ac_) return;
        // Options: Off(0), Silent 1(1), Silent 2(2)
        if (!is_initialized_) {
            ESP_LOGE(TAG, "not initialized yet, ignoring silent mode select command");
            return;
        }
        ToshibaSpecialModes old_special_mode = this->internal_special_mode_;
        switch (mode) {
            case 1: this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_SILENT_1; break;
            case 2: this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_SILENT_2; break;
            default: this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_STANDARD; break;
        }
        // Reset special mode and fireplace selects
        // Use flag to suppress on_value callbacks and prevent infinite recursion
        updating_from_ac_ = true;
        this->special_mode_select_->publish_state("Standard");
        if (fireplace_select_) fireplace_select_->publish_state("Off");
        updating_from_ac_ = false;
        // Handle 8 degrees exit if needed
        if (old_special_mode == ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES &&
            this->internal_target_temperature_ < 17) {
            this->internal_target_temperature_ = 17;
            this->request_write_register_(ToshibaCommand::TARGET_TEMPERATURE, this->internal_target_temperature_);
            if (sensor_fcu_setpoint_temp_) sensor_fcu_setpoint_temp_->publish_state(this->internal_target_temperature_);
        }
        this->request_write_register_(ToshibaCommand::SPECIAL_MODE, this->internal_special_mode_);
    }

    void set_fireplace_select(int mode) {
        if (updating_from_ac_) return;
        // Options: Off(0), Fireplace 1(1), Fireplace 2(2)
        if (!is_initialized_) {
            ESP_LOGE(TAG, "not initialized yet, ignoring fireplace select command");
            return;
        }
        ToshibaSpecialModes old_special_mode = this->internal_special_mode_;
        switch (mode) {
            case 1: this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_FIREPLACE_1; break;
            case 2: this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_FIREPLACE_2; break;
            default: this->internal_special_mode_ = ToshibaSpecialModes::SPECIAL_MODE_STANDARD; break;
        }
        // Reset special mode and silent selects
        // Use flag to suppress on_value callbacks and prevent infinite recursion
        updating_from_ac_ = true;
        this->special_mode_select_->publish_state("Standard");
        if (silent_mode_select_) silent_mode_select_->publish_state("Off");
        updating_from_ac_ = false;
        // Handle 8 degrees exit if needed
        if (old_special_mode == ToshibaSpecialModes::SPECIAL_MODE_EIGHT_DEGREES &&
            this->internal_target_temperature_ < 17) {
            this->internal_target_temperature_ = 17;
            this->request_write_register_(ToshibaCommand::TARGET_TEMPERATURE, this->internal_target_temperature_);
            if (sensor_fcu_setpoint_temp_) sensor_fcu_setpoint_temp_->publish_state(this->internal_target_temperature_);
        }
        this->request_write_register_(ToshibaCommand::SPECIAL_MODE, this->internal_special_mode_);
    }

    void set_silent_mode_select_ptr(select::Select* s) { silent_mode_select_ = s; }
    void set_fireplace_select_ptr(select::Select* s) { fireplace_select_ = s; }

    ///////////////////////////////////////////
    // SENSOR SETTERS (called from climate.py codegen)
    ///////////////////////////////////////////
    void set_outdoor_temperature_sensor(sensor::Sensor* s) { sensor_outdoor_temperature_ = s; }
    void set_fcu_air_temp_sensor(sensor::Sensor* s) { sensor_fcu_air_temp_ = s; }
    void set_fcu_setpoint_sensor(sensor::Sensor* s) { sensor_fcu_setpoint_temp_ = s; }
    void set_fcu_tc_temp_sensor(sensor::Sensor* s) { sensor_fcu_tc_temp_ = s; }
    void set_fcu_tcj_temp_sensor(sensor::Sensor* s) { sensor_fcu_tcj_temp_ = s; }
    void set_fcu_fan_rpm_sensor(sensor::Sensor* s) { sensor_fcu_fan_rpm_ = s; }
    void set_cdu_td_temp_sensor(sensor::Sensor* s) { sensor_cdu_td_temp_ = s; }
    void set_cdu_ts_temp_sensor(sensor::Sensor* s) { sensor_cdu_ts_temp_ = s; }
    void set_cdu_te_temp_sensor(sensor::Sensor* s) { sensor_cdu_te_temp_ = s; }
    void set_cdu_load_sensor(sensor::Sensor* s) { sensor_cdu_load_ = s; }
    void set_cdu_iac_sensor(sensor::Sensor* s) { sensor_cdu_iac_ = s; }

    ///////////////////////////////////////////
    // SWITCHES
    ///////////////////////////////////////////
    void set_internal_thermistor_switch(bool state) {
        ESP_LOGD(TAG, "set_internal_thermistor_switch %d", state);
        if (switch_internal_thermistor_) switch_internal_thermistor_->publish_state(state);
    }

    void set_ionizer_switch(bool state) {
        ESP_LOGD(TAG, "set_ionizer_switch %d", state);
        if (!is_initialized_) {
            ESP_LOGE(TAG, "not initialized yet, ignoring ionizer switch command");
            return;
        }

        if (state) {
            this->request_write_register_(ToshibaCommand::IONIZER, ToshibaIonizer::IONIZER_ON);
        } else {
            this->request_write_register_(ToshibaCommand::IONIZER, ToshibaIonizer::IONIZER_OFF);
        }
    }

    void set_wifi_led_switch(bool state) {
        ESP_LOGD(TAG, "set_wifi_led_switch %d", state);
        if (!is_initialized_) {
            ESP_LOGE(TAG, "not initialized yet, ignoring wifi led command");
            return;
        }
        // Write to both variants — the AC accepts the correct one, ignores the other
        if (state) {
            this->request_write_register_(ToshibaCommand::WIFILED1, 0x05);
            this->request_write_register_(ToshibaCommand::WIFILED2, 0x00);
        } else {
            this->request_write_register_(ToshibaCommand::WIFILED1, 0x00);
            this->request_write_register_(ToshibaCommand::WIFILED2, 0x80);
        }
    }

private:

    void request_registers_(bool full) {
        this->request_read_register_(ToshibaCommand::ROOM_TEMPERATURE);
        this->request_read_register_(ToshibaCommand::OUTDOOR_TEMPERATURE);

        // these registers can be changed externally (IR remote) and could conflict with our internal state
        this->request_read_register_(ToshibaCommand::SPECIAL_MODE);
        this->request_read_register_(ToshibaCommand::TARGET_TEMPERATURE);

        if (full) {
            this->request_read_register_(ToshibaCommand::POWER_STATE);
            this->request_read_register_(ToshibaCommand::MODE);
            this->request_read_register_(ToshibaCommand::FAN_MODE);
            this->request_read_register_(ToshibaCommand::SWING_MODE);
            this->request_read_register_(ToshibaCommand::IONIZER);
            this->request_read_register_(ToshibaCommand::POWER_SELECT);
            this->request_read_register_(ToshibaCommand::ODU_STATUS);
            this->request_read_register_(ToshibaCommand::IDU_STATUS);
        }
    }

    std::deque<std::pair<double, long>> offset_history_;  // <error, time>
    long last_fcu_fan_off_millis_ = 0;
    int thermal_runaway_fix = 0;
    int8_t thermostat_rounding_mode = 0;

    void smart_thermostat_control() {
        if (!is_initialized_) {
            return;
        }
        if (millis() - last_external_temperature_sensor_control_millis_ < 10000) {
            return;
        }

        if (millis() - this->last_partial_register_request_millis_ < 2500) {  // after requesting the current IDU target_temp and special_mode, wait for 4000 ms before writing to the registers
            return;
        }

        last_external_temperature_sensor_control_millis_ = millis();

        if (switch_internal_thermistor_ && switch_internal_thermistor_->state) {
            return;
        }

        double room_temp = 20;

        if (temperature_sensor_ != nullptr) {
            room_temp = temperature_sensor_->get_state();
        }
        if (std::isnan(room_temp) || room_temp == 0) {
            room_temp = internal_idu_room_temperature_;
  if (temperature_sensor_ != nullptr && !std::isnan(temperature_sensor_->get_state())) {
    ESP_LOGI(TAG, "[THERMOSTAT] Using external temperature sensor: %.2f", temperature_sensor_->get_state());
  } else {
    ESP_LOGI(TAG, "[THERMOSTAT] Using internal IDU room temperature: %d", internal_idu_room_temperature_);
  }
        }

        if (room_temp < 0) {
            room_temp = 0;
        }
        if (room_temp > 35) {
            room_temp = 35;
        }

        if (this->mode != climate::CLIMATE_MODE_HEAT && this->mode != climate::CLIMATE_MODE_COOL &&
            this->mode != climate::CLIMATE_MODE_HEAT_COOL) {
            this->current_temperature = room_temp;
            this->publish_state();
            return;
        }

        if (!sensor_fcu_fan_rpm_ || sensor_fcu_fan_rpm_->state <= 0) {
            last_fcu_fan_off_millis_ = millis();
        }

        // if the fan is running (for at least one minute), add the current offset to the offset history
        if (sensor_fcu_fan_rpm_ && sensor_fcu_fan_rpm_->state > 0 && millis() - last_fcu_fan_off_millis_ > 60000) {
            offset_history_.emplace_back((double)internal_idu_room_temperature_ - room_temp, millis());
        }

        // delete elements older than 15 minutes but only if at least 10 are left in the offset_history
        long current_time = millis();
        while (offset_history_.size() > 10 &&
               current_time - offset_history_.front().second > 900000) {  // 900000 millis = 15 minutes
            offset_history_.pop_front();
        }

        // select the median value of offset history but keep the elements in offset_history
        double median_error = 0;
        double average_error = 0;
        if (!offset_history_.empty()) {
            std::vector<double> errors;
            errors.reserve(offset_history_.size());

            for (const auto& item : offset_history_) {
                errors.push_back(item.first);  // add only the error of each pair
                average_error += item.first;
            }

            average_error /= offset_history_.size();
            size_t n = errors.size() / 2;
            std::nth_element(errors.begin(), errors.begin() + n, errors.end());
            if (errors.size() % 2 == 0) {
                std::nth_element(errors.begin(), errors.begin() + n - 1, errors.begin() + n);
                median_error = (errors[n - 1] + errors[n]) / 2.0;
            } else {
                median_error = errors[n];
            }
        }

        // calculate target_error and target_setpoint
        double target_error = this->target_temperature - room_temp;
        double target_setpoint =
            this->target_temperature + median_error + target_error * this->config_settings_.smart_thermostat_multiplier;

        // occasionally, the devices suffer from thermal runaway. it will not perform the requested operation
        // even if the error is significant and the setpoint is adjusted. 
        // below we fix this by setting a plausible, but significant change in target temperature.
        // this can increase compressor cycles, but keeps the error in check.
        if (this->config_settings_.smart_thermostat_runaway_protection) {
            if (target_error > std::max(0.25, 1.0/this->config_settings_.smart_thermostat_multiplier)) {
                thermal_runaway_fix = 1;
            }
            else if (target_error < -std::max(0.25, 1.0/this->config_settings_.smart_thermostat_multiplier)) {
                thermal_runaway_fix = -1;
            }
            else if (std::abs(target_error) < 0.15) {
                thermal_runaway_fix = 0;
            }

            if (thermal_runaway_fix == 1) {
                target_setpoint = std::max((double)this->target_temperature, target_setpoint);
                target_setpoint = std::max((double)internal_idu_room_temperature_, target_setpoint);
                target_setpoint = std::max((double)this->target_temperature+median_error, target_setpoint);
                target_setpoint += 3.0;
            } else if (thermal_runaway_fix == -1) {
                target_setpoint = std::min((double)this->target_temperature, target_setpoint);
                target_setpoint = std::min((double)internal_idu_room_temperature_, target_setpoint);
                target_setpoint = std::min((double)this->target_temperature+median_error, target_setpoint);
                target_setpoint -= 3.0;

            }
        }

        // to account for the low 1°C precision of the device, we need to either ceil or floor the target.
        // we switch only at extrema which ideally leads to a slow, but constant osciallation around the target
        if (target_error > 0.2) {
            thermostat_rounding_mode = 1;
        } else if (target_error < -0.2) {
            thermostat_rounding_mode = -1;
        }
        uint8_t target_setpoint_int = std::floor(std::min(255.0, std::max(0.0, target_setpoint)));

        if (thermostat_rounding_mode == 1) {
            target_setpoint_int = std::ceil(std::min(255.0, std::max(0.0, target_setpoint)));
        }


        uint8_t min_setpoint =
            this->mode == climate::CLIMATE_MODE_HEAT ? MIN_TEMP_SETPOINT_HEATING : MIN_TEMP_SETPOINT_COOLING;

        uint8_t max_setpoint = MAX_TEMP_SETPOINT;

        target_setpoint_int = std::max(min_setpoint, std::min(max_setpoint, target_setpoint_int));

        // update the internal target temperature if the rounded setpoint is different
        if (target_setpoint_int != this->internal_target_temperature_) {
            this->internal_target_temperature_ = target_setpoint_int;
            automatic_eight_degrees_switchover(this->internal_target_temperature_);

            if (this->internal_target_temperature_ < 17) {
                this->request_write_register_(ToshibaCommand::TARGET_TEMPERATURE,
                                              this->internal_target_temperature_ + 16);
            } else {
                this->request_write_register_(ToshibaCommand::TARGET_TEMPERATURE, this->internal_target_temperature_);
            }
            if (sensor_fcu_setpoint_temp_) sensor_fcu_setpoint_temp_->publish_state(this->internal_target_temperature_);
            ESP_LOGD(TAG,
                     "smart_thermostat: set internal_target_temperature_ for target %.2f (current: %.2f) to %d (raw: "
                     "%.2f) (fcuAirTemp: %.2f) with median_error %.2f (avg_error: %.2f) and thermal_runaway_fix %d",
                     this->target_temperature, room_temp, target_setpoint_int, target_setpoint,
                     sensor_fcu_air_temp_ ? sensor_fcu_air_temp_->state : NAN, median_error, average_error, thermal_runaway_fix);
        } else {
            ESP_LOGD(TAG,
                     "smart_thermostat: set internal_target_temperature_ for target %.2f (current: %.2f) to %d (raw: "
                     "%.2f) (fcuAirTemp: %.2f) with median_error %.2f (avg_error: %.2f) and thermal_runaway_fix %d [no change]",
                     this->target_temperature, room_temp, target_setpoint_int, target_setpoint,
                     sensor_fcu_air_temp_ ? sensor_fcu_air_temp_->state : NAN, median_error, average_error, thermal_runaway_fix);
        }

        this->current_temperature = room_temp;
        this->publish_state();
    }

    void loop() {
        if (loop_cnt_ % 1000 == 0) {
            ESP_LOGD(TAG, "loop %d", loop_cnt_);
        }
        loop_cnt_++;

        process_uart_rx();
        process_uart_tx();

        smart_thermostat_control();  // will continously monitor but only apply changes if "internal thermostat" is
                                     // disabled

        if (is_initialized_ && millis() > 30000 && millis()-last_external_temperature_sensor_control_millis_ > 2500) { // wait for 2.5s after we potentially wrote to the target temperature / special mode registers
            if (millis() - last_partial_register_request_millis_ > 5000) {
                ESP_LOGD(TAG, "requesting partial registers");
                last_partial_register_request_millis_ = millis();
                request_registers_(false);
            } else if (millis() - last_full_register_request_millis_ > 60000) {
                ESP_LOGD(TAG, "requesting full registers");
                last_full_register_request_millis_ = millis();
                request_registers_(true);
            }
        }
    }
};

inline void CustomSwitch::write_state(bool state) {
    publish_state(state);
    if (controller_) {
        switch (type_) {
            case SWITCH_INTERNAL_THERMISTOR:
                controller_->set_internal_thermistor_switch(state);
                break;
            case SWITCH_IONIZER:
                controller_->set_ionizer_switch(state);
                break;
        }
    }
}

}  // namespace esphome
