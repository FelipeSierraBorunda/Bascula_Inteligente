# NutriGo Báscula — Especificación BLE (v1)

## Servicio
- UUID del servicio: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- Nombre de anuncio: `NutriGo-Bascula`
- Stack BLE en firmware: NimBLE (ESP32-C3 mini)

## Características
| Sufijo UUID | Nombre    | Propiedades       | Formato |
|-------------|-----------|-------------------|---------|
| `...0002`   | Lectura   | Notify            | paquete de 8 bytes |
| `...0003`   | Comando   | Write (sin resp.) | uint8 + payload opcional |
| `...0004`   | Respuesta | Notify            | uint8 código + uint8 resultado |

## Paquete de lectura (8 bytes, little-endian)
```c
typedef struct __attribute__((packed)) {
    uint8_t  version;     // 1
    int32_t  peso_dg;     // décimas de gramo, con signo
    uint8_t  estado;      // bit0 estable, bit1 calibrada, bit2 batería baja
    uint16_t bateria_mv;  // milivolts
} nutrigo_lectura_t;
```

## Comandos (característica Comando)
- `0x01` tara
- `0x02` calibrar: payload `int32` little-endian con el peso patrón en décimas de gramo (total 5 bytes)
- `0x03` dormir: deja de notificar y entra en bajo consumo

## Respuesta (característica Respuesta)
- Byte 1: código del comando recibido (`0x01`, `0x02`, `0x03`)
- Byte 2: `0x00` ok, `0x01` error

## Comportamiento
- Notificación a 10 Hz con conexión; si el peso no cambia, una vez por segundo.
- Calibración guardada en NVS del ESP32-C3 (funciona sin app abierta).
- Sin conexión durante 60 s: vuelve a anunciarse y espera.

## Responsabilidades
- Báscula: peso, tara, calibración, batería, estado.
- App de Mac: alimentos, registro diario, calorías y macros.
