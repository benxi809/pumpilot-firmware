/**
 * @file ble_service_sdk.c
 * @brief BLE GATT 服务真实实现（nRF5 SDK 17 + SoftDevice S132）。
 *
 * 实现 ble_service.h 契约：
 * - Service `0000180F`（Drug Delivery / IEEE 11073）
 * - Char  `00002A20`（write + notify）          —— Data（IEEE11073 数据流）
 * - Char  `00002A21`（read）                    —— Status
 *
 * 采用标准 16-bit UUID（BLE 基 UUID），无需 sd_ble_uuid_vs_add。
 * 该文件仅在 ARM/SoftDevice 构建（PUMPILOT_SDK_BUILD=1）时编译；
 * host 单测(mock) 不包含本文件。
 *
 * 依据《固件完整API与BLE通讯协议文档》§4、§7。
 */

#include "sdk_common.h"
#include "ble_srv_common.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh.h"
#include "ble_service.h"
#include <string.h>

/* ============ 16-bit 标准 UUID ============ */
#define BLE_UUID_11073_SVC     0x180F
#define BLE_UUID_11073_DATA    0x2A20
#define BLE_UUID_11073_STATUS  0x2A21

/* 2A20 数据特征最大长度（IEEE11073 APDU 上限即可） */
#define DATA_CHAR_MAX_LEN      258u

/* ============ 模块状态 ============ */
static uint16_t             m_conn_handle     = BLE_CONN_HANDLE_INVALID;
static ble_gatts_char_handles_t m_char_handles;   /* data(2A20) 特征句柄集合 */
static ble_gatts_char_handles_t m_status_handles; /* status(2A21) 特征句柄集合 */
static uint16_t             m_data_handle     = BLE_GATT_HANDLE_INVALID;
static uint16_t             m_status_handle   = BLE_GATT_HANDLE_INVALID;
static uint8_t              m_status_value[2];         /* 2A21，LE */
static ble_write_cb_t       m_on_write        = NULL;
static ble_conn_cb_t        m_on_conn         = NULL;
static bool                 m_bound           = false;
static bool                 m_directed        = false;
static uint8_t              m_target_addr[6];

/* ============ GATT 事件分发（由 main_sdk observer 调用） ============ */
void ble_service_sdk_ble_evt(const ble_evt_t *p_ble_evt)
{
    ble_write_cb_t cb;

    switch (p_ble_evt->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED:
        m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
        if (m_on_conn) m_on_conn(true);
        break;

    case BLE_GAP_EVT_DISCONNECTED:
        m_conn_handle = BLE_CONN_HANDLE_INVALID;
        if (m_on_conn) m_on_conn(false);
        break;

    case BLE_GATTS_EVT_WRITE: {
        ble_gatts_evt_write_t const *p = &p_ble_evt->evt.gatts_evt.params.write;
        if (p->handle == m_data_handle &&
            (p->op == BLE_GATTS_OP_WRITE_REQ || p->op == BLE_GATTS_OP_WRITE_CMD) &&
            p->len > 0) {
            cb = m_on_write;
            if (cb) cb(p->data, p->len);
        }
        /* m_char_handles.cccd_handle：CCCD 订阅变更，无需额外处理 */
        break;
    }

    case BLE_GATTS_EVT_SYS_ATTR_MISSING:
        sd_ble_gatts_sys_attr_set(p_ble_evt->evt.gatts_evt.conn_handle,
                                  NULL, 0, 0);
        break;

    default:
        break;
    }
}

/* ============ GATT 服务注册 ============ */
static ret_code_t gatt_service_add(void)
{
    ret_code_t              err;
    ble_uuid_t              ble_uuid;
    ble_gatts_char_md_t     char_md;
    ble_gatts_attr_md_t     cccd_md;
    ble_gatts_attr_md_t     data_md;
    ble_gatts_attr_md_t     status_md;
    ble_gatts_attr_t        attr_char_value;
    ble_uuid_t              char_uuid;
    uint16_t                svc_handle;
    uint8_t                 data_init_val[DATA_CHAR_MAX_LEN];

    memset(data_init_val, 0, sizeof(data_init_val));

    /* ---- Service 180F ---- */
    memset(&ble_uuid, 0, sizeof(ble_uuid));
    ble_uuid.type = BLE_UUID_TYPE_BLE;
    ble_uuid.uuid = BLE_UUID_11073_SVC;
    err = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY,
                                   &ble_uuid, &svc_handle);
    if (err != NRF_SUCCESS) return err;

    /* ---- 2A20 Data（write + write_wo_resp + notify） ---- */
    memset(&char_md, 0, sizeof(char_md));
    memset(&cccd_md, 0, sizeof(cccd_md));
    memset(&data_md, 0, sizeof(data_md));

    char_md.char_props.write        = 1;
    char_md.char_props.write_wo_resp = 1;
    char_md.char_props.notify       = 1;
    char_md.p_cccd_md               = &cccd_md;
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&data_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&data_md.write_perm);
    data_md.vloc = BLE_GATTS_VLOC_STACK;
    data_md.vlen = 1;                       /* 可变长度 */

    memset(&attr_char_value, 0, sizeof(attr_char_value));
    memset(&char_uuid, 0, sizeof(char_uuid));
    char_uuid.type = BLE_UUID_TYPE_BLE;
    char_uuid.uuid = BLE_UUID_11073_DATA;
    attr_char_value.p_uuid    = &char_uuid;
    attr_char_value.p_attr_md = &data_md;
    attr_char_value.init_len  = 0;
    attr_char_value.init_offs = 0;
    attr_char_value.max_len   = DATA_CHAR_MAX_LEN;
    attr_char_value.p_value   = data_init_val;
    memset(&m_char_handles, 0, sizeof(m_char_handles));
    err = sd_ble_gatts_characteristic_add(svc_handle, &char_md,
                                          &attr_char_value,
                                          &m_char_handles);
    if (err != NRF_SUCCESS) return err;
    /* sd_ble_gatts_characteristic_add 返回特征值句柄与 CCCD 句柄 */
    m_data_handle = m_char_handles.value_handle;

    /* ---- 2A21 Status（read，LE 2字节） ---- */
    memset(&char_md, 0, sizeof(char_md));
    memset(&status_md, 0, sizeof(status_md));
    char_md.char_props.read = 1;
    char_md.p_cccd_md       = NULL;
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&status_md.read_perm);
    status_md.vloc = BLE_GATTS_VLOC_STACK;
    status_md.vlen = 1;
    m_status_value[0] = 0; m_status_value[1] = 0;

    memset(&attr_char_value, 0, sizeof(attr_char_value));
    memset(&char_uuid, 0, sizeof(char_uuid));
    char_uuid.type = BLE_UUID_TYPE_BLE;
    char_uuid.uuid = BLE_UUID_11073_STATUS;
    attr_char_value.p_uuid    = &char_uuid;
    attr_char_value.p_attr_md = &status_md;
    attr_char_value.init_len  = 2;
    attr_char_value.init_offs = 0;
    attr_char_value.max_len   = 2;
    attr_char_value.p_value   = m_status_value;
    err = sd_ble_gatts_characteristic_add(svc_handle, &char_md,
                                          &attr_char_value,
                                          &m_status_handles);
    m_status_handle = m_status_handles.value_handle;
    return err;
}

/* ============ ble_service.h 契约实现 ============ */

void ble_service_init(ble_write_cb_t on_write, ble_conn_cb_t on_conn)
{
    m_on_write = on_write;
    m_on_conn  = on_conn;
    m_conn_handle = BLE_CONN_HANDLE_INVALID;

    gatt_service_add();
    /* 广播由 main_sdk.c 在 softdevice 初始化完成后启动 */
}

bool ble_service_notify(const uint8_t *data, uint16_t len)
{
    ble_gatts_hvx_params_t hvx;
    uint16_t               hl = len;

    if (m_conn_handle == BLE_CONN_HANDLE_INVALID) return false;
    if (hl > DATA_CHAR_MAX_LEN) hl = DATA_CHAR_MAX_LEN;

    memset(&hvx, 0, sizeof(hvx));
    hvx.handle = m_data_handle;
    hvx.type   = BLE_GATT_HVX_NOTIFICATION;
    hvx.offset = 0;
    hvx.p_len  = &hl;
    hvx.p_data = (uint8_t *)data;

    return (sd_ble_gatts_hvx(m_conn_handle, &hvx) == NRF_SUCCESS);
}

uint16_t ble_service_read_status(void)
{
    return (uint16_t)(m_status_value[0] | ((uint16_t)m_status_value[1] << 8));
}

void ble_service_set_status(uint16_t status)
{
    m_status_value[0] = (uint8_t)(status & 0xFF);
    m_status_value[1] = (uint8_t)((status >> 8) & 0xFF);
    if (m_conn_handle != BLE_CONN_HANDLE_INVALID) {
        ble_gatts_value_t v;
        memset(&v, 0, sizeof(v));
        v.len  = 2;
        v.offset = 0;
        v.p_value = m_status_value;
        sd_ble_gatts_value_set(m_conn_handle, m_status_handle, &v);
    }
}

bool ble_service_is_connected(void)
{
    return (m_conn_handle != BLE_CONN_HANDLE_INVALID);
}

bool ble_service_is_bound(void)        { return m_bound; }
void ble_service_set_bound(bool bound) { m_bound = bound; }
void ble_service_set_directed(bool directed) { m_directed = directed; }

void ble_service_set_target(const uint8_t addr[6])
{
    if (addr) memcpy(m_target_addr, addr, 6);
    else      memset(m_target_addr, 0, 6);
}

const uint8_t *ble_service_get_target(void) { return m_target_addr; }
bool ble_service_directed_enabled(void)     { return m_directed; }
uint16_t ble_service_data_handle(void)      { return m_data_handle; }

/** 直接向应用回调分发一条写入（host 单测/调试也可调用） */
void ble_service_dispatch_write(const uint8_t *data, uint16_t len)
{
    if (m_on_write && data && len > 0) m_on_write(data, len);
}
