#include "estado.h"
#include "config.h"

int32_t          s_offset      = 0;
float            s_factor      = FACTOR_DEFAULT;
bool             s_calibrada   = false;
float            s_tara_g      = 0.0f;
volatile int32_t s_ajustada    = 0;
volatile float   s_peso_bruto  = 0.0f;
volatile float   s_peso_g      = 0.0f;
volatile bool    s_estable     = false;
volatile uint16_t s_bateria_mv = 0;
volatile bool    s_dormido     = false;
