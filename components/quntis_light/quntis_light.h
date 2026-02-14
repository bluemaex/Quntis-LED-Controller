#pragma once

#include <vector>

#include "esphome/components/light/light_output.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "quntis_control.h"

namespace esphome {
    namespace quntis_light {

        class QuntisLight : public light::LightOutput, public Component {
           public:
            void setup() override;
            void loop() override;
            void dump_config() override;
            float get_setup_priority() const override { return setup_priority::HARDWARE; }

            light::LightTraits get_traits() override;
            void write_state(light::LightState* state) override;
            void calibrate();
            bool is_calibrating() const { return is_calibrating_; }
            bool is_on() const { return current_power_; }
            void override_power_state(bool state);

            // Configuration setters (called by generated code from light.py)
            void set_ce_pin(uint8_t pin) { ce_pin_ = pin; }
            void set_cs_pin(uint8_t pin) { cs_pin_ = pin; }
            void set_spi_clk_pin(uint8_t pin) { spi_clk_pin_ = pin; }
            void set_spi_mosi_pin(uint8_t pin) { spi_mosi_pin_ = pin; }
            void set_spi_miso_pin(uint8_t pin) { spi_miso_pin_ = pin; }
            void set_device_address(std::vector<uint8_t> addr) { controller_.set_device_address(addr); }
            void set_device_payload(std::vector<uint8_t> payload) { controller_.set_device_payload(payload); }
            void set_brightness_steps(int steps) { brightness_steps_ = steps; }
            void set_color_temp_steps(int steps) { color_temp_steps_ = steps; }
            void set_min_mireds(float mireds) { min_mireds_ = mireds; }
            void set_max_mireds(float mireds) { max_mireds_ = mireds; }
            void set_step_delay(uint32_t delay_ms) { step_delay_ms_ = delay_ms; }

           protected:
            // State machine for non-blocking RF step operations
            enum OperationState {
                IDLE,
                TOGGLING_POWER,
                SENDING_BRIGHTNESS,
                SENDING_COLOR_TEMP,
            };

            void process_state_machine_();
            bool send_step_(bool is_brightness);
            bool start_transition_(bool& pending, int current, int target, OperationState state, const char* label);
            void queue_target_(int target, int& target_step, bool& pending, int current, const char* label);
            void publish_current_state_();
            int mireds_to_percent_(float mireds);
            float percent_to_mireds_(int percent);

            // Hardware configuration
            uint8_t ce_pin_{1};
            uint8_t cs_pin_{5};
            uint8_t spi_clk_pin_{2};
            uint8_t spi_mosi_pin_{4};
            uint8_t spi_miso_pin_{3};
            // Light parameters
            int brightness_steps_{100};
            int color_temp_steps_{50};
            float min_mireds_{153};
            float max_mireds_{500};
            uint32_t step_delay_ms_{100};

            // RF controller
            QuntisControl controller_;

            // Current tracked state (what we believe the lamp is at)
            bool current_power_{false};
            int current_brightness_step_{0};
            int current_color_step_{25};  // Mid-point default

            // State machine
            OperationState op_state_{IDLE};
            uint32_t last_step_time_{0};
            int remaining_steps_{0};
            bool step_direction_{true};

            // Queued target values from write_state()
            bool target_power_{false};
            int target_brightness_step_{0};
            int target_color_step_{25};
            bool has_pending_power_{false};
            bool has_pending_brightness_{false};
            bool has_pending_color_{false};

            // Calibration state
            bool is_calibrating_{false};

            // Skip RF on first write_state (boot restore) since we can't know lamp's actual state
            bool first_write_{true};

            // Deferred state publish flag (avoids recursive write_state calls)
            bool needs_state_publish_{false};

            // Reference to light state for publishing updates
            light::LightState* light_state_{nullptr};
        };

    }  // namespace quntis_light
}  // namespace esphome
