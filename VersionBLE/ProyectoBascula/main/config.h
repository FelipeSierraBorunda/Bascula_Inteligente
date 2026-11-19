#pragma once
// Constantes del proyecto NutriGo (báscula BLE, ESP32-C3 mini)
#include "driver/gpio.h"

// ---- Pines ----
#define PIN_HX711_DOUT          GPIO_NUM_4
#define PIN_HX711_SCK           GPIO_NUM_3

// ---- Batería (divisor 47k/47k sobre GPIO0 = ADC1_CH0 en C3) ----
#define BAT_DIVISOR             2.0f
#define BAT_BAJA_MV             3400
#define BAT_PERIODO_MS          5000

// ---- Medición HX711 ----
#define HX_PERIODO_MS           100
#define HX_PERIODO_DORMIDO_MS   1000
#define HX_TIMEOUT_US           500000
#define HX_MUESTRAS_OFFSET      10
#define N_VENTANA               10
#define UMBRAL_ESTABLE_G        0.5f
#define FACTOR_DEFAULT          (200.59f / 81000.0f)   // g por cuenta (valor previo)

// ---- Tareas FreeRTOS (pila en bytes) ----
#define STACK_HX711             4096
#define STACK_BATERIA           4096
#define PRIO_HX711              5
#define PRIO_BATERIA            3

// ---- BLE ----
#define NOMBRE_DISPOSITIVO      "NutriGo-Bascula"
#define NUTRIGO_VERSION         1
#define NUTRIGO_CMD_TARA        0x01
#define NUTRIGO_CMD_CALIBRAR    0x02
#define NUTRIGO_CMD_DORMIR      0x03
#define NUTRIGO_RES_OK          0x00
#define NUTRIGO_RES_ERROR       0x01
#define NUTRIGO_BIT_ESTABLE     0x01
#define NUTRIGO_BIT_CALIBRADA   0x02
#define NUTRIGO_BIT_BAT_BAJA    0x04
#define NUTRIGO_LECTURA_CADA_N  10   // notificar aunque no cambie, cada N lecturas
