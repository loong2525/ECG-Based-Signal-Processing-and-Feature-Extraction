/*
 * @Compony: NUC
 * @Date: 2026-07-19 10:36:13
 * @LastEditors: Loong2525
 * @LastEditTime: 2026-07-19
 */
#ifndef __HMI_H
#define __HMI_H

#include "main.h"
#include <stdint.h>
#include <string.h>

/* ========== 串口屏(TJC)页面名称定义 ========== */
#define PAGE_INIT       "page init"        /* 装置初始化界面      */
#define PAGE_WAVE       "page wave"        /* 波形图采集界面      */
#define PAGE_REGISTER   "page register"    /* 注册界面            */
#define PAGE_LOAD       "page load"        /* 加载界面            */
#define PAGE_USER_S     "page user_s"      /* 用户验证成功        */
#define PAGE_ADM_S      "page adm_s"       /* 管理员验证成功      */
#define PAGE_DEFAULT    "page dufault"     /* 验证失败/默认界面   */
#define PAGE_R_S        "page r_s"         /* 注册成功            */
#define PAGE_MANAGE     "page user"      /* 用户管理界面        */

/* ========== 2字节任务命令定义 ==========
 * 通过 UART5(串口屏) 或 UART7(天问) 发送2字节命令(大端序,高字节在前)
 * 高字节 = 命令类别(0x00=注册,0x01=验证,0x02=开锁,0x03=用户管理,0x04=系统初始化)
 * 低字节 = 子类型(0x00=用户,0x01=管理员; 系统命令高/低字节相同)
*/
#define CMD_USER_VERIFY     0x0100   /* 用户权限验证     */
#define CMD_ADMIN_VERIFY    0x0101   /* 管理员权限验证   */
#define CMD_ADMIN_REGISTER  0x0011   /* 管理员权限注册   */
#define CMD_USER_REGISTER   0x0010   /* 用户权限注册     */
#define CMD_UNLOCK          0x0202   /* 开锁             */
#define CMD_SYS_INIT        0x0404   /* 系统初始化       */
#define CMD_MANAGE_USERS    0x0303   /* 用户管理         */

/* ========== 天问(UART7) 2字节16进制命令定义 ==========
 * 天问语音助手使用2字节16进制编码通信，不接收字符串，无0xFF后缀
 * 发送时高字节在前(大端序)
 */
#define TW_CMD_PAGE_WAVE     0x1111   /* 通知天问：波形采集页面 */
#define TW_CMD_PAGE_USER_S   0x2222   /* 通知天问：用户验证成功 */
#define TW_CMD_PAGE_DEFAULT  0x3333   /* 通知天问：验证失败/默认 */
#define TW_CMD_PAGE_ADM_S    0x4444   /* 通知天问：管理员验证成功 */
#define TW_CMD_PAGE_R_S      0x5555   /* 通知天问：注册成功 */
#define TW_CMD_PAGE_INIT     0x6666   /* 通知天问：初始化页面 */
#define TW_CMD_PAGE_MANAGE   0x7777   /* 通知天问：用户管理页面 */
#define TW_CMD_UNLOCK        0x0202   /* 通知天问：开锁指令 */

/* ========== HMI 主状态机 ========== */
typedef enum {
    HMI_STATE_IDLE = 0,
    HMI_STATE_USER_VERIFY,
    HMI_STATE_ADMIN_VERIFY,
    HMI_STATE_USER_REGISTER,
    HMI_STATE_ADMIN_REGISTER,
    HMI_STATE_UNLOCK,
    HMI_STATE_SYS_INIT,
    HMI_STATE_MANAGE_USERS
} HMI_State;

/* ========== HMI 子状态(任务内步骤) ========== */
typedef enum {
    HMI_SUB_IDLE = 0,
    HMI_SUB_START_ACQ,
    HMI_SUB_SHOW_WAVE,
    HMI_SUB_ACQUIRE,
    HMI_SUB_COMPARE_SD,
    HMI_SUB_WRITE_SD,
    HMI_SUB_LIST_SD,
    HMI_SUB_DELETE_USER,
    HMI_SUB_WAIT_USERNAME,
    HMI_SUB_SEND_RESULT,
    HMI_SUB_DONE
} HMI_SubState;

/* ========== 公开函数原型 ========== */
void HMI_Init(void);
void HMI_TaskProcess(void);
void HMI_SendToScreen(const char *str);
void HMI_SendToTianwen(uint16_t cmd);  /* 发送2字节16进制命令到天问(UART7)，无0xFF后缀 */
void HMI_SendToScreenRaw(const uint8_t *data, uint16_t len);
void HMI_SendToTianwenRaw(const uint8_t *data, uint16_t len);

/* 模块仅负责HMI逻辑控制, 不依赖外部模块 */
#endif /* __HMI_H */
