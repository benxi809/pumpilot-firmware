/**
 * @file ieee11073.h
 * @brief IEEE 11073 agent（SDK 无关，核心协议逻辑可单测）。
 *
 * 依据《固件完整API与BLE通讯协议文档》§5~§9、《接口边界与契约文档》§3。
 * 负责：APDU 编解码、命令帧解析/分发（0x01~0x0A）、上报帧构建（0x81~0x87）、
 * op_id 幂等去重、CONNECT 蓝牙密码鉴权与绑定。
 */
#ifndef PUMPILOT_IEEE11073_H
#define PUMPILOT_IEEE11073_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============ APDU 类型 ============ */
#define APDU_TYPE_PRESENTATION  0x50u   /* PDU */
#define APDU_TYPE_ECHO          0xE2u   /* ECHO */

/* ============ IEEE11073 对象 Handle == */
#define OBJ_HANDLE_MDS          0u
#define OBJ_HANDLE_DELIVERED_BOLUS  1u
#define OBJ_HANDLE_CURRENT_BOLUS    10u
#define OBJ_HANDLE_BOLUS_DELAY      11u

/* ============ 命令类型码（APP→固件） ============ */
enum {
    CMD_SET_BASAL       = 0x01,  /* 下发 48 段基础率 */
    CMD_SET_TEMP_BASAL  = 0x02,  /* 临时基础率(1~4段) */
    CMD_DELIVER_BOLUS   = 0x03,  /* 执行大剂量 */
    CMD_SET_TIME        = 0x04,  /* 时间同步 */
    CMD_START           = 0x05,  /* 开始输注 */
    CMD_PAUSE           = 0x06,  /* 暂停 */
    CMD_RESUME          = 0x07,  /* 恢复 */
    CMD_ABANDON         = 0x08,  /* 废止 */
    CMD_SET_PARAM       = 0x09,  /* 系统参数 */
    CMD_REPLACE_PROFILE = 0x0A   /* 替换曲线 */
};

/* ============ 上报类型码（固件→APP） ============ */
enum {
    RPT_STATUS  = 0x81,  /* 一般状态(电量/阶段) */
    RPT_WORK    = 0x82,  /* 工作状态(大剂量/堵塞/临时) */
    RPT_MED     = 0x83,  /* 药物状态(已注射/剩余/时间) */
    RPT_ALERT   = 0x84,  /* 报警(立即) */
    RPT_TIME    = 0x85,  /* 泵时间 */
    RPT_FACTORY = 0x86,  /* 泵特征/出厂信息 */
    RPT_RESULT  = 0x87   /* 命令执行结果 */
};

/* ============ 命令执行结果码 ============ */
enum {
    CMD_RESULT_OK          = 0,   /* 成功 */
    CMD_RESULT_REJECT      = 1,   /* 拒绝(状态不允许) */
    CMD_RESULT_CRC_ERROR   = 2,   /* CRC 校验失败 */
    CMD_RESULT_OPID_DUP    = 3,   /* 重复 op_id(幂等) */
    CMD_RESULT_PAYLOAD_ERR = 4    /* 负载非法 */
};

/* 命令帧最大长度 */
#define IEEE11073_FRAME_MAX   256u
/* 管理缓冲区 */
#define IEEE11073_BUF_MAX     128u

/**
 * @brief 初始化 agent（对象模型、幂等历史）。
 */
void ieee11073_init(void);

/**
 * @brief 处理一条来自 2A20 的数据（可为 APDU 或快捷命令帧）。
 * @param data 数据
 * @param len  长度
 * @return 结果码（0=处理成功）
 */
int ieee11073_on_data(const uint8_t *data, uint16_t len);

/**
 * @brief 组一条上报帧（RPT|len|seq|payload|CRC，小端）并返回长度。
 * @param rpt_type 上报类型码(0x81~0x87)
 * @param payload   载荷
 * @param plen      载荷长度
 * @param out      输出缓冲（>=IEEE11073_FRAME_MAX）
 * @return 帧长度；<0 错误
 */
int ieee11073_build_report(uint8_t rpt_type,
                           const uint8_t *payload, uint16_t plen,
                           uint8_t *out);

/**
 * @brief 主动上报某类状态（由服务层在唤醒/报警时调用）。
 * @param rpt_type 上报类型码
 * @return 帧构建结果（>=0帧长），并置 pending 待 BLE notify
 */
int ieee11073_push_report(uint8_t rpt_type);

/** 是否有待 notify 的帧 */
bool ieee11073_has_pending(void);

/**
 * @brief 取出一条待 notify 帧（BleService 拉取）。
 * @param out 输出
 * @param len_out 帧长
 * @return true=有
 */
bool ieee11073_get_pending(uint8_t *out, uint16_t *len_out);

/* ============ 鉴权/绑定 ============ */
/** 是否已绑定 */
bool ieee11073_is_bound(void);

/**
 * @brief 校验蓝牙连接密码（首次绑定鉴权）。
 * @param pwd   密码（≤6位）
 * @param plen  密码长
 * @return true=通过
 */
bool ieee11073_auth_bind_pwd(const uint8_t *pwd, uint16_t plen);

/**
 * @brief 校验绑定（是否与已绑定 APP 匹配）。绑定后仅原 APP 可连。
 * @param serial APP 侧提供的设备序列号
 * @return true=匹配
 */
bool ieee11073_check_bound(const uint8_t *serial, uint16_t slen);

/* ============ 对象模型/状态查询 ============ */
/** 查询某类编码对象的 Handle */
uint16_t ieee11073_get_handle(uint16_t class_code);

/* ============ 与调度/状态集成 ============ */
/**
 * @brief 通知 agent 各状态源（由上层在变化时调用，用于组成 RPT_*）。
 * 简化：agent 内部聚合基础率/大剂量/状态机/电量等最新值。
 *
 * @param state 状态机状态
 * @param bat_mv 电池电压
 * @param delivered_bolus_iu 已注射大剂量
 * @param reservoir_ml 剩余药量
 */
void ieee11073_update_sources(uint8_t state, uint16_t bat_mv,
                              float delivered_bolus_iu, float reservoir_ml);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_IEEE11073_H */
