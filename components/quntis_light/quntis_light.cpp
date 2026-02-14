#include "quntis_light.h"

#include <SPI.h>

#include <algorithm>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
    namespace quntis_light {

        static const char* const TAG = "quntis_light";

        void QuntisLight::setup() {
            ESP_LOGI(TAG, "Setting up Quntis Light Output...");

            // Configure SPI pins via Arduino API before RF24 init
            SPI.begin(spi_clk_pin_, spi_miso_pin_, spi_mosi_pin_, -1);

            // Configure the RF controller
            controller_.set_pins(ce_pin_, cs_pin_);

            if (!controller_.begin()) {
                ESP_LOGE(TAG, "Failed to initialize RF24 transceiver!");
                this->mark_failed();
                return;
            }

            ESP_LOGI(TAG, "Quntis Light Output ready");
        }

        void QuntisLight::dump_config() {
            const uint8_t* addr = controller_.get_address();
            const uint8_t* pl = controller_.get_payload();

            ESP_LOGCONFIG(TAG, "Quntis Light Output:");
            ESP_LOGCONFIG(TAG, "  CE Pin: %d", ce_pin_);
            ESP_LOGCONFIG(TAG, "  CS Pin: %d", cs_pin_);
            ESP_LOGCONFIG(TAG, "  SPI CLK: %d, MOSI: %d, MISO: %d", spi_clk_pin_, spi_mosi_pin_, spi_miso_pin_);
            ESP_LOGCONFIG(TAG, "  Device Address: %02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2], addr[3], addr[4]);
            ESP_LOGCONFIG(TAG, "  Device Payload: %02X:%02X:%02X:%02X", pl[0], pl[1], pl[2], pl[3]);
            ESP_LOGCONFIG(TAG, "  Brightness Steps: %d", brightness_steps_);
            ESP_LOGCONFIG(TAG, "  Color Temp Steps: %d", color_temp_steps_);
            ESP_LOGCONFIG(TAG, "  Color Temp Range: %.0f - %.0f mireds", min_mireds_, max_mireds_);
            ESP_LOGCONFIG(TAG, "  Step Delay: %d ms", step_delay_ms_);
        }

        light::LightTraits QuntisLight::get_traits() {
            auto traits = light::LightTraits();
            traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
            traits.set_min_mireds(min_mireds_);
            traits.set_max_mireds(max_mireds_);
            return traits;
        }

        void QuntisLight::write_state(light::LightState* state) {
            light_state_ = state;

            bool is_on = state->current_values.is_on();
            float brightness;
            state->current_values_as_brightness(&brightness);
            float color_temp = state->current_values.get_color_temperature();

            int target_brightness = std::min((int)(brightness * brightness_steps_), brightness_steps_);
            int target_color = std::min(mireds_to_percent_(color_temp) * color_temp_steps_ / 100, color_temp_steps_);

            ESP_LOGD(TAG, "write_state: on=%s brightness=%.2f (step %d) color_temp=%.0f mireds (step %d)",
                     ONOFF(is_on), brightness, target_brightness, color_temp, target_color);

            // On first write after boot, sync internal state to ESPHome's stored values (without sending RF commands)
            if (first_write_) {
                first_write_ = false;
                current_power_ = is_on;
                current_brightness_step_ = target_brightness;
                current_color_step_ = target_color;
                ESP_LOGI(TAG, "Initial state sync (no RF): on=%s brightness_step=%d color_step=%d",
                         ONOFF(is_on), target_brightness, target_color);
                return;
            }

            // Handle power change
            if (is_on != current_power_) {
                target_power_ = is_on;
                has_pending_power_ = true;
            }

            // Color and brightness
            if (is_on) {
                queue_target_(target_brightness, target_brightness_step_, has_pending_brightness_,
                              current_brightness_step_, "brightness");
                queue_target_(target_color, target_color_step_, has_pending_color_,
                              current_color_step_, "color");
            }

            if (op_state_ == IDLE) {
                process_state_machine_();
            }
        }

        //
        // State machine for ESPHome to handle multi-step transitions
        //
        void QuntisLight::loop() {
            if (needs_state_publish_) {
                needs_state_publish_ = false;
                publish_current_state_();
            }

            uint32_t now = millis();
            if (op_state_ == IDLE || now - last_step_time_ < step_delay_ms_) {
                return;
            }

            last_step_time_ = now;
            bool done = false;

            switch (op_state_) {
                case TOGGLING_POWER:
                    current_power_ = target_power_;
                    has_pending_power_ = false;
                    ESP_LOGI(TAG, "Power toggled to %s", ONOFF(current_power_));
                    done = true;
                    break;

                case SENDING_BRIGHTNESS:
                    done = send_step_(true);
                    break;

                case SENDING_COLOR_TEMP:
                    done = send_step_(false);
                    break;

                default:
                    break;
            }

            if (done) {
                op_state_ = IDLE;
                process_state_machine_();
                if (op_state_ == IDLE) {
                    needs_state_publish_ = true;
                }
            }
        }

        void QuntisLight::process_state_machine_() {
            // Priority: power > brightness > color temp
            if (has_pending_power_) {
                ESP_LOGI(TAG, "Toggling power to %s", ONOFF(target_power_));
                controller_.OnOff();
                op_state_ = TOGGLING_POWER;
                last_step_time_ = millis();
                return;
            }

            if (start_transition_(has_pending_brightness_, current_brightness_step_,
                                  target_brightness_step_, SENDING_BRIGHTNESS, "brightness")) return;
            if (start_transition_(has_pending_color_, current_color_step_,
                                  target_color_step_, SENDING_COLOR_TEMP, "color")) return;

            // Nothing pending, stay idle
            op_state_ = IDLE;
            if (is_calibrating_) {
                is_calibrating_ = false;
                ESP_LOGI(TAG, "Calibration complete: brightness=%d, color=%d", current_brightness_step_, current_color_step_);
            }
        }

        bool QuntisLight::send_step_(bool is_brightness) {
            int& current_step = is_brightness ? current_brightness_step_ : current_color_step_;
            const char* label = is_brightness ? "Brightness" : "Color";

            if (remaining_steps_ > 0) {
                if (is_brightness)
                    controller_.Dim(step_direction_, true);
                else
                    controller_.Color(step_direction_, true);
                remaining_steps_--;
                current_step += step_direction_ ? 1 : -1;
                ESP_LOGV(TAG, "%s step: %s, remaining=%d, current=%d",
                         label,
                         is_brightness ? (step_direction_ ? "UP" : "DOWN")
                                       : (step_direction_ ? "COLDER" : "WARMER"),
                         remaining_steps_, current_step);
            }
            if (remaining_steps_ <= 0) {
                int target = is_brightness ? target_brightness_step_ : target_color_step_;
                bool pending = is_brightness ? has_pending_brightness_ : has_pending_color_;
                ESP_LOGI(TAG, "%s transition done: current=%d, target=%d, pending=%s",
                         label, current_step, target, YESNO(pending));
                return true;
            }
            return false;
        }

        bool QuntisLight::start_transition_(bool& pending, int current, int target,
                                            OperationState state, const char* label) {
            if (!pending) return false;
            int diff = target - current;
            if (diff != 0) {
                step_direction_ = (diff > 0);
                remaining_steps_ = abs(diff);
                op_state_ = state;
                last_step_time_ = 0;
                ESP_LOGI(TAG, "Starting %s transition: %d -> %d (%d steps %s)",
                         label, current, target, remaining_steps_, step_direction_ ? "up" : "down");
                return true;
            }
            pending = false;
            ESP_LOGD(TAG, "%s already at target %d, clearing pending", label, current);
            return false;
        }

        void QuntisLight::queue_target_(int target, int& target_step, bool& pending,
                                        int current, const char* label) {
            if (target != current) {
                if (pending && op_state_ != IDLE) {
                    ESP_LOGI(TAG, "Pending %s overwritten: %d -> %d (was targeting %d)",
                             label, current, target, target_step);
                }
                target_step = target;
                pending = true;
            }
        }

        void QuntisLight::publish_current_state_() {
            if (!light_state_) return;

            auto call = light_state_->make_call();
            call.set_state(current_power_);
            if (current_power_) {
                // Quantize brightness to our step grid; ensure non-zero when on (step 0 = dimmest, not off)
                float brightness = std::max((float)current_brightness_step_ / brightness_steps_,
                                            1.0f / brightness_steps_);
                call.set_brightness(brightness);
                call.set_color_temperature(percent_to_mireds_((current_color_step_ * 100) / color_temp_steps_));
            }
            call.perform();

            ESP_LOGD(TAG, "Published state to HA: on=%s brightness_step=%d color_step=%d",
                     ONOFF(current_power_), current_brightness_step_, current_color_step_);
        }

        //
        // HomeAssistent uses mireds for color temperature, for us a simple percentage is easier
        // Inverted mapping: high mireds (warm) → 0%, low mireds (cold) → 100%
        //
        int QuntisLight::mireds_to_percent_(float mireds) {
            if (mireds >= max_mireds_) return 0;
            if (mireds <= min_mireds_) return 100;
            return (int)(((max_mireds_ - mireds) * 100.0f) / (max_mireds_ - min_mireds_));
        }

        float QuntisLight::percent_to_mireds_(int percent) {
            return max_mireds_ - (percent * (max_mireds_ - min_mireds_) / 100.0f);
        }

        //
        // Calibration and power override
        //
        void QuntisLight::calibrate() {
            if (is_calibrating_) {
                ESP_LOGW(TAG, "Calibration already in progress, ignoring");
                return;
            }

            ESP_LOGI(TAG, "Calibrating: sending %d dim-down + %d color-cold steps to reach known minimum",
                     brightness_steps_, color_temp_steps_);

            is_calibrating_ = true;

            // Assume worst case: lamp is at max brightness and warmest color
            current_brightness_step_ = brightness_steps_;
            current_color_step_ = 0;
            current_power_ = true;

            // Target: minimum brightness, coldest color temp
            target_brightness_step_ = 0;
            target_color_step_ = color_temp_steps_;
            has_pending_brightness_ = true;
            has_pending_color_ = true;

            if (op_state_ == IDLE) {
                process_state_machine_();
            }
        }

        void QuntisLight::override_power_state(bool state) {
            ESP_LOGI(TAG, "Power state override: %s -> %s (no RF sent)", ONOFF(current_power_), ONOFF(state));
            current_power_ = state;
            needs_state_publish_ = true;
        }

    }  // namespace quntis_light
}  // namespace esphome
