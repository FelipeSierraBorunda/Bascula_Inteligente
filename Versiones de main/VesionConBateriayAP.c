// =============================================================
//  Báscula con HX711 + ESP32 (ESP-IDF) 
//  Autor: Felipe 
// =============================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "driver/adc.h"

#include "lwip/api.h"   // netconn API
#include "lwip/err.h"

// ============================== Pines ===============================
#define DOUT_PIN     4   // HX711 DOUT
#define PD_SCK_PIN   3   // HX711 SCK

// ============================ Globales ==============================
static const char *TAG = "BASCULA";

static int32_t offset_bascula = 0;            // Cero (tara) del HX711
static float   peso_referencia = 57.72f;      // g: masa patrón para escalar
static int32_t lecturaDeReferencia = 100046;  // cuentas a esa masa
static bool    calibrado_con_peso = false;

// Variables compartidas entre tareas (lecturas/peso/estado)
volatile int32_t lectura_cruda_global    = 0;
volatile int32_t lectura_ajustada_global = 0;
volatile float   peso_global             = 0.0f;

volatile int   peso_deseado = 0;     // objetivo de dosificación (g)

// ========================= Batería (ADC) ============================
#define NIVEL_BATERIA_CH   ADC1_CHANNEL_0
#define NIVEL_BATERIA_GPIO GPIO_NUM_0

#define ADC_MAX_COUNTS   4095.0f
#define ADC_VREF_CAL     2.99f     // Vref real medida

#define R1_BAT 47000.0f
#define R2_BAT 47000.0f

volatile float g_v_bat = 0.0f;

// ======================== Prototipos (HX711) ========================
static void     hx711_init(void);
static int32_t  hx711_read_raw_24bit(void);
static void     calibrar_bascula(void);
static void     task_hx711(void *arg);
static void     bateria_task(void *arg);

// ================== Prototipos (WiFi + HTTP server) =================
static void     wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);
static void     http_server(void *arg);
static void     ResponderConexion(struct netconn *conn);

// =========================== HTTP assets ============================
static const char HTML_Header[]   = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
static const char icoIMG_Header[] = "HTTP/1.1 200 OK\r\nContent-Type: image/ico\r\n\r\n";

extern const uint8_t LEDsPage_html_start[] asm("_binary_LEDsPage_html_start");
extern const uint8_t LEDsPage_html_end[]   asm("_binary_LEDsPage_html_end");
extern const uint8_t admin_html_start[]    asm("_binary_admin_html_start");
extern const uint8_t admin_html_end[]      asm("_binary_admin_html_end");
extern const uint8_t favicon_ico_start[]   asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[]     asm("_binary_favicon_ico_end");

// ====================================================================
//                          MÓDULO HX711
// ====================================================================
static void hx711_init(void)
{
    gpio_set_direction(DOUT_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(PD_SCK_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PD_SCK_PIN, 0);
}

static void bateria_adc_init(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(NIVEL_BATERIA_CH, ADC_ATTEN_DB_12);

    gpio_pullup_dis(NIVEL_BATERIA_GPIO);
    gpio_pulldown_dis(NIVEL_BATERIA_GPIO);
}

static int32_t hx711_read_raw_24bit(void)
{
    int32_t data = 0;

    // Espera a DOUT en bajo (dato listo)
    while (gpio_get_level(DOUT_PIN) == 1) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // 24 bits MSB primero
    for (int i = 0; i < 24; i++) {
        gpio_set_level(PD_SCK_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        data <<= 1;
        if (gpio_get_level(DOUT_PIN)) data |= 1;
        gpio_set_level(PD_SCK_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // 1 pulso extra -> canal A ganancia 128
    gpio_set_level(PD_SCK_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(PD_SCK_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    // Extensión de signo (24 -> 32 bits)
    if (data & 0x800000) {
        data |= 0xFF000000;
    }
    return data;
}

static void calibrar_bascula(void)
{
    int64_t suma = 0;
    printf("\nIniciando calibración...\n");

    for (int i = 0; i < 10; i++) {
        int32_t lectura = hx711_read_raw_24bit();
        suma += lectura;
        printf(" Lectura %d: %ld\n", i + 1, (long)lectura);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    offset_bascula = (int32_t)(suma / 10);
    calibrado_con_peso = true;
    printf("\nCalibración completada. Offset (cero): %ld\n", (long)offset_bascula);
}

static void task_hx711(void *arg)
{
    int lectura_num = 0;
    while (1) {
        int32_t lectura_cruda = hx711_read_raw_24bit();
        lectura_num++;

        int32_t lectura_ajustada = lectura_cruda - offset_bascula;
        float peso_calculado = 0.0f;
        if (calibrado_con_peso && lecturaDeReferencia != 0) {
            peso_calculado = ((float)lectura_ajustada * peso_referencia) / (float)lecturaDeReferencia;
        }

        lectura_cruda_global    = lectura_cruda;
        lectura_ajustada_global = lectura_ajustada;
        peso_global             = peso_calculado;

        printf("\n=========================================\n");
        printf(" Lectura # %d\n", lectura_num);
        printf(" Crudo:    %ld\n", (long)lectura_cruda);
        printf(" Ajustado: %ld\n", (long)lectura_ajustada);
        printf(" Peso:     %.2f g\n", peso_calculado);
        printf("=========================================\n");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void bateria_task(void *arg)
{
    bateria_adc_init();

    while (1)
    {
        int raw = adc1_get_raw(NIVEL_BATERIA_CH);

        float v_adc = ((float)raw / ADC_MAX_COUNTS) * ADC_VREF_CAL;
        float v_bat = v_adc * (R1_BAT + R2_BAT) / R2_BAT;

        g_v_bat = v_bat;

        printf("RAW: %d | Vadc: %.3f V | Vbat: %.3f V\n",
               raw, v_adc, v_bat);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ====================================================================
//                           WIFI + HTTP
// ====================================================================
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *evt = (wifi_event_ap_staconnected_t *)data;
        printf("\nCliente conectado al AP. MAC: %02X:%02X:%02X:%02X:%02X:%02X  AID:%d\n",
               evt->mac[0], evt->mac[1], evt->mac[2],
               evt->mac[3], evt->mac[4], evt->mac[5], evt->aid);
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *evt = (wifi_event_ap_stadisconnected_t *)data;
        printf("\nCliente desconectado del AP. MAC: %02X:%02X:%02X:%02X:%02X:%02X  AID:%d\n",
               evt->mac[0], evt->mac[1], evt->mac[2],
               evt->mac[3], evt->mac[4], evt->mac[5], evt->aid);
    }
}

static void ResponderConexion(struct netconn *conn)
{
    struct netbuf *inbuf;
    char *buf;
    u16_t buflen;

    err_t err = netconn_recv(conn, &inbuf);
    if (err == ERR_OK) {
        netbuf_data(inbuf, (void **)&buf, &buflen);
        char *ptr;

        // Página principal
        ptr = strstr(buf, "GET / HTTP");
        if (ptr != NULL) {
            printf("GET /\n");
            netconn_write(conn, HTML_Header, sizeof(HTML_Header) - 1, NETCONN_NOCOPY);
            netconn_write(conn, LEDsPage_html_start, LEDsPage_html_end - LEDsPage_html_start, NETCONN_NOCOPY);
        }
        // /admin
        ptr = strstr(buf, "GET /admin HTTP");
        if (ptr != NULL) {
            printf("GET /admin\n");
            netconn_write(conn, HTML_Header, sizeof(HTML_Header) - 1, NETCONN_NOCOPY);
            netconn_write(conn, admin_html_start, admin_html_end - admin_html_start, NETCONN_NOCOPY);
        }
        // /LEDsPage
        ptr = strstr(buf, "GET /LEDsPage HTTP");
        if (ptr != NULL) {
            printf("GET /LEDsPage\n");
            netconn_write(conn, HTML_Header, sizeof(HTML_Header) - 1, NETCONN_NOCOPY);
            netconn_write(conn, LEDsPage_html_start, LEDsPage_html_end - LEDsPage_html_start, NETCONN_NOCOPY);
        }
        // favicon
        ptr = strstr(buf, "GET /favicon.ico");
        if (ptr != NULL) {
            printf("GET /favicon.ico\n");
            netconn_write(conn, icoIMG_Header, sizeof(icoIMG_Header) - 1, NETCONN_NOCOPY);
            netconn_write(conn, favicon_ico_start, favicon_ico_end - favicon_ico_start, NETCONN_NOCOPY);
        }
        // Peso actual (texto plano)
        ptr = strstr(buf, "GET /peso");
        if (ptr != NULL) {
            char resp[96];
            int n = snprintf(resp, sizeof(resp),
                             "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n%.2f",
                             peso_global);
            netconn_write(conn, resp, n, NETCONN_NOCOPY);
        }
        // Batería actual (texto plano)
        ptr = strstr(buf, "GET /bateria");
        if (ptr != NULL) {
            char resp[96];
            int n = snprintf(resp, sizeof(resp),
                             "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n%.2f",
                             g_v_bat);
            netconn_write(conn, resp, n, NETCONN_NOCOPY);
        }

        // Peso=XXX
        ptr = strstr(buf, "Peso=");
        if (ptr != NULL) {
            int P;
            if (sscanf(ptr, "Peso=%d", &P) == 1) {
                peso_deseado = P;
                printf("Objetivo de peso: %d g\n", P);
            }
        }
        // PesoReferencia=YYY
        ptr = strstr(buf, "PesoReferencia=");
        if (ptr != NULL) {
            int pref;
            if (sscanf(ptr, "PesoReferencia=%d", &pref) == 1) {
                peso_referencia     = (float)pref;
                lecturaDeReferencia = lectura_ajustada_global;
                calibrado_con_peso  = (lecturaDeReferencia != 0);
                printf("*** Peso de referencia registrado: %.2f g, cuentas: %ld ***\n",
                       peso_referencia, (long)lecturaDeReferencia);
            }
        }
        // Tara
        ptr = strstr(buf, "Tara");
        if (ptr != NULL) {
            offset_bascula = lectura_cruda_global;
            printf("*** Tara hecha ***\n");
        }
        // Calibración offset promedio
        ptr = strstr(buf, "Calibracion");
        if (ptr != NULL) {
            calibrar_bascula();
        }
    }

    netconn_close(conn);
    if (inbuf) netbuf_delete(inbuf);
}

static void http_server(void *arg)
{
    struct netconn *conn = netconn_new(NETCONN_TCP);
    if (!conn) {
        ESP_LOGE(TAG, "No se pudo crear netconn");
        vTaskDelete(NULL);
        return;
    }
    netconn_bind(conn, IP_ADDR_ANY, 80);
    netconn_listen(conn);

    while (1) {
        struct netconn *newconn;
        err_t err = netconn_accept(conn, &newconn);
        if (err == ERR_OK) {
            ResponderConexion(newconn);
            netconn_delete(newconn);
        }
    }

    netconn_close(conn);
    netconn_delete(conn);
}

// ====================================================================
//                              APP MAIN
// ====================================================================
void app_main(void)
{
    // [0] Núcleo del stack
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // [1] SOLO AP
    esp_netif_create_default_wifi_ap();

    // [2] Inicialización WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Registrar eventos de AP (opcional, solo para log)
    esp_event_handler_instance_t wifi_any;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &wifi_any));

    // [3] Config AP
    wifi_config_t ap_config = {
        .ap = {
            .ssid           = "NutriGo_AP",
            .ssid_len       = 8,
            .channel        = 1,
            .password       = "12345678",
            .max_connection = 8,
            .authmode       = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    // [4] Modo solo AP
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // [5] Info AP
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(ap_netif, &ip_info);
        uint8_t ap_mac[6];
        esp_wifi_get_mac(ESP_IF_WIFI_AP, ap_mac);

        printf("\nAP listo\n");
        printf("IP AP:      " IPSTR "\n", IP2STR(&ip_info.ip));
        printf("Netmask AP: " IPSTR "\n", IP2STR(&ip_info.netmask));
        printf("Gateway AP: " IPSTR "\n", IP2STR(&ip_info.gw));
        printf("MAC AP: %02X:%02X:%02X:%02X:%02X:%02X\n",
               ap_mac[0], ap_mac[1], ap_mac[2], ap_mac[3], ap_mac[4], ap_mac[5]);
        printf("SSID: %s, PASS: %s, Canal: %d\n",
               (char *)ap_config.ap.ssid, (char *)ap_config.ap.password, ap_config.ap.channel);
    }

    // [6] Tareas
    xTaskCreate(http_server,  "http_server",  4096, NULL, 5, NULL);
    xTaskCreate(task_hx711,   "task_hx711",   4096, NULL, 5, NULL);
    xTaskCreate(bateria_task, "bateria_task", 2048, NULL, 5, NULL);

    // [7] Inicialización y calibración
    hx711_init();
    calibrar_bascula();

    // [8] Bucle
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
