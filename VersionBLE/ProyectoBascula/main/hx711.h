#pragma once
#include <stdbool.h>
#include <stdint.h>

void hx711_init(void);
bool hx711_leer(int32_t *salida);                 // una lectura cruda
bool hx711_promedio(int n, int32_t *salida);      // promedio de n lecturas
void hx711_tarea(void *arg);                      // tarea: muestrea y publica
