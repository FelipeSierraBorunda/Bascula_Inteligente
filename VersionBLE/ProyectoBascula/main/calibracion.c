#include "calibracion.h"
#include "estado.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "CALIBRACION";
#define NVS_NAMESPACE "bascula"
#define NVS_CLAVE_FACTOR "factor"

void calibracion_cargar(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_factor);
    if (nvs_get_blob(h, NVS_CLAVE_FACTOR, &s_factor, &len) == ESP_OK && len == sizeof(s_factor)) {
        s_calibrada = true;
        ESP_LOGI(TAG, "Calibración cargada: %.6f g/cuenta", s_factor);
    }
    nvs_close(h);
}

esp_err_t calibracion_guardar(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, NVS_CLAVE_FACTOR, &s_factor, sizeof(s_factor));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}
