#include "sdkconfig.h"
#include <stdbool.h>
#include <stddef.h>
#include "../managed_components/espressif__esp-sr/include/esp32s3/esp_afe_sr_iface.h"
#include "../managed_components/espressif__esp-sr/include/esp32s3/esp_afe_sr_models.h"

// Fungsi ini membungkus Macro C99 agar bisa dibaca oleh C++
afe_config_t get_default_afe_config()
{
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
    afe_config_t config = AFE_CONFIG_DEFAULT();
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    return config;
}