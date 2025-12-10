#include <string.h>
#include "device_config.h"
#include "nvs_config.h"
#include "global_state.h"
#include "esp_log.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static const char * TAG = "device_config";

/**
 * @brief Attempt to read board version from eFuse USER_DATA block
 *
 * This function tries to read board version information from ESP32 eFuse.
 * Format: First 3 bytes of USER_DATA contain board version string (e.g., "701")
 *
 * @param board_version_out Buffer to store the board version string (min 4 bytes)
 * @return true if board version was successfully read from eFuse, false otherwise
 */
static bool efuse_read_board_version(char *board_version_out, size_t max_len)
{
    if (board_version_out == NULL || max_len < 4) {
        return false;
    }

    // Read USER_DATA block (example: using first 3 bytes for board version)
    // This assumes board version was burned to eFuse during manufacturing
    // Format example: "701" for Bitaxe SupraHex board 701
    uint8_t efuse_data[4] = {0};

    // Try to read from eFuse - this is a placeholder implementation
    // In production, you'd use specific eFuse block designated for board info
    // esp_efuse_read_block() or esp_efuse_read_field_blob() would be used here

    // Check if efuse has valid ASCII board version (3 digits)
    bool valid = true;
    for (int i = 0; i < 3; i++) {
        if (efuse_data[i] < '0' || efuse_data[i] > '9') {
            valid = false;
            break;
        }
    }

    if (valid && efuse_data[0] != 0) {
        snprintf(board_version_out, max_len, "%c%c%c", efuse_data[0], efuse_data[1], efuse_data[2]);
        ESP_LOGI(TAG, "Board version read from eFuse: %s", board_version_out);
        return true;
    }

    return false;
}

esp_err_t device_config_init(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    // Try to read board version from eFuse first (hardware-programmed during manufacturing)
    char efuse_board_version[4] = {0};
    bool efuse_valid = efuse_read_board_version(efuse_board_version, sizeof(efuse_board_version));

    // Get board version from NVS (user-configured or default)
    char * board_version = nvs_config_get_string(NVS_CONFIG_BOARD_VERSION);

    // If eFuse has valid board version and differs from NVS, warn user
    if (efuse_valid && board_version && strcmp(efuse_board_version, board_version) != 0) {
        ESP_LOGW(TAG, "Board version mismatch: eFuse='%s' vs NVS='%s'. Using eFuse value.",
                 efuse_board_version, board_version);
        free(board_version);
        board_version = strdup(efuse_board_version);
        if (!board_version) {
            ESP_LOGE(TAG, "Failed to allocate memory for board_version from eFuse");
            return ESP_ERR_NO_MEM;
        }
        // Optionally update NVS to match eFuse
        nvs_config_set_string(NVS_CONFIG_BOARD_VERSION, efuse_board_version);
    } else if (efuse_valid && (board_version == NULL || board_version[0] == '\0')) {
        ESP_LOGI(TAG, "Using board version from eFuse: %s", efuse_board_version);
        if (board_version) free(board_version);
        board_version = strdup(efuse_board_version);
        if (!board_version) {
            ESP_LOGE(TAG, "Failed to allocate memory for board_version from eFuse");
            return ESP_ERR_NO_MEM;
        }
        nvs_config_set_string(NVS_CONFIG_BOARD_VERSION, efuse_board_version);
    } else if (!efuse_valid) {
        ESP_LOGI(TAG, "No valid board version in eFuse, using NVS configuration: %s",
                 board_version ? board_version : "default");
    }

    // Safety check: if board_version is NULL at this point, we can't proceed
    if (!board_version) {
        ESP_LOGE(TAG, "Board version is NULL, cannot initialize device config");
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0 ; i < ARRAY_SIZE(default_configs); i++) {
        if (strcmp(default_configs[i].board_version, board_version) == 0) {
            GLOBAL_STATE->DEVICE_CONFIG = default_configs[i];

            ESP_LOGI(TAG, "Device Model: %s", GLOBAL_STATE->DEVICE_CONFIG.family.name);
            ESP_LOGI(TAG, "Board Version: %s", GLOBAL_STATE->DEVICE_CONFIG.board_version);
            ESP_LOGI(TAG, "ASIC: %dx %s (%d cores)", GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name, GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count);

            free(board_version);
            return ESP_OK;
        }
    }

    ESP_LOGI(TAG, "Custom Board Version: %s", board_version);

    GLOBAL_STATE->DEVICE_CONFIG.board_version = strdup(board_version);
    if (!GLOBAL_STATE->DEVICE_CONFIG.board_version) {
        ESP_LOGE(TAG, "Failed to allocate memory for device config board_version");
        free(board_version);
        return ESP_ERR_NO_MEM;
    }

    char * device_model = nvs_config_get_string(NVS_CONFIG_DEVICE_MODEL);

    for (int i = 0 ; i < ARRAY_SIZE(default_families); i++) {
        if (strcasecmp(default_families[i].name, device_model) == 0) {
            GLOBAL_STATE->DEVICE_CONFIG.family = default_families[i];

            ESP_LOGI(TAG, "Device Model: %s", GLOBAL_STATE->DEVICE_CONFIG.family.name);

            break;
        }
    }

    char * asic_model = nvs_config_get_string(NVS_CONFIG_ASIC_MODEL);

    for (int i = 0 ; i < ARRAY_SIZE(default_asic_configs); i++) {
        if (strcasecmp(default_asic_configs[i].name, asic_model) == 0) {
            GLOBAL_STATE->DEVICE_CONFIG.family.asic = default_asic_configs[i];

            ESP_LOGI(TAG, "ASIC: %dx %s (%d cores)", GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name, GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count);

            break;
        }
    }

    GLOBAL_STATE->DEVICE_CONFIG.plug_sense = nvs_config_get_bool(NVS_CONFIG_PLUG_SENSE);
    GLOBAL_STATE->DEVICE_CONFIG.asic_enable = nvs_config_get_bool(NVS_CONFIG_ASIC_ENABLE);
    GLOBAL_STATE->DEVICE_CONFIG.EMC2101 = nvs_config_get_bool(NVS_CONFIG_EMC2101);
    GLOBAL_STATE->DEVICE_CONFIG.EMC2103 = nvs_config_get_bool(NVS_CONFIG_EMC2103);
    GLOBAL_STATE->DEVICE_CONFIG.EMC2302 = nvs_config_get_bool(NVS_CONFIG_EMC2302);
    GLOBAL_STATE->DEVICE_CONFIG.emc_internal_temp = nvs_config_get_bool(NVS_CONFIG_EMC_INTERNAL_TEMP);
    GLOBAL_STATE->DEVICE_CONFIG.emc_ideality_factor = nvs_config_get_u16(NVS_CONFIG_EMC_IDEALITY_FACTOR);
    GLOBAL_STATE->DEVICE_CONFIG.emc_beta_compensation = nvs_config_get_u16(NVS_CONFIG_EMC_BETA_COMPENSATION);
    GLOBAL_STATE->DEVICE_CONFIG.temp_offset = nvs_config_get_i32(NVS_CONFIG_TEMP_OFFSET);
    GLOBAL_STATE->DEVICE_CONFIG.DS4432U = nvs_config_get_bool(NVS_CONFIG_DS4432U);
    GLOBAL_STATE->DEVICE_CONFIG.INA260 = nvs_config_get_bool(NVS_CONFIG_INA260);
    GLOBAL_STATE->DEVICE_CONFIG.TPS546 = nvs_config_get_bool(NVS_CONFIG_TPS546);
    GLOBAL_STATE->DEVICE_CONFIG.TMP1075 = nvs_config_get_bool(NVS_CONFIG_TMP1075);

    // test values
    GLOBAL_STATE->DEVICE_CONFIG.power_consumption_target = nvs_config_get_u16(NVS_CONFIG_POWER_CONSUMPTION_TARGET);

    free(board_version);
    free(device_model);
    free(asic_model);

    return ESP_OK;
}
