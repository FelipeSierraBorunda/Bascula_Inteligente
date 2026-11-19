#pragma once
#include "esp_err.h"

void      calibracion_cargar(void);    // lee el factor desde NVS si existe
esp_err_t calibracion_guardar(void);   // guarda s_factor en NVS
