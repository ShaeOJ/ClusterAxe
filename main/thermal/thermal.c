#include "thermal.h"

#include "esp_log.h"
#include "esp_check.h"

#include "EMC2101.h"
#include "EMC2103.h"
#include "EMC2302.h"
#include "TMP1075.h"

static const char * TAG = "thermal";

esp_err_t Thermal_init(DeviceConfig * DEVICE_CONFIG)
{
    if (DEVICE_CONFIG->EMC2101) {
        ESP_RETURN_ON_ERROR(EMC2101_init(DEVICE_CONFIG->temp_offset), TAG, "Failed to initialise EMC2101");

        // Validate and apply EMC2101 configuration if set
        // Valid ideality factor range: 0x08 to 0x37 (as per emc2101_ideality_t enum)
        // Valid beta compensation range: 0x00 to 0x08 (as per emc2101_beta_t enum)
        bool ideality_valid = (DEVICE_CONFIG->emc_ideality_factor >= 0x08 &&
                               DEVICE_CONFIG->emc_ideality_factor <= 0x37);
        bool beta_valid = (DEVICE_CONFIG->emc_beta_compensation <= 0x08);

        if (DEVICE_CONFIG->emc_ideality_factor != 0x00 && ideality_valid && beta_valid) {
            ESP_LOGI(TAG, "EMC2101 configuration: Ideality Factor: 0x%02x, Beta Compensation: 0x%02x",
                     DEVICE_CONFIG->emc_ideality_factor, DEVICE_CONFIG->emc_beta_compensation);
            EMC2101_set_ideality_factor(DEVICE_CONFIG->emc_ideality_factor);
            EMC2101_set_beta_compensation(DEVICE_CONFIG->emc_beta_compensation);
        } else if (DEVICE_CONFIG->emc_ideality_factor != 0x00) {
            ESP_LOGW(TAG, "EMC2101 configuration out of range - Ideality: 0x%02x (valid: 0x08-0x37), Beta: 0x%02x (valid: 0x00-0x08). Using defaults.",
                     DEVICE_CONFIG->emc_ideality_factor, DEVICE_CONFIG->emc_beta_compensation);
        }
    }
    if (DEVICE_CONFIG->EMC2103) {
        ESP_RETURN_ON_ERROR(EMC2103_init(DEVICE_CONFIG->temp_offset), TAG, "Failed to initialise EMC2103");
    }
    if (DEVICE_CONFIG->EMC2302) {
        ESP_RETURN_ON_ERROR(EMC2302_init(), TAG, "Failed to initialise EMC2302");
    }
    if (DEVICE_CONFIG->TMP1075) {
        ESP_RETURN_ON_ERROR(TMP1075_init(DEVICE_CONFIG->temp_offset), TAG, "Failed to initialise TMP1075");
    }

    return ESP_OK;
}

//percent is a float between 0.0 and 1.0
esp_err_t Thermal_set_fan_percent(DeviceConfig * DEVICE_CONFIG, float percent)
{
    if (DEVICE_CONFIG->EMC2101) {
        return EMC2101_set_fan_speed(percent);
    }
    if (DEVICE_CONFIG->EMC2103) {
        return EMC2103_set_fan_speed(percent);
    }
    if (DEVICE_CONFIG->EMC2302) {
        EMC2302_set_fan_speed(percent);
    }
    return ESP_OK;
}

uint16_t Thermal_get_fan_speed(DeviceConfig * DEVICE_CONFIG) 
{
    if (DEVICE_CONFIG->EMC2101) {
        return EMC2101_get_fan_speed();
    }
    if (DEVICE_CONFIG->EMC2103) {
        return EMC2103_get_fan_speed();
    }
    if (DEVICE_CONFIG->EMC2302) {
        return EMC2302_get_fan_speed();
    }
    return 0;
}

uint16_t Thermal_get_fan2_speed(DeviceConfig * DEVICE_CONFIG) 
{
    if (DEVICE_CONFIG->EMC2302) {
        return EMC2302_get_fan2_speed();
    }
    return 0;
}

float Thermal_get_chip_temp(GlobalState * GLOBAL_STATE)
{
    if (!GLOBAL_STATE->ASIC_initalized) {
        return -1;
    }

    if (GLOBAL_STATE->DEVICE_CONFIG.EMC2101) {
        if (GLOBAL_STATE->DEVICE_CONFIG.emc_internal_temp) {
            return EMC2101_get_internal_temp();
        } else {
            return EMC2101_get_external_temp();
        }
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.EMC2103) {
        return EMC2103_get_external_temp();
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.TMP1075) {
        return TMP1075_read_temperature(0);
    }
    return -1;
}

float Thermal_get_chip_temp2(GlobalState * GLOBAL_STATE)
{
    if (!GLOBAL_STATE->ASIC_initalized) {
        return -1;
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.EMC2103) {
        return EMC2103_get_external_temp2();
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.TMP1075) {
        return TMP1075_read_temperature(1);
    }
    return -1;
}
