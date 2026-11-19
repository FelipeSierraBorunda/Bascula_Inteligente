// NutriGo — Báscula BLE (ESP32-C3 mini). Arranque de módulos y tareas.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "config.h"
#include "estado.h"
#include "calibracion.h"
#include "hx711.h"
#include "bateria.h"
#include "ble_nutrigo.h"

static const char *TAG = "NUTRIGO";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    calibracion_cargar();
    hx711_init();
    bateria_init();

    ESP_LOGI(TAG, "Calibrando cero: mantén la báscula vacía...");
    if (!hx711_promedio(HX_MUESTRAS_OFFSET, &s_offset)) {
        ESP_LOGE(TAG, "HX711 no responde");
    }

    ble_nutrigo_iniciar();

    xTaskCreate(hx711_tarea,   "hx711",   STACK_HX711,   NULL, PRIO_HX711,   NULL);
    xTaskCreate(bateria_tarea, "bateria", STACK_BATERIA, NULL, PRIO_BATERIA, NULL);
}
