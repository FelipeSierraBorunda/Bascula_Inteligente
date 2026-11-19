#include <string.h>
#include <math.h>
#include "ble_nutrigo.h"
#include "config.h"
#include "estado.h"
#include "calibracion.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_NUTRIGO";

// Paquete de lectura: 8 bytes, little-endian (ver ESPECIFICACION_BLE.md)
typedef struct __attribute__((packed)) {
    uint8_t  version;
    int32_t  peso_dg;
    uint8_t  estado;
    uint16_t bateria_mv;
} nutrigo_lectura_t;
_Static_assert(sizeof(nutrigo_lectura_t) == 8, "El paquete debe medir 8 bytes");

static const ble_uuid128_t uuid_servicio  = BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x6e);
static const ble_uuid128_t uuid_lectura   = BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x02,0x00,0x40,0x6e);
static const ble_uuid128_t uuid_comando   = BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x03,0x00,0x40,0x6e);
static const ble_uuid128_t uuid_respuesta = BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x04,0x00,0x40,0x6e);

static uint8_t  s_own_addr_type;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_lect_handle, s_resp_handle;
static bool     s_sub_lect = false, s_sub_resp = false;

static int gap_evento(struct ble_gap_event *ev, void *arg);

// ---------------- Paquetes ----------------
static uint8_t estado_byte(void)
{
    uint8_t e = 0;
    if (s_estable)                                       e |= NUTRIGO_BIT_ESTABLE;
    if (s_calibrada)                                     e |= NUTRIGO_BIT_CALIBRADA;
    if (s_bateria_mv > 0 && s_bateria_mv < BAT_BAJA_MV)  e |= NUTRIGO_BIT_BAT_BAJA;
    return e;
}

static nutrigo_lectura_t lectura_actual(void)
{
    nutrigo_lectura_t p = {
        .version    = NUTRIGO_VERSION,
        .peso_dg    = (int32_t)lroundf(s_peso_g * 10.0f),
        .estado     = estado_byte(),
        .bateria_mv = s_bateria_mv,
    };
    return p;
}

static void notificar_lectura(void)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_sub_lect) {
        return;
    }
    nutrigo_lectura_t p = lectura_actual();
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&p, sizeof(p));
    if (om) {
        ble_gatts_notify_custom(s_conn, s_lect_handle, om);
    }
}

static void responder(uint8_t cmd, uint8_t resultado)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_sub_resp) {
        return;
    }
    uint8_t b[2] = { cmd, resultado };
    struct os_mbuf *om = ble_hs_mbuf_from_flat(b, sizeof(b));
    if (om) {
        ble_gatts_notify_custom(s_conn, s_resp_handle, om);
    }
}

// ---------------- Comandos ----------------
static void procesar_comando(const uint8_t *d, uint16_t len)
{
    if (len < 1) {
        return;
    }
    switch (d[0]) {
    case NUTRIGO_CMD_TARA:
        s_tara_g = s_peso_bruto;
        ESP_LOGI(TAG, "Tara: %.1f g", s_tara_g);
        responder(NUTRIGO_CMD_TARA, NUTRIGO_RES_OK);
        break;

    case NUTRIGO_CMD_CALIBRAR: {
        if (len < 5) {
            responder(NUTRIGO_CMD_CALIBRAR, NUTRIGO_RES_ERROR);
            break;
        }
        int32_t dg;
        memcpy(&dg, &d[1], sizeof(dg));
        float patron_g = dg / 10.0f;
        if (s_ajustada == 0 || patron_g <= 0.0f) {
            responder(NUTRIGO_CMD_CALIBRAR, NUTRIGO_RES_ERROR);
            break;
        }
        s_factor    = patron_g / (float)s_ajustada;
        s_calibrada = true;
        s_tara_g    = 0.0f;
        esp_err_t e = calibracion_guardar();
        ESP_LOGI(TAG, "Calibrada con %.1f g: %.6f g/cuenta", patron_g, s_factor);
        responder(NUTRIGO_CMD_CALIBRAR, e == ESP_OK ? NUTRIGO_RES_OK : NUTRIGO_RES_ERROR);
        break;
    }

    case NUTRIGO_CMD_DORMIR:
        s_dormido = true;
        responder(NUTRIGO_CMD_DORMIR, NUTRIGO_RES_OK);
        if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
        }
        ESP_LOGI(TAG, "Modo bajo consumo (reinicia el equipo para volver)");
        break;

    default:
        responder(d[0], NUTRIGO_RES_ERROR);
        break;
    }
}

// ---------------- GATT ----------------
static int acceso_lectura(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    nutrigo_lectura_t p = lectura_actual();
    return os_mbuf_append(ctxt->om, &p, sizeof(p)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int acceso_comando(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    uint8_t buf[8];
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    procesar_comando(buf, len);
    return 0;
}

static int acceso_vacio(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return 0;   // la característica de respuesta solo notifica
}

static struct ble_gatt_chr_def chars[] = {
    {
        .uuid       = &uuid_lectura.u,
        .access_cb  = acceso_lectura,
        .val_handle = &s_lect_handle,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
    },
    {
        .uuid      = &uuid_comando.u,
        .access_cb = acceso_comando,
        .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid       = &uuid_respuesta.u,
        .access_cb  = acceso_vacio,
        .val_handle = &s_resp_handle,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
    },
    { 0 },
};

static const struct ble_gatt_svc_def servicios[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &uuid_servicio.u,
        .characteristics = chars,
    },
    { 0 },
};

// ---------------- GAP ----------------
static void iniciar_publicidad(void)
{
    struct ble_hs_adv_fields campos = {0};
    campos.flags                = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    campos.uuids128             = &uuid_servicio;
    campos.num_uuids128         = 1;
    campos.uuids128_is_complete = 1;
    if (ble_gap_adv_set_fields(&campos) != 0) {
        ESP_LOGE(TAG, "No pude configurar el anuncio");
        return;
    }

    struct ble_hs_adv_fields resp = {0};
    resp.name             = (const uint8_t *)NOMBRE_DISPOSITIVO;
    resp.name_len         = strlen(NOMBRE_DISPOSITIVO);
    resp.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&resp);

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    int rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_evento, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Error al anunciar: %d", rc);
    }
}

static int gap_evento(struct ble_gap_event *ev, void *arg)
{
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            s_conn = ev->connect.conn_handle;
            ESP_LOGI(TAG, "Conectado");
        } else {
            iniciar_publicidad();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        s_conn     = BLE_HS_CONN_HANDLE_NONE;
        s_sub_lect = false;
        s_sub_resp = false;
        ESP_LOGI(TAG, "Desconectado");
        if (!s_dormido) {
            iniciar_publicidad();
        }
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (!s_dormido) {
            iniciar_publicidad();
        }
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.attr_handle == s_lect_handle) {
            s_sub_lect = ev->subscribe.cur_notify;
        } else if (ev->subscribe.attr_handle == s_resp_handle) {
            s_sub_resp = ev->subscribe.cur_notify;
        }
        break;

    default:
        break;
    }
    return 0;
}

// ---------------- Arranque ----------------
static void on_sync(void)
{
    if (ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) {
        ESP_LOGE(TAG, "No pude obtener la dirección BLE");
        return;
    }
    iniciar_publicidad();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "Pila BLE reiniciada, motivo %d", reason);
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_nutrigo_iniciar(void)
{
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(NOMBRE_DISPOSITIVO);

    int rc = ble_gatts_count_cfg(servicios);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(servicios);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "Error en GATT: %d", rc);
    }

    nimble_port_freertos_init(host_task);
}

// Notifica solo si cambió el peso o el estado, o cada N lecturas.
void ble_nutrigo_publicar(void)
{
    static int32_t ult_dg   = INT32_MIN;
    static uint8_t ult_est  = 0xFF;
    static int     contador = 0;

    nutrigo_lectura_t p = lectura_actual();
    bool cambio = (p.peso_dg != ult_dg) || (p.estado != ult_est);
    if (cambio || ++contador >= NUTRIGO_LECTURA_CADA_N) {
        notificar_lectura();
        ult_dg   = p.peso_dg;
        ult_est  = p.estado;
        contador = 0;
    }
}
