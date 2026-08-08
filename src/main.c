/**
 * @file main.c
 * @brief 固件入口。
 *
 * FW-D1：无 SDK 场景提供裸机 main 骨架；FW-D3 接入 SoftDevice/BLE 后在此加入
 * nrf_sdh 初始化、GATT 服务注册与事件分发，并在主循环处理 SoftDevice 事件。
 * 当前版本为对板（非 SDK）依赖可编译的引导骨架。
 */
#include "pump_app.h"

/* SDK 接入时由链接脚本/构建宏打开 */
#ifndef PUMPILOT_HOST_BUILD
#define PUMPILOT_HOST_BUILD 1
#endif

#if PUMPILOT_HOST_BUILD
/* 主机/无软设备构建：直接进入裸机主程序 */
int main(void)
{
    pump_app_init(0);
    pump_app_run();
    /* 不应返回 */
    return 0;
}
#else
/* 未来 SoftDevice 构建在此加入 nrf_sdh_enable_request 等 */
int main(void)
{
    pump_app_init(0);
    pump_app_run();
    return 0;
}
#endif
