#include "bateria.h"
#include "config.h"
#include "estado.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "BATERIA";
#define BAT_ADC_CH ADC_CHANNEL_0   // GPIO0 en ESP32-C3

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali = NULL;

void bateria_init(void)
{
    adc_oneshot_unit_init_cfg_t ucfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&ucfg, &s_adc));

    adc_oneshot_chan_cfg_t ccfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, BAT_ADC_CH, &ccfg));

    adc_cali_curve_fitting_config_t cc = {
        .unit_id  = ADC_UNIT_1,
        .chan     = BAT_ADC_CH,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cc, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "Sin calibración de ADC; uso aproximación lineal");
        s_cali = NULL;
    }
}

void bateria_tarea(void *arg)
{
    for (;;) {
        int raw = 0, mv = 0;
        if (adc_oneshot_read(s_adc, BAT_ADC_CH, &raw) == ESP_OK) {
            if (s_cali == NULL || adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) {
                mv = raw * 3300 / 4095;
            }
            s_bateria_mv = (uint16_t)(mv * BAT_DIVISOR);
        }
        vTaskDelay(pdMS_TO_TICKS(BAT_PERIODO_MS));
    }
}
