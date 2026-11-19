#pragma once
// Estado compartido entre módulos (definido en estado.c)
#include <stdbool.h>
#include <stdint.h>

extern int32_t          s_offset;        // cero eléctrico (báscula vacía al arrancar)
extern float            s_factor;        // g por cuenta
extern bool             s_calibrada;
extern float            s_tara_g;        // tara del usuario, en g
extern volatile int32_t s_ajustada;      // cuentas menos offset
extern volatile float   s_peso_bruto;    // g sin tara
extern volatile float   s_peso_g;        // g con tara
extern volatile bool    s_estable;
extern volatile uint16_t s_bateria_mv;
extern volatile bool    s_dormido;
