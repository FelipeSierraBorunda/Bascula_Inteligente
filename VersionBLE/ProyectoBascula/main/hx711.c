#include "hx711.h"
#include "config.h"
#include "estado.h"
#include "ble_nutrigo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"

static float s_ventana[N_VENTANA];
static int   s_idx = 0;
static int   s_n   = 0;

static void actualizar_estabilidad(float p)
{
    s_ventana[s_idx] = p;
    s_idx = (s_idx + 1) % N_VENTANA;
    if (s_n < N_VENTANA) {
        s_n++;
        s_estable = false;
        return;
    }
    float mn = s_ventana[0], mx = s_ventana[0];
    for (int i = 1; i < N_VENTANA; i++) {
        if (s_ventana[i] < mn) mn = s_ventana[i];
        if (s_ventana[i] > mx) mx = s_ventana[i];
    }
    s_estable = (mx - mn) <= UMBRAL_ESTABLE_G;
}

void hx711_init(void)
{
    gpio_set_direction(PIN_HX711_DOUT, GPIO_MODE_INPUT);
    gpio_set_direction(PIN_HX711_SCK, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_HX711_SCK, 0);
}

bool hx711_leer(int32_t *salida)
{
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(PIN_HX711_DOUT) == 1) {
        if (esp_timer_get_time() - t0 > HX_TIMEOUT_US) {
            return false;
        }
        vTaskDelay(1);
    }

    uint32_t d = 0;
    for (int i = 0; i < 24; i++) {
        gpio_set_level(PIN_HX711_SCK, 1);
        esp_rom_delay_us(1);
        d = (d << 1) | (uint32_t)gpio_get_level(PIN_HX711_DOUT);
        gpio_set_level(PIN_HX711_SCK, 0);
        esp_rom_delay_us(1);
    }
    gpio_set_level(PIN_HX711_SCK, 1);   // pulso extra: canal A, ganancia 128
    esp_rom_delay_us(1);
    gpio_set_level(PIN_HX711_SCK, 0);

    if (d & 0x800000) {
        d |= 0xFF000000;
    }
    *salida = (int32_t)d;
    return true;
}

bool hx711_promedio(int n, int32_t *salida)
{
    int64_t suma = 0;
    int ok = 0;
    int32_t v;
    for (int i = 0; i < n; i++) {
        if (hx711_leer(&v)) {
            suma += v;
            ok++;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (ok == 0) {
        return false;
    }
    *salida = (int32_t)(suma / ok);
    return true;
}

void hx711_tarea(void *arg)
{
    int32_t raw;
    for (;;) {
        if (hx711_leer(&raw)) {
            s_ajustada   = raw - s_offset;
            s_peso_bruto = s_ajustada * s_factor;
            s_peso_g     = s_peso_bruto - s_tara_g;
            actualizar_estabilidad(s_peso_g);
            ble_nutrigo_publicar();
        }
        vTaskDelay(pdMS_TO_TICKS(s_dormido ? HX_PERIODO_DORMIDO_MS : HX_PERIODO_MS));
    }
}
