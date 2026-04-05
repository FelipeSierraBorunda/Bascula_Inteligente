#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "driver/gpio.h"

#define NIVEL_BATERIA_CH   ADC1_CHANNEL_0
#define NIVEL_BATERIA_GPIO GPIO_NUM_0

// A partir de tu calibración:
// raw = 2249 corresponde a 1.642 V → Vref ≈ 2.99 V
#define ADC_MAX_COUNTS   4095.0f
#define ADC_VREF_CAL     2.99f

#define R1_BAT 47000.0f
#define R2_BAT 47000.0f

volatile float g_v_bat = 0.0f;

static void bateria_adc_init(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(NIVEL_BATERIA_CH, ADC_ATTEN_DB_12);

    gpio_pullup_dis(NIVEL_BATERIA_GPIO);
    gpio_pulldown_dis(NIVEL_BATERIA_GPIO);
}

static void bateria_task(void *arg)
{
    bateria_adc_init();

    while (1)
    {
        int raw = adc1_get_raw(NIVEL_BATERIA_CH);

        // Usar el Vref calibrado
        float v_adc = ((float)raw / ADC_MAX_COUNTS) * ADC_VREF_CAL;

        // Voltaje real de la batería
        float v_bat = v_adc * (R1_BAT + R2_BAT) / R2_BAT;

        g_v_bat = v_bat;

        printf("RAW: %d | Vadc: %.3f V | Vbat: %.3f V\n",
               raw, v_adc, v_bat);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    xTaskCreate(bateria_task, "bateria_task", 2048, NULL, 5, NULL);
}

