# Pumpilot 固件 完整 API 与 BLE 通讯协议文档

**项目名称**：Pumpilot — 睿昇胰岛素泵泵体固件（nRF52832）
**文档版本**：v1.0
**编制日期**：2026-08-04
**依据文档**：《接口边界与契约文档 v1.0》《固件架构与概要设计 v1.0》《固件详细设计说明书 v1.0》《固件软件需求说明书(FW-SRS) v1.0》

---

## 1. 概述

本文档定义**固件侧**的：
1. **内部服务 API**（C 模块函数签名、数据结构）；
2. **BLE 通讯协议**（IEEE 11073-20601 / 10419 实现、GATT 物理层、命令帧/上报帧格式、编解码、幂等与重试、时序）。

是 **FW-D3**（BLE + IEEE11073 agent）编码与 APP↔固件联调的直接依据。与 APP 侧《API与BLE通讯协议文档》互为镜像（本侧为 agent/service 实现）。

---

# 第一部分：内部服务 API

## 2. 通用约定

- C 语言，事件驱动 + 回调；
- 返回值统一 `ret_code_t`（`0=NRF_SUCCESS`，负值为错误码）；
- 模块间通过事件队列传递，不直接跨层调用；
- 关键模块由 `services/`、`drivers/`、`app/` 三目录组织。

## 3. 模块 API 明细

### 3.1 BasalScheduler
| 函数 | 签名 | 说明 |
|------|------|------|
| load | `ret_code_t basal_scheduler_load(const float table[48]);` | 载入48段基础率 |
| start/pause/resume | `ret_code_t basal_scheduler_*();` | 启停 |
| compute_ticks | `uint32_t basal_scheduler_compute_ticks(uint8_t seg);` | 段脉冲数 |
| tick | `void basal_scheduler_tick(void);` | 3分钟周期槽 |

### 3.2 BolusScheduler
| 函数 | 签名 | 说明 |
|------|------|------|
| start | `bolus_status_t bolus_scheduler_start(const bolus_spec_t *spec);` | 启动大剂量 |
| stop | `void bolus_scheduler_stop(bolus_stop_reason_t r);` | 终止 |
| tick | `void bolus_scheduler_tick(void);` | 30ms时隙 |

**bolus_spec_t**：{ type(快/扩/复), dose_IU, extension_min, bolus_ratio }（复合双波）

### 3.3 ReservoirMonitor
| 函数 | 签名 | 说明 |
|------|------|------|
| update | `void reservoir_monitor_update(void);` | 扫描格栅更新 |
| get_ml | `float reservoir_monitor_get_ml(void);` | 当前药量 |
| get_pos | `uint8_t reservoir_monitor_get_pos(void);` | 物理序号 |

### 3.4 AlarmEngine
| 函数 | 签名 | 说明 |
|------|------|------|
| raise/clear | `void alarm_engine_raise(uint16_t bit, alert_level_t l);` / `alarm_engine_clear(uint16_t bit);` | 报警位操作 |
| check | `void alarm_engine_check(void);` | 周期巡检阈值 |

### 3.5 ClockMgr
| 函数 | 签名 | 说明 |
|------|------|------|
| set/get | `ret_code_t clock_mgr_set(const rtc_datetime_t*);` / `clock_mgr_get(rtc_datetime_t*);` | 时钟读写 |
| need_sync | `bool clock_mgr_need_sync(void);` | 是否需对时(>1h) |

### 3.6 Ieee11073（agent）
| 函数 | 签名 | 说明 |
|------|------|------|
| init | `ret_code_t ieee11073_init(void);` | 初始化对象模型 |
| handle_cmd | `ret_code_t ieee11073_handle_cmd(const uint8_t *frame, uint16_t len);` | 处理APP命令 |
| build_report | `ret_code_t ieee11073_build_report(uint8_t **out, uint16_t *len);` | 组上报帧 |
| get_handle | `uint16_t ieee11073_get_handle(uint16_t class_code);` | 查对象Handle |

### 3.7 BleService
| 函数 | 签名 | 说明 |
|------|------|------|
| init | `ret_code_t ble_service_init(void);` | GATT注册+广播 |
| notify | `ret_code_t ble_service_notify(const uint8_t *data, uint16_t len);` | 经2A20上报 |
| on_write | `void ble_service_on_write(const uint8_t *data, uint16_t len);` | 2A20写入回调 |

---

# 第二部分：BLE 通讯协议（IEEE 11073）

## 4. GATT 物理层

| 项 | 值 | 说明 |
|----|----|------|
| Service | `0000180F-0000-1000-8000-00805F9B34FB` | Drug Delivery / IEEE 11073 |
| Char 数据(write/notify) | `00002A20-0000-1000-8000-00805F9B34FB` | IEEE 11073 Data |
| Char 状态(read) | `00002A21-0000-1000-8000-00805F9B34FB` | IEEE 11073 Status |
| Config-ID | 0x076C (1900) | IEEE 11073-10419 |
| data-proto-id | 0x5069 | PHD Optimized Exchange |

**绑定鉴权**：首次连接使用二维码中蓝牙密码（≤6位，字典序降序的倒数）校验；绑定后仅与原绑定 APP 连接，定向广播并自动恢复。

## 5. IEEE 11073-20601 优化交换协议（agent）

### 5.1 APDU（Application Protocol Data Unit）

- 所有消息封装为 **APDU** 帧：

```
┌──────────┬──────────┬──────────────┐
│ APDU 类型 │ 长度(uint16)│  payload     │
│ (1字节)  │          │              │
└──────────┴──────────┴──────────────┘
```

- 类型：`0x50` PRESENTATION (PDU) / `0xE2` ECHO。
- 呈现报文内部：CHOICE(Association / Operation / Data)。

### 5.2 关联（Association）建立

1. APP(manager) 发 `AssociateRequest`；
2. agent 返回 `AssociateResponse(accepted)`；
3. 协商 data-proto-id=0x5069、Config-ID=0x076C。

### 5.3 对象模型（agent）

对应《接口边界与契约文档》§3.3，agent 维护以下对象：

| 对象 | 类 | Handle | 方向 |
|------|----|:------:|:----:|
| MDS | MDC_MOC_VMS_MDS_SIMP(37) | 0 | 上报 |
| 大剂量设置/延迟 | NU(6) | 10/11 | 接收 |
| 已注射大剂量/基础率 | NU(6) | 1/— | 上报 |
| 基础率/ICR/ISF表 | NU(6) | — | 接收/上报 |
| 剩余药量/浓度 | NU(6) | — | 上报 |
| OpStat/泵状态 | Enum(5) | — | 上报 |
| PM-store / SCHEDSTORE | PMSTORE/SCHEDSTORE | 6/— | 读写 |

### 5.4 OpStat 与 InsPumpStat 位契约

- **OpStat**：bit0未定/bit1关闭/bit2待机/bit3准备中/bit4初始化中/bit5等待/bit6就绪/bit9治疗停止/bit10治疗暂停/bit11治疗运行。
- **InsPumpStat**：air-pressure-out-of-range / bolus-canceled / delivery-max / infusion-set-detached / infusion-set-incomplete / occlusion-detected / power-insufficient / priming-issue / reservoir-empty / reservoir-issue / reservoir-low / reservoir-attached / temp-basal-canceled / temp-basal-expired / temperature-out-of-range。

## 6. 命令帧（APP → 固件）

> 命令带全局唯一 **op_id**，泵对已执行命令幂等去重。

### 6.1 命令类型码

| 命令码 | 名称 | 说明 |
|:------:|------|------|
| 0x01 | CMD_SET_BASAL | 下发48段基础率 |
| 0x02 | CMD_SET_TEMP_BASAL | 临时基础率(1~4段) |
| 0x03 | CMD_DELIVER_BOLUS | 执行大剂量 |
| 0x04 | CMD_SET_TIME | 时间同步 |
| 0x05 | CMD_START | 开始输注 |
| 0x06 | CMD_PAUSE | 暂停 |
| 0x07 | CMD_RESUME | 恢复 |
| 0x08 | CMD_ABANDON | 废止 |
| 0x09 | CMD_SET_PARAM | 系统参数(启动/增量) |
| 0x0A | CMD_REPLACE_PROFILE | 替换曲线 |

### 6.2 通用命令帧格式

```
┌────────┬────────┬─────────┬──────────────┬───────────┐
│ CMD(1) │ op_id(4)│ 长度(2) │  payload     │ CRC(2)    │
└────────┴────────┴─────────┴──────────────┴───────────┘
```

- 固件校验 CRC，返回接收结果；控制命令另反馈执行结果。

### 6.3 CMD_DELIVER_BOLUS 负载

```
{
  op_id       : uint32
  bolus_type  : 0=fast 1=ext 2=dual
  dose_IU     : float          # 总剂量(IU)
  extension_min: uint16        # 扩展时间(扩展/复合)
  bolus_ratio : uint8(0-100)   # 复合前半比例(%)
}
```

## 7. 上报帧（固件 → APP）

### 7.1 上报类型码

| 上报码 | 名称 | 说明 |
|:------:|------|------|
| 0x81 | RPT_STATUS | 一般状态(电量/阶段) |
| 0x82 | RPT_WORK | 工作状态(大剂量/堵塞/临时) |
| 0x83 | RPT_MED | 药物状态(已注射/剩余/时间) |
| 0x84 | RPT_ALERT | 报警(立即上报) |
| 0x85 | RPT_TIME | 泵时间 |
| 0x86 | RPT_FACTORY | 泵特征/出厂信息 |
| 0x87 | RPT_RESULT | 命令执行结果 |

### 7.2 上报帧格式

```
┌────────┬────────┬─────────┬──────────────┬───────────┐
│ RPT(1) │ 长度(2)│  seq(2) │   payload    │ CRC(2)    │
└────────┴────────┴─────────┴──────────────┴───────────┘
```

- 常规状态在**唤醒周期**上报；**报警一旦产生立即上报**（不等休眠周期）。

**【注意】** 此帧为固件内部实现建议；若按 IEEE 11073 标准，字符特征上报应走 **2A20 notify**，内容为 IEEE 11073 APDU（见 §5）。本表为快捷命令载荷约定，实际联调以 APDU 为准。

## 8. 编解码与字节序

- 多字节整数：**小端**；
- float：IEEE 754，小端；
- CRC：CRC16-CCITT（polynomial 0x1021）；
- 帧头无固定魔数，靠长度+CRC 校验定界。

## 9. 幂等与重试

- 命令携带 `op_id`；固件记录最近 N 个已执行 `op_id`；
- APP 重试复用同一 `op_id` → 泵去重，**防重复输注**（对齐 RSK-06）；
- 数据/命令传输完成需泵校验并返回接收结果；控制命令需确认+反馈执行结果。

## 10. 通信时序

### 10.1 首次绑定
```
APP ──AssociateRequest──────► Firmware
APP ◄──AssociateResponse────── Firmware (accepted, 密码校验)
APP ──CMD_SET_TIME──────────► Firmware
APP ──CMD_SET_BASAL─────────► Firmware (48段)
APP ──CMD_SET_PARAM─────────► Firmware (启动参数)
APP ◄──RPT_RESULT(ok)──────── Firmware
```

### 10.2 执行大剂量
```
APP ──CMD_DELIVER_BOLUS(op_id)──► Firmware
APP ◄──RPT_RESULT(received)────── Firmware
APP ◄──RPT_WORK(执行中)────────── Firmware (输注中)
APP ◄──RPT_MED(已注射/剩余)────── Firmware (周期性)
APP ◄──RPT_RESULT(完成)────────── Firmware (完成后)
```

### 10.3 报警上报
```
[报警触发] → Firmware 立即:
APP ◄──RPT_ALERT(堵塞/一级)────── Firmware (不等休眠周期)
```

### 10.4 对时
```
APP ──CMD_SET_TIME──────────► Firmware
APP ◄──RPT_RESULT(ok)──────── Firmware
[此后每1h, 或时钟需同步时重复]
```

---

## 11. 与 APP 侧文档的关系

- 本文档与 APP《API与BLE通讯协议文档》**互为镜像**：APP 为 manager，本侧为 agent/service；
- 命令/上报码、对象 Handle、帧格式需在两侧**保持一致**，任何变更须双向更新契约并评审；
- 联调以《接口边界与契约文档》为唯一基准，用 nRF Connect 抓包验证报文一致性。

---

*本协议固件侧实现基线，随 FW-D3 联调细化。*
