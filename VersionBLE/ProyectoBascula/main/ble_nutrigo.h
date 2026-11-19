#pragma once

void ble_nutrigo_iniciar(void);   // arranca NimBLE, GATT y anuncio
void ble_nutrigo_publicar(void);  // llamar tras cada lectura; notifica si cambió
