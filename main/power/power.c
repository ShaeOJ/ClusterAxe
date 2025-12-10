#include "TPS546.h"
#include "INA260.h"
#include "DS4432U.h"

#include "power.h"

float Power_get_current(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        return TPS546_get_iout() * 1000.0;
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.INA260) {
        return INA260_read_current();
    }

    return 0.0;
}

/**
 * @brief Get total system power consumption
 *
 * Power measurement strategy depends on available hardware:
 *
 * TPS546 (ASIC voltage regulator):
 *   - Measures only ASIC core power (Vcore * Icore)
 *   - Does NOT include: ESP32 WiFi, display, fans, peripherals
 *   - Uses static power_offset from device config to estimate system overhead
 *   - Typical offset: 3-5W for 600-series, varies by board
 *
 * INA260 (main power rail sensor):
 *   - Measures total input power from PSU
 *   - Includes all system components
 *   - More accurate for total system power
 *
 * Future improvements:
 *   - Add INA260 sensors on individual rails (3.3V, 5V, 12V) for per-subsystem tracking
 *   - Implement dynamic power model based on WiFi activity, display state, fan RPM
 *   - Log power consumption patterns to refine offset values
 *
 * @param GLOBAL_STATE Pointer to global state
 * @return Total system power in watts (W)
 */
float Power_get_power(GlobalState * GLOBAL_STATE)
{
    float power = 0.0;
    float current = 0.0;

    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        current = TPS546_get_iout() * 1000.0;
        // Calculate ASIC regulator output power (in watts)
        power = TPS546_get_vout() * current / 1000.0;

        // Add static offset for non-ASIC system components
        // This is a rough approximation - actual overhead varies with:
        // - WiFi TX/RX activity
        // - Display brightness/refresh
        // - Fan speed (PWM duty cycle)
        // - I2C/UART communication
        power += GLOBAL_STATE->DEVICE_CONFIG.family.power_offset;
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.INA260) {
        // INA260 on main rail gives total system power - use this if available
        power = INA260_read_power() / 1000.0;
    }

    return power;
}

float Power_get_input_voltage(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        return TPS546_get_vin() * 1000.0;
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.INA260) {
        return INA260_read_voltage();
    }
    
    return 0.0;
}

float Power_get_vreg_temp(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        return TPS546_get_temperature();
    }

    return 0.0;
}
