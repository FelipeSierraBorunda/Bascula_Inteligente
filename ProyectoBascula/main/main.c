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
#include "nvs.h"
#include "nvs_flash.h"

#include "driver/gpio.h"
#include "driver/adc.h"

#include "lwip/api.h"
#include "lwip/err.h"
#include "esp_timer.h"

#include "esp_heap_caps.h"


// ============================== Pines ===============================

#define DOUT_PIN     4   // HX711 DOUT
#define PD_SCK_PIN   3   // HX711 SCK

// ============================ Globales ==============================

static const char *TAG = "BASCULA";

volatile float tara_g = 0.0f;   // tara en gramos (cero del usuario)
static int32_t offset_bascula      = 0;
static float   peso_referencia     = 200.59f;
static int32_t lecturaDeReferencia = 81000;
static bool    calibrado_con_peso  = false;

volatile int32_t lectura_cruda_global    = 0;
volatile int32_t lectura_ajustada_global = 0;
volatile float   peso_global             = 0.0f;
volatile float   peso_filtrado_global    = 0.0f;


// ===== Variables para diagnóstico =====
// ===== Filtro por zona muerta (deadband) para el peso mostrado =====
static float ruido_base_g     = 0.3f;  // ruido típico (puedes ajustarlo luego)
static float margen_cambio_g  = 0.2f;  // margen adicional que tú aceptas
static bool  peso_init        = false; // para inicializar el peso filtrado

#define N_VENTANA  10  // número de muestras en la ventana (ajustable)
float ruido_actual = 0.0f;       
bool  estable_actual = false;    // indica si el peso está estable
// ===== Ventana deslizante para medir ruido y estabilidad =====


// Arreglo circular con las últimas N_VENTANA lecturas de peso (en gramos)
static float ventana_pesos[N_VENTANA] = {0};

// Índice donde se escribirá la siguiente muestra
static int indice_ventana = 0;

// Cuántas muestras válidas hay actualmente (al inicio puede ser < N_VENTANA)
static int num_validos_ventana = 0;

// Umbrales en gramos para decidir estabilidad (ajustables)
static const float UMBRAL_RUIDO = 0.5f;   // ruido máximo permitido (max-min) para decir "estable"
static const float UMBRAL_DELTA = 0.3f;   // distancia máxima entre lectura actual y promedio

int   clientes_conectados = 0;   // clientes conectados al AP
volatile int peso_deseado = 0;

// ========================= Batería (ADC) ============================

#define NIVEL_BATERIA_CH   ADC1_CHANNEL_0
#define NIVEL_BATERIA_GPIO GPIO_NUM_0

#define ADC_MAX_COUNTS   2580.0f
#define ADC_VREF_CAL     1.886f
#define Factor           1.99f

volatile float g_v_bat    = 0.0f;
volatile int   g_bat_raw  = 0;
volatile float g_v_adc    = 0.0f;
volatile float g_nivel_bat = 0.0f;

// ============================= Alimentos ============================

char alimento_nombre[32] = {0};
int  alimento_grasa      = 0;
int  alimento_prote      = 0;
int  alimento_carb       = 0;
int  alimento_kcal       = 0;

// ======================= Memoria de alimentos día =====================

#define MAX_ALIMENTOS_POR_DIA 50

typedef struct {
    char  nombre[32];
    float gramos;
    float grasa;
    float prote;
    float carb;
    float kcal;
} Alimento_t;

typedef struct {
    int         num_items;
    Alimento_t  items[MAX_ALIMENTOS_POR_DIA];
    float       total_grasa;
    float       total_prote;
    float       total_carb;
    float       total_kcal;
} MacrosDiarios_data_t;

static MacrosDiarios_data_t BaseDeDatosAlimentos;
// ======================= Datos del usuario  =====================
// ======================= Perfil de usuario ============================
typedef struct {
    char  nombre[32];     
    int   edad;          
    float altura_cm;      
    char  sexo;           

    char  objetivo[12];   // "definicion", "mantener", "volumen"
    float peso_kg; 
    float meta_kcal;      
} PerfilUsuario_t;

// Variable global del perfil
static PerfilUsuario_t PerfilUsuario;


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

extern const uint8_t LEDsPage_html_start[]   asm("_binary_LEDsPage_html_start");
extern const uint8_t LEDsPage_html_end[]     asm("_binary_LEDsPage_html_end");
extern const uint8_t admin_html_start[]      asm("_binary_admin_html_start");
extern const uint8_t admin_html_end[]        asm("_binary_admin_html_end");
extern const uint8_t favicon_ico_start[]     asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[]       asm("_binary_favicon_ico_end");
extern const uint8_t alimentos_html_start[]  asm("_binary_alimentos_html_start");
extern const uint8_t alimentos_html_end[]    asm("_binary_alimentos_html_end");
extern const uint8_t Personal_html_start[]   asm("_binary_Personal_html_start");
extern const uint8_t Personal_html_end[]     asm("_binary_Personal_html_end");

// ====================================================================
//                 Gestión de memoria de calorías (RAM + NVS)
// ====================================================================
static void PerfilUsuario_init_defaults(void)
{
    memset(&PerfilUsuario, 0, sizeof(PerfilUsuario));
    snprintf(PerfilUsuario.nombre, sizeof(PerfilUsuario.nombre), "Usuario");
    PerfilUsuario.edad       = 0;
    PerfilUsuario.altura_cm  = 0.0f;
    PerfilUsuario.sexo       = 'O';
    snprintf(PerfilUsuario.objetivo, sizeof(PerfilUsuario.objetivo),
             "mantener");
    PerfilUsuario.meta_kcal  = 2600.0f;
    PerfilUsuario.peso_kg    = 0.0f;
}

// ----------------------------------------------------------
// Cálculo de meta_kcal con fórmula tipo Mifflin-St Jeor
// y ajuste según objetivo
// ----------------------------------------------------------
static float PerfilUsuario_calcular_meta_kcal(void)
{
    // Si faltan datos, regresamos un valor “seguro”
    if (PerfilUsuario.peso_kg <= 0.0f ||
        PerfilUsuario.altura_cm <= 0.0f ||
        PerfilUsuario.edad <= 0)
    {
        return 2600.0f;
    }

    // Tasa metabólica basal (TMB)
    float tmb;
    if (PerfilUsuario.sexo == 'M') {
        tmb = 10.0f * PerfilUsuario.peso_kg +
              6.25f * PerfilUsuario.altura_cm -
              5.0f * (float)PerfilUsuario.edad + 5.0f;
    } else {
        // F, O, o cualquier otro → usamos rama “no masculino”
        tmb = 10.0f * PerfilUsuario.peso_kg +
              6.25f * PerfilUsuario.altura_cm -
              5.0f * (float)PerfilUsuario.edad - 161.0f;
    }

    // Actividad ligera por defecto
    float factor_actividad = 1.40f;
    float mantenimiento = tmb * factor_actividad;

    // Ajuste según objetivo
    if (strcmp(PerfilUsuario.objetivo, "definicion") == 0) {
        return mantenimiento * 0.85f;   // -15 %
    } else if (strcmp(PerfilUsuario.objetivo, "volumen") == 0) {
        return mantenimiento * 1.10f;   // +10 %
    } else {
        // "mantener" o cualquier otro
        return mantenimiento;
    }
}

static void MacrosBaseDatos_init_struct(void)
{
    memset(&BaseDeDatosAlimentos, 0, sizeof(BaseDeDatosAlimentos));
}

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

    int espera_ms = 0;
    while (gpio_get_level(DOUT_PIN) == 1) {
        vTaskDelay(pdMS_TO_TICKS(1));
        espera_ms++;
        if (espera_ms >= 500) {
            printf("ERROR: Timeout esperando al HX711 (DOUT no baja)\n");
            return -1;
        }
    }

    for (int i = 0; i < 24; i++) {
        gpio_set_level(PD_SCK_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        data <<= 1;
        if (gpio_get_level(DOUT_PIN)) {
            data |= 1;
        }
        gpio_set_level(PD_SCK_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    gpio_set_level(PD_SCK_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(PD_SCK_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    if (data & 0x800000) {
        data |= 0xFF000000;
    }

    return data;
}

static void calibrar_bascula(void)
{
    int64_t suma           = 0;
    int     lecturas_validas = 0;

    printf("\nIniciando calibración...\n");

    for (int i = 0; i < 10; i++) {
        int32_t lectura = hx711_read_raw_24bit();

        if (lectura == -1) {
            printf(" Lectura %d: invalida (timeout HX711)\n", i + 1);
        } else {
            suma += lectura;
            lecturas_validas++;
            printf(" Lectura %d: %ld\n", i + 1, (long)lectura);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (lecturas_validas == 0) {
        printf("\nERROR: No se pudo calibrar, ninguna lectura valida del HX711.\n");
        return;
    }

    offset_bascula      = (int32_t)(suma / lecturas_validas);
    calibrado_con_peso  = true;

    printf("\nCalibración completada. Offset (cero): %ld (promedio de %d lecturas)\n",
           (long)offset_bascula, lecturas_validas);
}
// ====================================================================
//           Cálculo de ruido_actual y estable_actual (HX711)
// ====================================================================
static void actualizar_ruido_y_estabilidad(float peso_instantaneo)
{
    // 1) Insertar la nueva muestra en la ventana circular
    ventana_pesos[indice_ventana] = peso_instantaneo;
    indice_ventana++;
    if (indice_ventana >= N_VENTANA) {
        indice_ventana = 0;
    }

    if (num_validos_ventana < N_VENTANA) {
        num_validos_ventana++;
    }

    // 2) Si aún no tenemos suficientes muestras, marcamos como no estable
    if (num_validos_ventana < N_VENTANA) {
        ruido_actual   = 9999.0f;   // ruido no confiable
        estable_actual = false;
        // OJO: ya NO tocamos peso_filtrado_global aquí
        return;
    }

    // 3) Calcular min, max y promedio dentro de la ventana
    float suma = 0.0f;
    float min  =  1e9f;
    float max  = -1e9f;

    for (int i = 0; i < N_VENTANA; i++) {
        float x = ventana_pesos[i];
        suma += x;
        if (x < min) min = x;
        if (x > max) max = x;
    }

    float promedio = suma / (float)N_VENTANA;

    // 4) Calcular el ruido como (max - min)
    float ruido = max - min;
    ruido_actual = ruido;

    // 5) Evaluar estabilidad
    float delta = peso_instantaneo - promedio;
    if (delta < 0.0f) {
        delta = -delta;  // valor absoluto
    }

    if ((ruido_actual <= UMBRAL_RUIDO) && (delta <= UMBRAL_DELTA)) {
        estable_actual = true;
    } else {
        estable_actual = false;
    }

    // Importante: AQUÍ YA NO tocamos peso_filtrado_global.
}
// ====================================================================
//        Actualiza peso_filtrado_global usando una zona muerta
//        El peso solo cambia si Δ > (ruido_base_g + margen_cambio_g)
// ====================================================================
static void actualizar_peso_zona_muerta(float peso_instantaneo)
{
    float umbral = ruido_base_g + margen_cambio_g;  // ej. 0.3 + 0.5 = 0.8 g

    if (!peso_init) {
        // Primera vez: inicializamos al valor actual
        peso_filtrado_global = peso_instantaneo;
        peso_init = true;
        return;
    }

    float delta = peso_instantaneo - peso_filtrado_global;
    if (delta < 0.0f) {
        delta = -delta;  // valor absoluto
    }

    if (delta >= umbral) {
        // Cambio REAL: actualizamos el peso mostrado
        peso_filtrado_global = peso_instantaneo;
    } 
    else {
        // Cambio pequeño: se considera ruido, mantenemos el peso mostrado
        // No hacemos nada.
    }
}

static void task_hx711(void *arg)
{
    int lectura_num = 0;

    while (1) {
        int32_t lectura_cruda = hx711_read_raw_24bit();

        if (lectura_cruda == -1) {
            printf("Lectura HX711 invalida (-1), se ignora.\n");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        lectura_num++;

        int32_t lectura_ajustada = lectura_cruda - offset_bascula;

        float peso_calculado = 0.0f;
        if (calibrado_con_peso && lecturaDeReferencia != 0) {
            peso_calculado = ((float)lectura_ajustada * peso_referencia) /
                            (float)lecturaDeReferencia;
        }

        // Aplicar tara en gramos
        float peso_tareado = peso_calculado - tara_g;

        // Guardar la versión tareada como "peso_global"
        lectura_cruda_global    = lectura_cruda;
        lectura_ajustada_global = lectura_ajustada;
        peso_global             = peso_tareado;

        // Todo lo demás trabaja con el peso tareado
        actualizar_ruido_y_estabilidad(peso_tareado);
        actualizar_peso_zona_muerta(peso_tareado);


        printf("\n=========================================\n");
        printf(" Lectura # %d\n", lectura_num);
        printf(" Crudo:    %ld\n", (long)lectura_cruda);
        printf(" Ajustado: %ld\n", (long)lectura_ajustada);
        printf(" Peso:     %.2f g\n", peso_calculado);
        printf(" Peso filt: %.2f g\n", peso_filtrado_global);
        printf("=========================================\n");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void bateria_task(void *arg)
{
    bateria_adc_init();

    while (1) {
        int raw = adc1_get_raw(NIVEL_BATERIA_CH);

        float v_adc = ((float)raw / ADC_MAX_COUNTS) * ADC_VREF_CAL;
        float v_bat = Factor * v_adc;

        g_bat_raw = raw;
        g_v_adc   = v_adc;
        g_v_bat   = v_bat;

        if      (raw >= 2893) { g_nivel_bat = 5; }
        else if (raw >= 2865) { g_nivel_bat = 4; }
        else if (raw >= 2740) { g_nivel_bat = 3; }
        else if (raw >= 2650) { g_nivel_bat = 2; }
        else                  { g_nivel_bat = 1; }

        printf("RAW: %d | Vadc: %.3f V | Vbat: %.3f V\n",
               raw, v_adc, v_bat);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
// ==========================================================
//   Decodifica %20, %xx y +  →  texto normal
// ==========================================================
static void url_decode(char *s)
{
    char *p = s;  // lectura
    char *o = s;  // escritura

    while (*p) {
        if (*p == '%' && p[1] && p[2]) {
            // convertir %XX hex a char
            char hex[3] = { p[1], p[2], 0 };
            *o++ = (char) strtol(hex, NULL, 16);
            p += 3;
        }
        else if (*p == '+') {
            *o++ = ' ';
            p++;
        }
        else {
            *o++ = *p++;
        }
    }

    *o = '\0';
}

// ====================================================================
//                          NVS – Macros diarios
// ====================================================================

static void Abrir_MacrosBaseDatos_desde_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t    err;

    err = nvs_open("nutrigo", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        printf("NVS: no se pudo abrir namespace 'nutrigo' (%s)\n",
               esp_err_to_name(err));
        MacrosBaseDatos_init_struct();
        return;
    }

    size_t required_size = sizeof(BaseDeDatosAlimentos);
    err = nvs_get_blob(handle,
                       "macros_diarios",
                       &BaseDeDatosAlimentos,
                       &required_size);

    if (err == ESP_OK && required_size == sizeof(BaseDeDatosAlimentos)) {
        printf("NVS: macros cargados (items=%d, kcal_totales=%.1f)\n",
               BaseDeDatosAlimentos.num_items,
               BaseDeDatosAlimentos.total_kcal);
    } else {
        printf("NVS: sin datos previos de macros, inicializando...\n");
        MacrosBaseDatos_init_struct();
    }

    nvs_close(handle);
}

static void MacrosBaseDatos_guardar_en_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t    err;

    err = nvs_open("nutrigo", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        printf("NVS: no se pudo abrir namespace 'nutrigo' para guardar (%s)\n",
               esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(handle,
                       "macros_diarios",
                       &BaseDeDatosAlimentos,
                       sizeof(BaseDeDatosAlimentos));
    if (err != ESP_OK) {
        printf("NVS: error en nvs_set_blob(macros_diarios): %s\n",
               esp_err_to_name(err));
        nvs_close(handle);
        return;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        printf("NVS: error en nvs_commit: %s\n", esp_err_to_name(err));
    } else {
        printf("NVS: macros guardados (items=%d, kcal_totales=%.1f)\n",
               BaseDeDatosAlimentos.num_items,
               BaseDeDatosAlimentos.total_kcal);
    }

    nvs_close(handle);
}

static void MacrosBaseDatos_agregar_item_desde_actual(void)
{
    if (alimento_nombre[0] == '\0') {
        printf("No hay alimento seleccionado, no se agrega.\n");
        return;
    }

    if (peso_filtrado_global <= 0.0f) {
        printf("Peso actual <= 0, no se agrega.\n");
        return;
    }

    if (BaseDeDatosAlimentos.num_items >= MAX_ALIMENTOS_POR_DIA) {
        printf("Base de datos llena (%d items), no se agrega.\n",
               MAX_ALIMENTOS_POR_DIA);
        return;
    }

    float gramos  = peso_filtrado_global;
    float grasa_f = (alimento_grasa * gramos) / 100.0f;
    float prote_f = (alimento_prote * gramos) / 100.0f;
    float carb_f  = (alimento_carb  * gramos) / 100.0f;
    float kcal_f  = (alimento_kcal  * gramos) / 100.0f;

    Alimento_t *item = &BaseDeDatosAlimentos.items[BaseDeDatosAlimentos.num_items];

    snprintf(item->nombre, sizeof(item->nombre), "%s", alimento_nombre);
    item->gramos = gramos;
    item->grasa  = grasa_f;
    item->prote  = prote_f;
    item->carb   = carb_f;
    item->kcal   = kcal_f;

    BaseDeDatosAlimentos.num_items++;

    BaseDeDatosAlimentos.total_grasa += grasa_f;
    BaseDeDatosAlimentos.total_prote += prote_f;
    BaseDeDatosAlimentos.total_carb  += carb_f;
    BaseDeDatosAlimentos.total_kcal  += kcal_f;

    printf("\n=== ALIMENTO AGREGADO A BASE DE DATOS ===\n");
    printf("Nombre: %s\n", item->nombre);
    printf("g: %.1f, grasa: %.1f, prote: %.1f, carb: %.1f, kcal: %.1f\n",
           item->gramos, item->grasa, item->prote, item->carb, item->kcal);
    printf("Totales -> G=%.1f, P=%.1f, C=%.1f, K=%.1f (items=%d)\n",
           BaseDeDatosAlimentos.total_grasa,
           BaseDeDatosAlimentos.total_prote,
           BaseDeDatosAlimentos.total_carb,
           BaseDeDatosAlimentos.total_kcal,
           BaseDeDatosAlimentos.num_items);

    MacrosBaseDatos_guardar_en_nvs();
}
static void MacrosBaseDatos_agregar_item_negativo(void)
{
    if (alimento_nombre[0] == '\0') {
        printf("No hay alimento seleccionado, no se agrega.\n");
        return;
    }

    // Necesitamos un peso NEGATIVO (jarra desde tara)
    if (peso_filtrado_global >= 0.0f) {
        printf("Modo jalea: peso no negativo (%.2f g), no se agrega.\n", peso_filtrado_global);
        return;
    }

    if (BaseDeDatosAlimentos.num_items >= MAX_ALIMENTOS_POR_DIA) {
        printf("Base de datos llena (%d items), no se agrega.\n",
               MAX_ALIMENTOS_POR_DIA);
        return;
    }

    // Lo que comiste es el valor absoluto del peso negativo
    float gramos = -peso_filtrado_global;

    float grasa_f = (alimento_grasa * gramos) / 100.0f;
    float prote_f = (alimento_prote * gramos) / 100.0f;
    float carb_f  = (alimento_carb  * gramos) / 100.0f;
    float kcal_f  = (alimento_kcal  * gramos) / 100.0f;

    Alimento_t *item = &BaseDeDatosAlimentos.items[BaseDeDatosAlimentos.num_items];

    // Puedes diferenciar en el nombre si quieres, por ahora uso el mismo
    snprintf(item->nombre, sizeof(item->nombre), "%s", alimento_nombre);
    item->gramos = gramos;
    item->grasa  = grasa_f;
    item->prote  = prote_f;
    item->carb   = carb_f;
    item->kcal   = kcal_f;

    BaseDeDatosAlimentos.num_items++;

    BaseDeDatosAlimentos.total_grasa += grasa_f;
    BaseDeDatosAlimentos.total_prote += prote_f;
    BaseDeDatosAlimentos.total_carb  += carb_f;
    BaseDeDatosAlimentos.total_kcal  += kcal_f;

    printf("\n=== ALIMENTO (MODO JALEA) AGREGADO ===\n");
    printf("Nombre: %s\n", item->nombre);
    printf("g consumidos: %.1f, grasa: %.1f, prote: %.1f, carb: %.1f, kcal: %.1f\n",
           item->gramos, item->grasa, item->prote, item->carb, item->kcal);
    printf("Totales -> G=%.1f, P=%.1f, C=%.1f, K=%.1f (items=%d)\n",
           BaseDeDatosAlimentos.total_grasa,
           BaseDeDatosAlimentos.total_prote,
           BaseDeDatosAlimentos.total_carb,
           BaseDeDatosAlimentos.total_kcal,
           BaseDeDatosAlimentos.num_items);

    MacrosBaseDatos_guardar_en_nvs();

    // IMPORTANTÍSIMO: ahora queremos que el frasco, tal como quedó,
    // vuelva a marcar ~0 g → misma lógica de tara incremental que ya usamos:
    tara_g += peso_filtrado_global;    // ojo: peso_filtrado_global es negativo

    // Reiniciamos filtros para que se asiente otra vez
    peso_init           = false;
    num_validos_ventana = 0;
    for (int i = 0; i < N_VENTANA; i++) {
        ventana_pesos[i] = 0.0f;
    }

    printf("*** Modo jalea: registrados %.1f g consumidos y frasco re-tarado ***\n",
           gramos);
}
static void Abrir_PerfilUsuario_desde_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err;

    // Abrir el mismo namespace que usas para macros: "nutrigo"
    err = nvs_open("nutrigo", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        printf("NVS: no se pudo abrir namespace 'nutrigo' para perfil (%s)\n",
               esp_err_to_name(err));
        PerfilUsuario_init_defaults();
        return;
    }

    size_t required_size = sizeof(PerfilUsuario);
    err = nvs_get_blob(handle,
                       "perfil_usuario",      // clave en NVS
                       &PerfilUsuario,
                       &required_size);

    if (err == ESP_OK && required_size == sizeof(PerfilUsuario)) {
        // Se leyó un perfil válido
        printf("NVS: perfil cargado. Nombre=%s, objetivo=%s, meta_kcal=%.1f\n",
               PerfilUsuario.nombre,
               PerfilUsuario.objetivo,
               PerfilUsuario.meta_kcal);
    } else {
        // No hay perfil guardado o tamaño distinto → usar defaults
        printf("NVS: sin datos previos de perfil, usando valores por defecto.\n");
        PerfilUsuario_init_defaults();
    }

    nvs_close(handle);
}

static void PerfilUsuario_guardar_en_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err;

    // Abrimos el mismo namespace "nutrigo"
    err = nvs_open("nutrigo", NVS_READWRITE, &handle);
    if (err != ESP_OK) 
    {
        printf("NVS: no se pudo abrir 'nutrigo' para guardar perfil (%s)\n",
               esp_err_to_name(err));
        return;
    }

    // Guardamos toda la estructura PerfilUsuario como un blob
    err = nvs_set_blob(handle,
                       "perfil_usuario",    // clave
                       &PerfilUsuario,     // datos
                       sizeof(PerfilUsuario)); // tamaño
    if (err != ESP_OK) 
    {
        printf("NVS: error en nvs_set_blob(perfil_usuario): %s\n",
               esp_err_to_name(err));
        nvs_close(handle);
        return;
    }

    // Confirmamos escritura
    err = nvs_commit(handle);
    if (err != ESP_OK) 
    {
        printf("NVS: error en nvs_commit (perfil): %s\n",
               esp_err_to_name(err));
    } 
    else 
    {
       printf("\n===== PERFIL USUARIO (DEBUG) =====\n");
        printf("Nombre        : %s\n", PerfilUsuario.nombre);
        printf("Edad          : %d\n", PerfilUsuario.edad);
        printf("Altura (cm)   : %.1f\n", PerfilUsuario.altura_cm);
        printf("Peso (kg)     : %.1f\n", PerfilUsuario.peso_kg);
        printf("Sexo          : %c\n", PerfilUsuario.sexo);
        printf("Objetivo      : %s\n", PerfilUsuario.objetivo);
        printf("Meta kcal     : %.1f\n", PerfilUsuario.meta_kcal);
        printf("====================================\n");

    }

    nvs_close(handle);
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
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *evt = (wifi_event_ap_stadisconnected_t *)data;
        printf("\nCliente desconectado del AP. MAC: %02X:%02X:%02X:%02X:%02X:%02X  AID:%d\n",
               evt->mac[0], evt->mac[1], evt->mac[2],
               evt->mac[3], evt->mac[4], evt->mac[5], evt->aid);
    }
}

static void ResponderConexion(struct netconn *conn)
{
    struct netbuf *inbuf;
    char          *buf;
    u16_t          buflen;

    err_t err = netconn_recv(conn, &inbuf);
    if (err == ERR_OK) {
        netbuf_data(inbuf, (void **)&buf, &buflen);
        char *ptr;

        ptr = strstr(buf, "GET / HTTP");
        if (ptr != NULL) {
            printf("GET /\n");
            netconn_write(conn, HTML_Header, sizeof(HTML_Header) - 1, NETCONN_NOCOPY);
            netconn_write(conn, LEDsPage_html_start,
                          LEDsPage_html_end - LEDsPage_html_start, NETCONN_NOCOPY);
        }

        ptr = strstr(buf, "GET /admin HTTP");
        if (ptr != NULL) {
            printf("GET /admin\n");
            netconn_write(conn, HTML_Header, sizeof(HTML_Header) - 1, NETCONN_NOCOPY);
            netconn_write(conn, admin_html_start,
                          admin_html_end - admin_html_start, NETCONN_NOCOPY);
        }

        ptr = strstr(buf, "GET /LEDsPage HTTP");
        if (ptr != NULL) {
            printf("GET /LEDsPage\n");
            netconn_write(conn, HTML_Header, sizeof(HTML_Header) - 1, NETCONN_NOCOPY);
            netconn_write(conn, LEDsPage_html_start,
                          LEDsPage_html_end - LEDsPage_html_start, NETCONN_NOCOPY);
        }

        ptr = strstr(buf, "GET /alimentos HTTP");
        if (ptr != NULL) {
            printf("GET /alimentos\n");
            netconn_write(conn, HTML_Header, sizeof(HTML_Header) - 1, NETCONN_NOCOPY);
            netconn_write(conn, alimentos_html_start,
                          alimentos_html_end - alimentos_html_start, NETCONN_NOCOPY);
        }

        ptr = strstr(buf, "GET /Personal HTTP");
        if (ptr != NULL) {
            printf("GET /Personal\n");
            netconn_write(conn, HTML_Header, sizeof(HTML_Header) - 1, NETCONN_NOCOPY);
            netconn_write(conn, Personal_html_start,
                          Personal_html_end - Personal_html_start, NETCONN_NOCOPY);
        }

        ptr = strstr(buf, "GET /favicon.ico");
        if (ptr != NULL) {
            printf("GET /favicon.ico\n");
            netconn_write(conn, icoIMG_Header, sizeof(icoIMG_Header) - 1, NETCONN_NOCOPY);
            netconn_write(conn, favicon_ico_start,
                          favicon_ico_end - favicon_ico_start, NETCONN_NOCOPY);
        }

       ptr = strstr(buf, "GET /peso");
        if (ptr != NULL) {
            char resp[96];

            float p = peso_filtrado_global;

            // Zona muerta visual alrededor de cero
            if (p > -0.5f && p < 0.5f) {   // ±0.5 g de tolerancia
                p = 0.0f;
            }

            int n = snprintf(resp, sizeof(resp),
                            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n%.2f",
                            p);
            netconn_write(conn, resp, n, NETCONN_NOCOPY);
        }


        ptr = strstr(buf, "GET /bateria_raw");
        if (ptr != NULL) {
            char resp[64];
            int  n = snprintf(resp, sizeof(resp),
                              "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n%d",
                              g_bat_raw);
            netconn_write(conn, resp, n, NETCONN_NOCOPY);
        }

        ptr = strstr(buf, "GET /bateria_vadc");
        if (ptr != NULL) {
            char resp[64];
            int  n = snprintf(resp, sizeof(resp),
                              "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n%.3f",
                              g_v_adc);
            netconn_write(conn, resp, n, NETCONN_NOCOPY);
        }

        ptr = strstr(buf, "GET /bateria_vbat");
        if (ptr != NULL) {
            char resp[64];
            int  n = snprintf(resp, sizeof(resp),
                              "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n%.3f",
                              g_v_bat);
            netconn_write(conn, resp, n, NETCONN_NOCOPY);
        }

        ptr = strstr(buf, "GET /NivelBateria");
        if (ptr != NULL) {
            char resp[64];
            int  n = snprintf(resp, sizeof(resp),
                              "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n%.3f",
                              g_nivel_bat);
            netconn_write(conn, resp, n, NETCONN_NOCOPY);
        }

        ptr = strstr(buf, "Peso=");
        if (ptr != NULL) {
            int P;
            if (sscanf(ptr, "Peso=%d", &P) == 1) {
                peso_deseado = P;
                printf("Objetivo de peso: %d g\n", P);
            }
        }

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

        ptr = strstr(buf, "GET /Tara ");
        if (ptr == NULL) {
            ptr = strstr(buf, "POST /Tara ");
        }
        if (ptr != NULL) {
              tara_g += peso_filtrado_global;

            // Reiniciamos filtros para que la lectura se asiente de nuevo
            peso_init            = false;
            num_validos_ventana  = 0;
            printf("*** Tara hecha: tara_g = %.2f g ***\n", tara_g);

            const char resp_tara[] =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n";

            netconn_write(conn, resp_tara, sizeof(resp_tara) - 1, NETCONN_NOCOPY);

            netconn_close(conn);
            if (inbuf) {
                netbuf_delete(inbuf);
            }
            return;
        }

        ptr = strstr(buf, "GET /GuardarAlimento ");
        if (ptr == NULL) {
            ptr = strstr(buf, "POST /GuardarAlimento ");
        }
        if (ptr != NULL) {
            MacrosBaseDatos_agregar_item_desde_actual();

            printf("Se ha guardado el alimento\n");

            const char resp_guardar[] =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n";

            netconn_write(conn, resp_guardar, sizeof(resp_guardar) - 1, NETCONN_NOCOPY);

            netconn_close(conn);
            if (inbuf) {
                netbuf_delete(inbuf);
            }
            return;
        }

                ptr = strstr(buf, "GET /ConsumirNegativo ");
        if (ptr == NULL) {
            ptr = strstr(buf, "POST /ConsumirNegativo ");
        }
        if (ptr != NULL) {
            MacrosBaseDatos_agregar_item_negativo();

            const char resp_consumir[] =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n";

            netconn_write(conn, resp_consumir, sizeof(resp_consumir) - 1, NETCONN_NOCOPY);

            netconn_close(conn);
            if (inbuf) {
                netbuf_delete(inbuf);
            }
            return;
        }


        ptr = strstr(buf, "Calibracion");
        if (ptr != NULL) {
            calibrar_bascula();
        }

        ptr = strstr(buf, "GET /setAlimento?");
        if (ptr != NULL) {
            char *n = strstr(ptr, "nombre=");
            char *g = strstr(ptr, "g=");
            char *p = strstr(ptr, "p=");
            char *c = strstr(ptr, "c=");
            char *k = strstr(ptr, "k=");

            if (n != NULL) {
                n += 7;
                int i = 0;
                while (n[i] != '&' &&
                       n[i] != ' ' &&
                       n[i] != '\n' &&
                       n[i] != '\r' &&
                       i < (int)sizeof(alimento_nombre) - 1) {
                    alimento_nombre[i] = n[i];
                    i++;
                }
                alimento_nombre[i] = '\0';
                url_decode(alimento_nombre); 
            }

            if (g != NULL) {
                alimento_grasa = atoi(g + 2);
            }

            if (p != NULL) {
                alimento_prote = atoi(p + 2);
            }

            if (c != NULL) {
                alimento_carb = atoi(c + 2);
            }

            if (k != NULL) {
                alimento_kcal = atoi(k + 2);
            }

            printf("\n*** ALIMENTO RECIBIDO ***\n");
            printf("Nombre: %s\n", alimento_nombre);
            printf("Grasa:  %d\n", alimento_grasa);
            printf("Prote:  %d\n", alimento_prote);
            printf("Carb:   %d\n", alimento_carb);
            printf("Kcal:   %d\n", alimento_kcal);

            const char resp[] =
                "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK";
            netconn_write(conn, resp, sizeof(resp) - 1, NETCONN_NOCOPY);
        }

        ptr = strstr(buf, "GET /alimentoSeleccionado");
        if (ptr != NULL) {
            char resp[256];
            int  n;

            if (alimento_nombre[0] == '\0') {
                const char empty[] =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n\r\n"
                    "{\"valido\":false}";
                netconn_write(conn, empty, sizeof(empty) - 1, NETCONN_NOCOPY);
            } else {
                float gramos  = peso_global;
                float grasa_f = (alimento_grasa * gramos) / 100.0f;
                float prote_f = (alimento_prote * gramos) / 100.0f;
                float carb_f  = (alimento_carb  * gramos) / 100.0f;
                float kcal_f  = (alimento_kcal  * gramos) / 100.0f;

                int grasa = (int)(grasa_f + 0.5f);
                int prote = (int)(prote_f + 0.5f);
                int carb  = (int)(carb_f  + 0.5f);
                int kcal  = (int)(kcal_f  + 0.5f);

                n = snprintf(resp, sizeof(resp),
                             "HTTP/1.1 200 OK\r\n"
                             "Content-Type: application/json\r\n\r\n"
                             "{"
                               "\"valido\":true,"
                               "\"nombre\":\"%s\","
                               "\"grasa\":%d,"
                               "\"prote\":%d,"
                               "\"carb\":%d,"
                               "\"kcal\":%d"
                             "}",
                             alimento_nombre,
                             grasa,
                             prote,
                             carb,
                             kcal);

                netconn_write(conn, resp, n, NETCONN_NOCOPY);
            }
        }

        ptr = strstr(buf, "GET /caloriasDiarias");
        if (ptr != NULL) {
            char resp[256];
            int  n = snprintf(resp, sizeof(resp),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/json\r\n\r\n"
                              "{"
                                "\"items\":%d,"
                                "\"grasa\":%.2f,"
                                "\"prote\":%.2f,"
                                "\"carb\":%.2f,"
                                "\"kcal\":%.2f"
                              "}",
                              BaseDeDatosAlimentos.num_items,
                              BaseDeDatosAlimentos.total_grasa,
                              BaseDeDatosAlimentos.total_prote,
                              BaseDeDatosAlimentos.total_carb,
                              BaseDeDatosAlimentos.total_kcal);

            netconn_write(conn, resp, n, NETCONN_NOCOPY);
        }
        // ======================================
        //  /perfilUsuario  
        // ======================================
       ptr = strstr(buf, "GET /setPerfilUsuario?");
        if (ptr != NULL) {
            char *n = strstr(ptr, "nombre=");
            char *e = strstr(ptr, "edad=");
            char *h = strstr(ptr, "altura=");
            char *s = strstr(ptr, "sexo=");
            char *o = strstr(ptr, "objetivo=");
            char *w = strstr(ptr, "peso=");          // NUEVO

            // ---- nombre ----
            if (n != NULL) {
                n += 7;
                int i = 0;
                while (n[i] != '&' && n[i] != ' ' && n[i] != '\n' &&
                    n[i] != '\r' && i < (int)sizeof(PerfilUsuario.nombre) - 1)
                {
                    PerfilUsuario.nombre[i] = n[i];
                    i++;
                }
                PerfilUsuario.nombre[i] = '\0';
                url_decode(PerfilUsuario.nombre);
            }

            // ---- edad ----
            if (e != NULL) {
                PerfilUsuario.edad = atoi(e + 5);
            }

            // ---- altura ----
            if (h != NULL) {
                PerfilUsuario.altura_cm = atof(h + 7);
            }

            // ---- sexo ----
            if (s != NULL) {
                char c = *(s + 5);
                if (c == 'M' || c == 'F' || c == 'O') {
                    PerfilUsuario.sexo = c;
                }
            }

            // ---- objetivo ----
            if (o != NULL) {
                o += 9;
                int i = 0;
                while (o[i] != '&' && o[i] != ' ' && o[i] != '\n' &&
                    o[i] != '\r' && i < (int)sizeof(PerfilUsuario.objetivo) - 1)
                {
                    PerfilUsuario.objetivo[i] = o[i];
                    i++;
                }
                PerfilUsuario.objetivo[i] = '\0';
                url_decode(PerfilUsuario.objetivo);
            }
            // ---- peso_kg ----
            if (w != NULL) {
                PerfilUsuario.peso_kg = atof(w + 5);
            }
            // ---- meta_kcal: ahora se calcula, no viene de la URL ----
            PerfilUsuario.meta_kcal = PerfilUsuario_calcular_meta_kcal();

            PerfilUsuario_guardar_en_nvs();



            const char resp[] =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n\r\n"
                "OK";
            netconn_write(conn, resp, sizeof(resp)-1, NETCONN_NOCOPY);

            netconn_close(conn);
            if (inbuf) netbuf_delete(inbuf);
            return;
        }
                // ======================================
        //  GET /perfilUsuario  → devuelve JSON
        // ======================================
        ptr = strstr(buf, "GET /perfilUsuario");
        if (ptr != NULL) {
            char resp[512];
            int  n = snprintf(resp, sizeof(resp),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/json\r\n\r\n"
                              "{"
                                "\"nombre\":\"%s\","
                                "\"edad\":%d,"
                                "\"altura_cm\":%.1f,"
                                "\"peso_kg\":%.1f,"
                                "\"sexo\":\"%c\","
                                "\"objetivo\":\"%s\","
                                "\"meta_kcal\":%.1f"
                              "}",
                              PerfilUsuario.nombre,
                              PerfilUsuario.edad,
                              PerfilUsuario.altura_cm,
                              PerfilUsuario.peso_kg,
                              (PerfilUsuario.sexo ? PerfilUsuario.sexo : 'O'),
                              PerfilUsuario.objetivo,
                              PerfilUsuario.meta_kcal);

            netconn_write(conn, resp, n, NETCONN_NOCOPY);

            netconn_close(conn);
            if (inbuf) netbuf_delete(inbuf);
            return;
        }


        ptr = strstr(buf, "GET /resetCalorias");
        if (ptr != NULL) {
            MacrosBaseDatos_init_struct();
            MacrosBaseDatos_guardar_en_nvs();

            printf("RESET: Calorías del día reiniciadas.\n");

            const char resp[] =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n\r\n"
                "OK";

            netconn_write(conn, resp, sizeof(resp) - 1, NETCONN_NOCOPY);

            netconn_close(conn);
            if (inbuf) {
                netbuf_delete(inbuf);
            }
            return;
        }
     // ========================= /diagnostico =========================
      ptr = strstr(buf, "GET /diagnostico");
        if (ptr != NULL)
        {
            char resp[900];

            // ===== INFO HEAP =====
            size_t heap_actual = esp_get_free_heap_size();
            size_t heap_total  = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
            int heap_pct = (int)((heap_actual * 100) / heap_total);
            wifi_sta_list_t lista;
            esp_wifi_ap_get_sta_list(&lista);
            clientes_conectados = lista.num;


            int n = snprintf(resp, sizeof(resp),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n\r\n"
            "{"
                "\"peso_crudo\":%ld,"
                "\"peso_ajustado\":%ld,"
                "\"offset\":%ld,"
                "\"lecturaRef\":%ld,"
                "\"calibrado\":%s,"

                "\"ruido\":%.3f,"
                "\"estable\":%s,"

                "\"alimento\":\"%s\","

                "\"bateria_raw\":%d,"
                "\"bateria_vadc\":%.3f,"
                "\"bateria_vbat\":%.3f,"
                "\"bateria_nivel\":%.0f,"

                "\"clientes\":%d,"
                "\"uptime\":%lu,"

                "\"heap_actual\":%lu,"
                "\"heap_total\":%lu,"
                "\"heap_pct\":%d,"

                "\"firmware\":\"NutriGo v1.0.3\""
            "}",

            lectura_cruda_global,
            lectura_ajustada_global,
            offset_bascula,
            lecturaDeReferencia,
            calibrado_con_peso ? "true" : "false",

            ruido_actual,
            estable_actual ? "true" : "false",

            alimento_nombre,   

            g_bat_raw,
            g_v_adc,
            g_v_bat,
            g_nivel_bat,

            clientes_conectados,
            (unsigned long)(esp_timer_get_time() / 1000000ULL),

            (unsigned long)heap_actual,
            (unsigned long)heap_total,
            heap_pct
        );

        netconn_write(conn, resp, n, NETCONN_NOCOPY);

            netconn_close(conn);
            if (inbuf) netbuf_delete(inbuf);
            return;
        }


    }

    netconn_close(conn);
    if (inbuf) {
        netbuf_delete(inbuf);
    }
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
        err_t           err = netconn_accept(conn, &newconn);
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
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    Abrir_MacrosBaseDatos_desde_nvs();
    Abrir_PerfilUsuario_desde_nvs();


    hx711_init();
    vTaskDelay(pdMS_TO_TICKS(1000));

    calibrar_bascula();

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t wifi_any;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &wifi_any));

    wifi_config_t ap_config = {
        .ap = {
            .ssid           = "NutriGo",
            .ssid_len       = 7,
            .channel        = 1,
            .password       = "12345678",
            .max_connection = 8,
            .authmode       = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(30));

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
               ap_mac[0], ap_mac[1], ap_mac[2],
               ap_mac[3], ap_mac[4], ap_mac[5]);
        printf("SSID: %s, PASS: %s, Canal: %d\n",
               (char *)ap_config.ap.ssid,
               (char *)ap_config.ap.password,
               ap_config.ap.channel);
    }

    xTaskCreate(http_server,  "http_server",  4096, NULL, 5, NULL);
    xTaskCreate(task_hx711,   "task_hx711",   4096, NULL, 5, NULL);
    xTaskCreate(bateria_task, "bateria_task", 2048, NULL, 5, NULL);


    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
