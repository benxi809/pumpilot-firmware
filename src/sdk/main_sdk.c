/**
 * @file main_sdk.c
 * @brief nRF52832 + SoftDevice S132 固件入口（ARM/真实构建）。
 *
 * 职责：
 * - 启用 SoftDevice（nrf_sdh_enable_request）
 * - 配置时钟（LFCLK 32.768k）与 app_timer
 * - 使能 BLE 栈（nrf_sdh_ble_enable）
 * - 注册 BLE observer → 分发事件到 pump_app / ble_service_sdk
 * - 注册 IEEE11073 GATT（180F/2A20/2A21）
 * - 启动广播（首连普通 / 绑定后定向）
 * - 主循环：sd_app_evt_wait + pump_app_run 轮询
 *
 * 该文件仅在 PUMPILOT_SDK_BUILD=1（ARM 构建）编译。
 */

#include <string.h>
#include "nrf.h"
#include "app_error.h"
#include "app_timer.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_drv_clock.h"
#include "ble_advdata.h"
#include "ble_advertising.h"

#include "ble_service.h"
#include "pump_app.h"

/* ============ 配置 ============ */
#define APP_BLE_OBSERVER_PRIO   3
#define APP_BLE_CONN_CFG_TAG    1
#define APP_ADV_INTERVAL_FAST   0x0028   /* 40ms */
#define APP_ADV_TIMEOUT_FAST    30u      /* 秒 */

/* 广播可见名称 */
static const char APP_NAME[] = "Pumpilot";

/* ============ 外部（ble_service_sdk.c）事件分发 ============ */
void ble_service_sdk_ble_evt(const ble_evt_t *p_ble_evt);

/* 广播参数写入时用的 device name */
static ble_gap_conn_sec_mode_t  m_sec_mode;
static uint8_t                  m_adv_handle = BLE_GAP_ADV_SET_HANDLE_NOT_SET;
static uint8_t                  m_enc_advdata[BLE_GAP_ADV_SET_DATA_SIZE_MAX];
static uint8_t                  m_enc_scanresp[BLE_GAP_ADV_SET_DATA_SIZE_MAX];
static uint16_t                 m_advdata_len, m_scanresp_len;

/* app_timer 3 分钟槽（基础率）*/
APP_TIMER_DEF(m_basal_timer);

static void basal_timer_handler(void *p_context)
{
    (void)p_context;
    pump_app_event_push(APP_EV_TIMER_3MIN);
}

/* ============ BLE 广播构造 ============ */
static void advertising_init(void)
{
    ret_code_t err;
    ble_advdata_t advdata;
    ble_advdata_t scanresp;
    uint8_t       flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;

    /* 设置广播/连接可见的 Device Name */
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&m_sec_mode);
    err = sd_ble_gap_device_name_set(&m_sec_mode,
                                     (const uint8_t *)APP_NAME,
                                     strlen(APP_NAME));
    if (err != NRF_SUCCESS && err != NRF_ERROR_DATA_SIZE) {
        /* 名称超长仅警告，不阻塞 */
    }

    memset(&advdata, 0, sizeof(advdata));
    advdata.name_type          = BLE_ADVDATA_FULL_NAME;
    advdata.include_appearance = false;
    advdata.flags              = flags;   /* S132 API v7: 标志为 uint8_t 字段 */

    memset(&scanresp, 0, sizeof(scanresp));
    scanresp.name_type = BLE_ADVDATA_NO_NAME;

    m_advdata_len = sizeof(m_enc_advdata);
    err = ble_advdata_encode(&advdata, m_enc_advdata, &m_advdata_len);
    if (err != NRF_SUCCESS) return;

    m_scanresp_len = sizeof(m_enc_scanresp);
    err = ble_advdata_encode(&scanresp, m_enc_scanresp, &m_scanresp_len);
    if (err != NRF_SUCCESS) return;

    if (m_adv_handle == BLE_GAP_ADV_SET_HANDLE_NOT_SET) {
        /* S132 API v7: sd_ble_gap_adv_set_configure(handle, adv_data, adv_params)
           广播数据与扫描响应打包在 ble_gap_adv_data_t 中。 */
        ble_gap_adv_data_t adv_data;
        uint16_t           adv_len  = (uint16_t)m_advdata_len;
        uint16_t           srs_len  = (uint16_t)m_scanresp_len;
        memset(&adv_data, 0, sizeof(adv_data));
        adv_data.adv_data.p_data     = m_enc_advdata;
        adv_data.adv_data.len        = adv_len;
        adv_data.scan_rsp_data.p_data = m_enc_scanresp;
        adv_data.scan_rsp_data.len    = srs_len;
        err = sd_ble_gap_adv_set_configure(&m_adv_handle, &adv_data, NULL);
        if (err != NRF_SUCCESS) { APP_ERROR_CHECK(err); }
    }
}

static void advertising_start_norm(void)
{
    ret_code_t err;
    ble_gap_adv_params_t adv;
    memset(&adv, 0, sizeof(adv));
    adv.properties.type = BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED;
    adv.p_peer_addr     = NULL;            /* 无目标(普通) */
    adv.interval        = APP_ADV_INTERVAL_FAST;
    adv.duration        = APP_ADV_TIMEOUT_FAST;
    adv.filter_policy   = BLE_GAP_ADV_FP_ANY;
    err = sd_ble_gap_adv_start(m_adv_handle, APP_BLE_CONN_CFG_TAG);
    (void)err;
}

static void advertising_start_directed(void)
{
    const uint8_t *ta = ble_service_get_target();
    ret_code_t     err;
    ble_gap_addr_t peer;
    ble_gap_adv_params_t adv;

    if (!ta) { advertising_start_norm(); return; }

    /* 目标地址：固定假设为公共地址类型（若绑定为随机地址需相应调整） */
    peer.addr_type = BLE_GAP_ADDR_TYPE_PUBLIC;
    memcpy(peer.addr, ta, BLE_GAP_ADDR_LEN);

    memset(&adv, 0, sizeof(adv));
    adv.properties.type = BLE_GAP_ADV_TYPE_CONNECTABLE_NONSCANNABLE_DIRECTED;
    adv.p_peer_addr     = &peer;
    adv.interval        = 0x0080;
    adv.duration        = APP_ADV_TIMEOUT_FAST;
    adv.filter_policy   = BLE_GAP_ADV_FP_ANY;
    err = sd_ble_gap_adv_start(m_adv_handle, APP_BLE_CONN_CFG_TAG);
    (void)err;
}

void pump_ble_adv_restart(void)
{
    sd_ble_gap_adv_stop(m_adv_handle);
    if (ble_service_directed_enabled() && ble_service_is_bound()) {
        advertising_start_directed();
    } else {
        advertising_start_norm();
    }
}

/* ============ BLE observer（分发 GATT/连接事件） ============ */
static void on_ble_evt(ble_evt_t const *p_ble_evt, void *p_context)
{
    (void)p_context;
    switch (p_ble_evt->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED:
        /* 连接后停止广播 */
        sd_ble_gap_adv_stop(m_adv_handle);
        break;
    case BLE_GAP_EVT_DISCONNECTED:
        /* 断开后重新广播 */
        break;
    default:
        break;
    }
    /* 交给 BLE 服务（连接/写/notify 事件） */
    ble_service_sdk_ble_evt(p_ble_evt);
}
NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, on_ble_evt, NULL);

/* ============ 时钟 / timer / 电源 ============ */
static void clock_init(void)
{
    ret_code_t err = nrf_drv_clock_init();
    if (err != NRF_SUCCESS) { APP_ERROR_CHECK(err); }
    nrf_drv_clock_lfclk_request(NULL);
}

static void timers_init(void)
{
    ret_code_t err = app_timer_init();
    APP_ERROR_CHECK(err);

    err = app_timer_create(&m_basal_timer, APP_TIMER_MODE_REPEATED,
                           basal_timer_handler);
    APP_ERROR_CHECK(err);
}

/* app_timer 周期（毫秒）→ 基础率 3 分钟 */
#define BASAL_TIMER_PERIOD_MS    (uint32_t)(3u * 60u * 1000u)

static void basal_timer_start(void)
{
    ret_code_t err = app_timer_start(m_basal_timer,
                                     APP_TIMER_TICKS(BASAL_TIMER_PERIOD_MS),
                                     NULL);
    APP_ERROR_CHECK(err);
}

/* ============ SoftDevice / BLE 初始化 ============ */
static void ble_stack_init(void)
{
    ret_code_t err;
    uint32_t   ram_start = 0;

    err = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err);

    /* SoftDevice 需要的内存起点 */
    err = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err);
}

/* ============ SoftDevice 事件处理后再泵处理 ============ */
static void softdevice_evt_dispatch(void)
{
    /* 唤醒 SoftDevice 并处理其事件（内部会调用已注册的 observers） */
    sd_app_evt_wait();
}

int main(void)
{
    /* 1. 时钟 */
    clock_init();

    /* 2. 定时器 */
    timers_init();

    /* 3. BLE 栈 */
    ble_stack_init();

    /* 4. 电源管理 */
    nrf_pwr_mgmt_init();

    /* 5. 广告构造 + 注册 */
    advertising_init();

    /* 6. 泵应用（含 BLE 服务注册 + GPIO/HAL） */
    pump_app_init(0);

    /* 7. 启动基础率 3 分钟定时器 */
    basal_timer_start();

    /* 8. 启动广播 */
    pump_ble_adv_restart();

    /* 9. 主循环：SoftDevice 事件(定时器/BLE/低功耗) 与 泵事件 交替处理 */
    for (;;) {
        softdevice_evt_dispatch();   /* sd_app_evt_wait：睡眠知有事件则立刻返回 */
        pump_app_process_once();     /* 处理泵事件队列 + 低频轮询 */
    }
}
