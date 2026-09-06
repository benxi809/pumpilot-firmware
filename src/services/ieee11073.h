/**
 * @file ieee11073.h
 * @brief IEEE 11073 agent（SDK 无关，核心协议逻辑可单测）。
 *
 * 依据《API与BLE通讯协议文档》§8~§9、《180F_FW_IMPLEMENT》Phase A.
 * 命令帧：0x11 0x73 | 0x01 | cmd | opId(LE4) | payload | crc(LE2)
 * 上报帧：0x11 0x73 | 0x02 | rpt | payload ... | crc(LE2)
 * 帧 CRC = crc16_ccitt(从头到 payload 末，不含末尾 CRC 两字节)。
 */
#ifndef PUMPILOT_IEEE11073_H
#define PUMPILOT_IEEE11073_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 帧常量 ============ */
#define IEEE_SYNC0           0x11u   /* 同步字 */
#define IEEE_SYNC1           0x73u
#define IEEE_MSG_CMD         0x01u   /* 消息类型=命令 */
#define IEEE_MSG_REPORT      0x02u   /* 消息类型=数据上报 */

/* ============ 命令码（文档 §8；APP→固件写 2A20） ============ */
enum {
    IEEE_CMD_CONNECT        = 0x01,  /* 鉴权（密码） */
    IEEE_CMD_SET_BASAL_PROF = 0x02,  /* profile_id(1)+48×f32 */
    IEEE_CMD_SET_TEMP_BASAL = 0x03,  /* 临时基础率（先仅校验） */
    IEEE_CMD_SET_BOLUS      = 0x04,  /* 大剂量 */
    IEEE_CMD_START          = 0x05,  /* 开始输注 */
    IEEE_CMD_PAUSE          = 0x06,  /* 暂停(目标) */
    IEEE_CMD_RESUME         = 0x07,  /* 恢复 */
    IEEE_CMD_STOP           = 0x08,  /* 废止 */
    IEEE_CMD_SET_CLOCK      = 0x09,  /* 时钟 */
    IEEE_CMD_SWITCH_CURVE   = 0x0A,  /* 切换曲线 */
    IEEE_CMD_CONFIRM        = 0x0B   /* 回执(仅解析不执行) */
};

/* ============ 上报码（文档 §9；固件→APP notify 2A20） ============ */
enum {
    IEEE_RPT_ALERT   = 0x81,  /* 泵报警：level(1)+alarm_type(1)+文本 */
    IEEE_RPT_STATE   = 0x82,  /* 泵状态：OpStat(2)+InsPumpStat(4)+电量(1) */
    IEEE_RPT_MED     = 0x83,  /* 药量：物理序号(1)+剩余(IU f32 LE) */
    IEEE_RPT_FACTORY = 0x84,  /* 特征信息 */
    IEEE_RPT_CONFIRM = 0x85,  /* 命令回执：opId(4)+cmd(1)+result(u16 LE) */
    IEEE_RPT_TIME    = 0x86   /* 时间：y/m/d/h/min */
};

/* ============ 命令执行结果码 ============ */
enum {
    CMD_RESULT_OK          = 0,   /* 成功 */
    CMD_RESULT_REJECT      = 1,   /* 拒绝(状态不允许) */
    CMD_RESULT_CRC_ERROR   = 2,   /* CRC 校验失败 */
    CMD_RESULT_OPID_DUP    = 3,   /* 重复 op_id(幂等) */
    CMD_RESULT_PAYLOAD_ERR = 4    /* 负载非法 */
};
/* 无权限（未绑定/鉴权失败；协议层 error code 3004, 超出 u8 故回执用 u16 LE） */
#define IEEE_ERR_UNAUTHORIZED  3004u

/* 帧长度上限 */
#define IEEE11073_FRAME_MAX   256u

/**
 * @brief 初始化 agent（绑定/幂等历史/状态源清零）。
 */
void ieee11073_init(void);

/**
 * @brief 处理一条来自 2A20 的数据（0x11 0x73 自定义帧）。
 * @param data 数据
 * @param len  长度
 * @return 结果码（0=处理成功；非 0 见 CMD_RESULT_* —— 均有 pending 回执）
 */
int ieee11073_on_data(const uint8_t *data, uint16_t len);

/**
 * @brief 组一条文档上报帧：11 73 | 02 | rpt | payload | crc(LE2)。
 * @param rpt_type 上报类型码（IEEE_RPT_*）
 * @param payload  载荷
 * @param plen     载荷长度
 * @param out      输出缓冲（>= IEEE11073_FRAME_MAX）
 * @return 帧长度；<0 错误
 */
int ieee11073_build_report(uint8_t rpt_type,
                           const uint8_t *payload, uint16_t plen,
                           uint8_t *out);

/**
 * @brief 主动上报某类状态（构造后置 pending 待 BLE notify）。
 * @param rpt_type 上报类型码
 * @return 帧构建结果（>=0帧长）
 */
int ieee11073_push_report(uint8_t rpt_type);

/** 是否有待 notify 的帧 */
bool ieee11073_has_pending(void);

/**
 * @brief 取出一条待 notify 帧。
 * @param out 输出
 * @param len_out 帧长
 * @return true=有
 */
bool ieee11073_get_pending(uint8_t *out, uint16_t *len_out);

/* ============ 鉴权/绑定 ============ */
/** 是否已绑定（CONNECT 成功并鉴权通过） */
bool ieee11073_is_bound(void);

/**
 * @brief 校验蓝牙连接密码并置绑定标记。
 * @param pwd   密码（与 device_info_get()->bt_password 严格等长比较）
 * @param plen  密码长
 * @return true=通过
 */
bool ieee11073_auth_bind_pwd(const uint8_t *pwd, uint16_t plen);

/**
 * @brief 校验绑定序列号（保留占位，恒真）。
 */
bool ieee11073_check_bound(const uint8_t *serial, uint16_t slen);

/* ============ 对象模型/状态查询 ============ */
uint16_t ieee11073_get_handle(uint16_t class_code);

/* ============ 与调度/状态集成 ============ */
/**
 * @brief 通知 agent 各状态源（由上层变化时调用）。
 */
void ieee11073_update_sources(uint8_t state, uint16_t bat_mv,
                              float delivered_bolus_iu, float reservoir_ml);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_IEEE11073_H */
