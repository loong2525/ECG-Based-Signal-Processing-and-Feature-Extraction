/*
 * @Compony: NUC
 * @Date: 2026-07-19
 * @LastEditors: Loong2525
 * @LastEditTime: 2026-07-24
 * @Description: HMI模块 - 串口屏(TJC)与天问语音助手通信、任务调度状态机
 */

#include "HMI.h"
#include "usart.h"
#include "task.h"
#include <string.h> /* 引入 string.h 用于 strcmp */

/* ========== 环形缓冲区配置 ========== */
#define RX_BUF_SIZE   256         /* 必须是2的幂    */
#define RX_BUF_MASK   (RX_BUF_SIZE - 1)

typedef struct {
    uint8_t             buf[RX_BUF_SIZE];
    volatile uint16_t   head;     /* 生产者(中断)写入位置 */
    volatile uint16_t   tail;     /* 消费者(主循环)读取位置 */
} RingBuf;

/* UART接收环形缓冲区 */
static RingBuf  g_rx5;           /* UART5 - 串口屏(TJC) */
static RingBuf  g_rx7;           /* UART7 - 天问语音助手 */

/* 单字节接收缓冲(用于HAL_UART_Receive_IT) */
static uint8_t  g_rx5_byte;
static uint8_t  g_rx7_byte;

/* HMI状态机 */
static HMI_State    g_state      = HMI_STATE_IDLE;
static HMI_SubState g_sub        = HMI_SUB_IDLE;
static uint32_t     g_tick_wait  = 0;
static uint8_t      g_verify_pass = 0;
static char         g_username[64];
static uint8_t      g_username_len = 0;

/* ========== 环形缓冲区操作 ========== */
static void RingBuf_Init(RingBuf *rb)
{
    rb->head = 0;
    rb->tail = 0;
}

static uint16_t RingBuf_Used(RingBuf *rb)
{
    return (uint16_t)((uint16_t)(rb->head - rb->tail) & RX_BUF_MASK);
}

static uint8_t RingBuf_Put(RingBuf *rb, uint8_t data)
{
    uint16_t next = (uint16_t)((rb->head + 1) & RX_BUF_MASK);
    if (next == rb->tail) {
        return 1;               /* 缓冲区满 */
    }
    rb->buf[rb->head] = data;
    rb->head = next;
    return 0;
}

static uint8_t RingBuf_Get(RingBuf *rb, uint8_t *data)
{
    if (rb->head == rb->tail) {
        return 1;               /* 缓冲区空 */
    }
    *data = rb->buf[rb->tail];
    rb->tail = (uint16_t)((rb->tail + 1) & RX_BUF_MASK);
    return 0;
}

/* Peek: 查看偏移offset处的字节, 不消费 */
static uint8_t RingBuf_Peek(RingBuf *rb, uint16_t offset, uint8_t *data)
{
    if (RingBuf_Used(rb) <= offset) return 1;
    uint16_t idx = (uint16_t)((rb->tail + offset) & RX_BUF_MASK);
    *data = rb->buf[idx];
    return 0;
}

/* ========== 串口发送(带TJC \xff\xff\xff 结束符) ========== */
void HMI_SendToScreen(const char *str)
{
    uint16_t len = (uint16_t)strlen(str);
    uint8_t  term[3] = { 0xff, 0xff, 0xff };
    HAL_UART_Transmit(&huart5, (uint8_t *)str, len, 100);
    HAL_UART_Transmit(&huart5, term, 3, 100);
    HAL_Delay(100);
}

void HMI_SendToTianwen(uint16_t cmd)
{
    uint8_t buf[2];
    HAL_StatusTypeDef ret;
    buf[0] = (uint8_t)(cmd >> 8);   /* 高字节在前(大端序) */
    buf[1] = (uint8_t)(cmd & 0xFF);
    ret = HAL_UART_Transmit(&huart7, buf, 2, 100);
//    DBG_Printf("[UART7] TX: 0x%02X 0x%02X  ret=%d\r\n", buf[0], buf[1], ret);
    if (ret != HAL_OK) {
        DBG_Printf("[UART7] *** Transmit FAILED! ret=%d\r\n", ret);
    }
}

void HMI_SendToScreenRaw(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&huart5, (uint8_t *)data, len, 100);
}

void HMI_SendToTianwenRaw(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&huart7, (uint8_t *)data, len, 100);
}

/* ========== UART接收中断回调 ========== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == UART5) {
        RingBuf_Put(&g_rx5, g_rx5_byte);
        HAL_UART_Receive_IT(&huart5, &g_rx5_byte, 1);
    } else if (huart->Instance == UART7) {
        RingBuf_Put(&g_rx7, g_rx7_byte);
        HAL_UART_Receive_IT(&huart7, &g_rx7_byte, 1);
    }
}

/* ========== 命令解析 ========== */

/** 判断2字节是否为已知命令 */
static uint8_t IsValidCmd(uint16_t cmd)
{
    switch (cmd) {
    case CMD_USER_VERIFY:
    case CMD_ADMIN_VERIFY:
    case CMD_ADMIN_REGISTER:
    case CMD_USER_REGISTER:
    case CMD_UNLOCK:
    case CMD_SYS_INIT:
    case CMD_MANAGE_USERS:
        return 1;
    default:
        return 0;
    }
}

/** 从环形缓冲区读取用户名(跳过 \r \n 0xFF), 成功返回0 */
static uint8_t Username_Read(RingBuf *rb)
{
    uint8_t byte;
    g_username_len = 0;
    while ((RingBuf_Used(rb) > 0) && (g_username_len < sizeof(g_username) - 1)) {
        RingBuf_Get(rb, &byte);
        /* 遇到换行符表示用户名输入结束，停止读取 */
        /* 注意是break不是continue！continue会继续读后续字符直到缓冲区空 */
        if (byte == '\r' || byte == '\n') break;
        /* 过滤所有非打印字符：只保留0x20(空格)~0x7E(~)的可打印ASCII */
        /* 过滤命令残留字节(如0x01/0x00)和换行符，防止污染用户名 */
        if (byte < 0x20 || byte > 0x7E) continue;
        g_username[g_username_len++] = (char)byte;
    }
    /* 去除首尾空格 */
    while (g_username_len > 0 && g_username[g_username_len-1] == ' ') g_username_len--;
    if (g_username_len > 0) {
        uint8_t trim_start = 0;
        while (trim_start < g_username_len && g_username[trim_start] == ' ') trim_start++;
        if (trim_start > 0) {
            for (uint8_t i = trim_start; i < g_username_len; i++)
                g_username[i - trim_start] = g_username[i];
            g_username_len -= trim_start;
        }
    }
    g_username[g_username_len] = '\0';
    return (g_username_len > 0) ? 0 : 1;
}

/** 根据2字节命令启动对应任务 */
static void Cmd_Dispatch(uint16_t cmd)
{
   switch (cmd) {
   case CMD_USER_VERIFY:
       DBG_Printf("用户权限验证\r\n");
       g_state = HMI_STATE_USER_VERIFY;
       break;
   case CMD_ADMIN_VERIFY:
       DBG_Printf("管理员权限验证\r\n");
       g_state = HMI_STATE_ADMIN_VERIFY;
       break;
   case CMD_ADMIN_REGISTER:
       DBG_Printf("管理员权限注册\r\n");
       g_state = HMI_STATE_ADMIN_REGISTER;
       break;
   case CMD_USER_REGISTER:
       DBG_Printf("用户权限注册\r\n");
       g_state = HMI_STATE_USER_REGISTER;
       break;
   case CMD_UNLOCK:
       DBG_Printf("开锁\r\n");
       g_state = HMI_STATE_UNLOCK;
       break;
  case CMD_SYS_INIT:
      DBG_Printf("系统初始化\r\n");
      g_state = HMI_STATE_SYS_INIT;
      break;
  case CMD_MANAGE_USERS:
       DBG_Printf("用户管理\r\n");
       g_state = HMI_STATE_MANAGE_USERS;
       break;
   default:
       DBG_Printf("未知命令,丢弃: 0x%04X\r\n", cmd);
       break;
   }
    if (g_state != HMI_STATE_IDLE) {
        g_sub = HMI_SUB_IDLE;
    }
}

/**
 * 通用验证流程 (用户 / 管理员)
 * SUB_IDLE -> START_ACQ -> SHOW_WAVE -> ACQUIRE -> COMPARE_SD -> SEND_RESULT -> DONE
 */
static void Task_ProcessVerify(HMI_State state)
{
    switch (g_sub) {
    case HMI_SUB_IDLE:
        DBG_Printf("[HMI]   等待接收用户名...\r\n");
        g_sub = HMI_SUB_WAIT_USERNAME;
        break;

    case HMI_SUB_WAIT_USERNAME:
        if ((Username_Read(&g_rx5) == 0) || (Username_Read(&g_rx7) == 0)) {
           uint8_t permission = (state == HMI_STATE_ADMIN_VERIFY) ? 1 : 0;
            DBG_Printf("[HMI]   用户名: %s, 权限: %d\r\n", g_username, permission);
           g_sub = HMI_SUB_SHOW_WAVE;
        }
        break;

    case HMI_SUB_SHOW_WAVE:
        HMI_SendToScreen(PAGE_WAVE);
        HMI_SendToTianwen(TW_CMD_PAGE_WAVE);       
        DBG_Printf("发送 page wave 到串口屏和天问\r\n");
        g_sub = HMI_SUB_START_ACQ;
        break;
        
    case HMI_SUB_START_ACQ:
        ecg_camp_task();
        pcanet_sample_count = 0;
        pcanet_discard_count = 0;
        pcanet_collect_mode = 1;  /* auth mode */
        DBG_Printf("发送 数据点 到串口屏和天问\r\n");
        g_sub = HMI_SUB_ACQUIRE;
        break;

    case HMI_SUB_ACQUIRE:
        ecg_showwave_task();
        if (pcanet_collect_mode == 1 && pcanet_sample_count >= AUTH_RAW_POINTS) {
            pcanet_collect_mode = 0;
            g_sub = HMI_SUB_COMPARE_SD;
        }
        break;

    case HMI_SUB_COMPARE_SD:
        g_verify_pass = Task_Identity_Authentication(g_username, ads1292_auth_raw, AUTH_RAW_POINTS);
        g_sub = HMI_SUB_SEND_RESULT;
        break;

    case HMI_SUB_SEND_RESULT:
        if (g_verify_pass) {
            if (state == HMI_STATE_ADMIN_VERIFY) {
                if (strcmp(g_username, "FX") == 0) {
                    HMI_SendToScreen(PAGE_ADM_S);
                    HMI_SendToTianwen(TW_CMD_PAGE_ADM_S);
                    DBG_Printf("管理员验证通过 -> page adm_s\r\n");
                } else {
                    HMI_SendToScreen(PAGE_DEFAULT);
                    HMI_SendToTianwen(TW_CMD_PAGE_DEFAULT);
                    DBG_Printf("验证拦截：普通用户(%s)尝试进入管理界面 -> page default\r\n", g_username);
                }
            } else {
                HMI_SendToScreen(PAGE_USER_S);
                HMI_SendToTianwen(TW_CMD_PAGE_USER_S);
                DBG_Printf("用户验证通过 -> page user_s\r\n");
            }
        } else {
            HMI_SendToScreen(PAGE_DEFAULT);
            HMI_SendToTianwen(TW_CMD_PAGE_DEFAULT);
            DBG_Printf("验证失败 -> page default\r\n");
        }
        g_sub = HMI_SUB_DONE;
        break;

    case HMI_SUB_DONE:
        DBG_Printf("模拟: ECG停止采集\r\n");
        g_state = HMI_STATE_IDLE;
        g_sub   = HMI_SUB_IDLE;
        break;

    default:
        g_state = HMI_STATE_IDLE;
        g_sub   = HMI_SUB_IDLE;
        break;
    }
}

/**
 * 注册流程 (用户 / 管理员)
 *  等待用户名 -> START_ACQ -> SHOW_WAVE -> ACQUIRE -> WRITE_SD -> SEND_RESULT -> DONE
 */
static void Task_ProcessRegister(HMI_State state)
{
    uint8_t permission = (state == HMI_STATE_ADMIN_REGISTER) ? 1 : 0;

    switch (g_sub) {
    case HMI_SUB_IDLE:
        DBG_Printf("[HMI]   等待接收用户名...\r\n");
        g_sub = HMI_SUB_WAIT_USERNAME;
        break;

    case HMI_SUB_WAIT_USERNAME:
        if ((Username_Read(&g_rx5) == 0) || (Username_Read(&g_rx7) == 0)) {
            DBG_Printf("[HMI]   用户名: %s, 权限: %d\r\n", g_username, permission);
            g_sub = HMI_SUB_START_ACQ;
        }
        break;

    case HMI_SUB_START_ACQ:
        DBG_Printf("[HMI] Start registration collection...\r\n");
        pcanet_sample_count = 0;
        pcanet_discard_count = 0;
        pcanet_collect_mode = 2;  /* register mode */
        g_sub = HMI_SUB_SHOW_WAVE;
        break;

    case HMI_SUB_SHOW_WAVE:
        HMI_SendToScreen(PAGE_WAVE);
        HMI_SendToTianwen(TW_CMD_PAGE_WAVE);
        DBG_Printf("[HMI]   发送 page wave\r\n");
        g_tick_wait = HAL_GetTick() + 2000;
        g_sub = HMI_SUB_ACQUIRE;
        break;

    case HMI_SUB_ACQUIRE:
        ecg_showwave_task();
        if (pcanet_collect_mode == 2 && pcanet_sample_count >= REG_RAW_POINTS) {
            pcanet_collect_mode = 0;
            DBG_Printf("[HMI] Registration data collection done.\r\n");
            g_sub = HMI_SUB_WRITE_SD;
        }
        break;

    case HMI_SUB_WRITE_SD:
        DBG_Printf("[HMI] Writing registration to SD card...\r\n");
        Task_Identity_Registration(g_username, ads1292_reg_raw, REG_RAW_POINTS);
        g_sub = HMI_SUB_SEND_RESULT;
        break;

    case HMI_SUB_SEND_RESULT:
        HMI_SendToScreen(PAGE_R_S);
        HMI_SendToTianwen(TW_CMD_PAGE_R_S);
        DBG_Printf("[HMI]   注册成功 -> page r_s\r\n");
        g_sub = HMI_SUB_DONE;
        break;

    case HMI_SUB_DONE:
        DBG_Printf("[HMI]   模拟: ECG停止采集\r\n");
        g_state = HMI_STATE_IDLE;
        g_sub   = HMI_SUB_IDLE;
        break;

    default:
        g_state = HMI_STATE_IDLE;
        g_sub   = HMI_SUB_IDLE;
        break;
    }
}

/** 开锁: 通知天问 -> 延时5s -> 关锁 */
static void Task_ProcessUnlock(void)
{
    switch (g_sub) {
    case HMI_SUB_IDLE:
        DBG_Printf("[HMI]   模拟: 向天问发送开锁指令, 电磁锁打开\r\n");
        HMI_SendToTianwen(TW_CMD_UNLOCK);
        g_tick_wait = HAL_GetTick() + 5000;
        g_sub = HMI_SUB_DONE;
        break;

    case HMI_SUB_DONE:
        if (HAL_GetTick() < g_tick_wait) break;
        DBG_Printf("[HMI]   模拟: 5秒到, 电磁锁自动关闭\r\n");
        g_state = HMI_STATE_IDLE;
        g_sub   = HMI_SUB_IDLE;
        break;

    default:
        g_state = HMI_STATE_IDLE;
        g_sub   = HMI_SUB_IDLE;
        break;
    }
}

/** System Format: delete all user files except FX admin */
static void Task_ProcessSysInit(void)
{
    FRESULT fr;
    uint8_t deleted_count = 0;

    switch (g_sub) {
    case HMI_SUB_IDLE:
        DBG_Printf("[HMI] ===== SYS Format Start =====\r\n");
        fr = f_mount(&sd_fs, "0:", 1);
        if (fr == FR_OK) {
            g_sub = HMI_SUB_LIST_SD;
        } 
				else 
				{
            DBG_Printf("[HMI] SD mount FAILED: %d\r\n", fr);
            g_sub = HMI_SUB_DONE;
        }
        break;

    case HMI_SUB_LIST_SD:
    {
        DIR dir;
        FILINFO fno;
        fr = f_opendir(&dir, "");
        if (fr == FR_OK) {
            DBG_Printf("[HMI] Scanning files...\r\n");
            for (;;) {
               fr = f_readdir(&dir, &fno);
               if (fr != FR_OK || fno.fname[0] == 0) break;

              char *dot = strrchr(fno.fname, '.');
              if (!dot) continue;

              /* Skip directories (f_unlink only works on files) */
              if (fno.fattrib & AM_DIR) continue;

                /* Keep FX admin files, delete all other files */
                if (fno.fname[0] == 'F' && fno.fname[1] == 'X' &&
                    (fno.fname[2] == '\0' || fno.fname[2] == '.' || fno.fname[2] == '_')) {
                    DBG_Printf("[HMI] KEEP: %s (FX admin)\r\n", fno.fname);
                    continue;
                }

                fr = f_unlink(fno.fname);
                if (fr == FR_OK) {
                    DBG_Printf("[HMI] DELETE: %s\r\n", fno.fname);
                    deleted_count++;
                } else {
                    DBG_Printf("[HMI] DELETE FAIL: %s (err=%d)\r\n", fno.fname, fr);
                }
            }
            f_closedir(&dir);
        }

        DBG_Printf("[HMI] Format done, deleted %d files. FX preserved.\r\n", deleted_count);
        g_sub = HMI_SUB_DONE;
        break;
    }

    case HMI_SUB_DONE:
        DBG_Printf("[HMI] ===== SYS Format End =====\r\n");
        HMI_SendToScreen(PAGE_INIT);
        HMI_SendToTianwen(TW_CMD_PAGE_INIT);
        g_state = HMI_STATE_IDLE;
        g_sub   = HMI_SUB_IDLE;
        break;

    default:
        g_state = HMI_STATE_IDLE;
        g_sub   = HMI_SUB_IDLE;
        break;
    }
}
/** 用户管理: 列出用户 -> 接收用户名 -> 删除 */
static void Task_ProcessManageUsers(void)
{
    FRESULT fr;
    switch (g_sub) {
    case HMI_SUB_IDLE:
        DBG_Printf("[HMI]   读取SD卡所有用户列表...\r\n");
        fr = f_mount(&sd_fs, "0:", 1);
        if (fr == FR_OK) {
            HMI_SendToScreen(PAGE_MANAGE);
            HMI_SendToTianwen(TW_CMD_PAGE_MANAGE);
        }
        g_sub = HMI_SUB_LIST_SD;
        break;

    case HMI_SUB_LIST_SD:
    {
        DIR dir;
        FILINFO fno;
        uint8_t user_count = 0;
        fr = f_opendir(&dir, "");
        if (fr == FR_OK) {
            DBG_Printf("[HMI]   SD卡用户列表:\r\n");
            for (;;) {
                fr = f_readdir(&dir, &fno);
                if (fr != FR_OK || fno.fname[0] == 0) break;
                char *dot = strrchr(fno.fname, '.');
                if (dot && strcmp(dot, ".DAT") == 0) {
                    size_t name_len = (size_t)(dot - fno.fname);
                    if (name_len > 63) name_len = 63;
                    DBG_Printf("    - %.*s\r\n", (int)name_len, fno.fname);
                    user_count++;
                    /* 向串口屏推送用户名 */
                    {
                        char screen_cmd[96];
                        snprintf(screen_cmd, sizeof(screen_cmd),
                                 "user.t%d.txt=\"%.*s\"", user_count, (int)name_len, fno.fname);
                        HMI_SendToScreen(screen_cmd);
                    }
                }
            }
            f_closedir(&dir);
        }
        if (user_count == 0) {
            DBG_Printf("[HMI]   (暂无注册用户)\r\n");
        }
        DBG_Printf("[HMI]   共 %d 个用户\r\n", user_count);
       DBG_Printf("[HMI]   请在串口屏上选择要删除的用户\r\n");
        g_tick_wait = HAL_GetTick() + 5000;
       g_sub = HMI_SUB_WAIT_USERNAME;
        break;
    }

  case HMI_SUB_WAIT_USERNAME:
      DBG_Printf("[HMI]   等待选择要删除的用户...\r\n");
        if (HAL_GetTick() >= g_tick_wait) {
            DBG_Printf("[HMI]   用户选择超时，退出用户管理\r\n");
            g_sub = HMI_SUB_DONE;
        } else
        if ((Username_Read(&g_rx5) == 0) || (Username_Read(&g_rx7) == 0)) {
           DBG_Printf("[HMI]   准备删除用户: %s\r\n", g_username);
           g_sub = HMI_SUB_DELETE_USER;
       }
       break;

    case HMI_SUB_DELETE_USER:
    {
        char filename_dat[64];
        char filename_txt[64];
        char filename_cfg[64];
        snprintf(filename_dat, sizeof(filename_dat), "%s.DAT", g_username);
        snprintf(filename_txt, sizeof(filename_txt), "%s_RAW.TXT", g_username);
        snprintf(filename_cfg, sizeof(filename_cfg), "%s_CFG.TXT", g_username);

        DBG_Printf("[HMI]   删除用户文件: %s, %s, %s\r\n", filename_dat, filename_txt, filename_cfg);
        fr = f_mount(&sd_fs, "0:", 1);
        if (fr == FR_OK) {
            f_unlink(filename_dat);
            f_unlink(filename_txt);
            f_unlink(filename_cfg);
            f_mount(NULL, "", 0);
            DBG_Printf("[HMI]   用户 %s 已删除\r\n", g_username);
        } else {
            DBG_Printf("[HMI]   SD卡挂载失败，无法删除用户 %s\r\n", g_username);
        }
        g_sub = HMI_SUB_DONE;
        break;
    }

    case HMI_SUB_DONE:
        g_state = HMI_STATE_IDLE;
        g_sub   = HMI_SUB_IDLE;
        break;

    default:
        g_state = HMI_STATE_IDLE;
        g_sub   = HMI_SUB_IDLE;
        break;
    }
}

/* ========== 公开接口实现 ========== */

void HMI_Init(void)
{
    RingBuf_Init(&g_rx5);
    RingBuf_Init(&g_rx7);

    g_state = HMI_STATE_IDLE;
    g_sub   = HMI_SUB_IDLE;

    /*
     * 使能UART中断(若MX未配置NVIC,在此补充)
     * HAL_UART_Receive_IT 内部会检查NVIC, 若未使能可在此打开
     */
    HAL_NVIC_SetPriority(UART5_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(UART5_IRQn);
    HAL_NVIC_SetPriority(UART7_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(UART7_IRQn);

    /* 启动单字节中断接收 */
    HAL_UART_Receive_IT(&huart5, &g_rx5_byte, 1);
    HAL_UART_Receive_IT(&huart7, &g_rx7_byte, 1);

    /* 系统启动, 进入初始页面 */
    HMI_SendToScreen(PAGE_INIT);
    HMI_SendToTianwen(TW_CMD_PAGE_INIT);

    DBG_Printf("[HMI] HMI模块初始化完成, 等待指令...\r\n");
}

void HMI_TaskProcess(void)
{
    uint8_t  b1, b2;
    uint16_t cmd;

    /* 空闲时用滑动窗口轮询串口, 自动丢弃串口屏状态/无关返回值 */
    if (g_state == HMI_STATE_IDLE) {
        /* UART5 - 串口屏: 滑动窗口搜索有效2字节命令 */
        while (RingBuf_Used(&g_rx5) >= 2) {
            RingBuf_Peek(&g_rx5, 0, &b1);
            RingBuf_Peek(&g_rx5, 1, &b2);
            cmd = ((uint16_t)b1 << 8) | b2;
            if (IsValidCmd(cmd)) {
                RingBuf_Get(&g_rx5, &b1);
                RingBuf_Get(&g_rx5, &b2);
                Cmd_Dispatch(cmd);
                return;
            }
            /* 非有效命令字节, 丢弃1字节后窗口右移 */
            RingBuf_Get(&g_rx5, &b1);
        }
        /* UART7 - 天问: 滑动窗口搜索有效2字节命令 */
        while (RingBuf_Used(&g_rx7) >= 2) {
            RingBuf_Peek(&g_rx7, 0, &b1);
            RingBuf_Peek(&g_rx7, 1, &b2);
            cmd = ((uint16_t)b1 << 8) | b2;
            if (IsValidCmd(cmd)) {
                RingBuf_Get(&g_rx7, &b1);
                RingBuf_Get(&g_rx7, &b2);
                Cmd_Dispatch(cmd);
                return;
            }
            RingBuf_Get(&g_rx7, &b1);
        }
    }

    /* 执行当前任务的状态机 */
    switch (g_state) {
    case HMI_STATE_IDLE:
        /* 空闲时无任务，继续轮询 */
        break;

    case HMI_STATE_USER_VERIFY:
        Task_ProcessVerify(g_state);
        break;

    case HMI_STATE_ADMIN_VERIFY:
        Task_ProcessVerify(g_state);
        break;

    case HMI_STATE_USER_REGISTER:
        Task_ProcessRegister(g_state);
        break;

    case HMI_STATE_ADMIN_REGISTER:
        Task_ProcessRegister(g_state);
        break;

    case HMI_STATE_UNLOCK:
        Task_ProcessUnlock();
        break;

    case HMI_STATE_SYS_INIT:
        Task_ProcessSysInit();
        break;

    case HMI_STATE_MANAGE_USERS:
        Task_ProcessManageUsers();
        break;

    default:
        DBG_Printf("[HMI] >> STATE: UNKNOWN(%d), reset to IDLE\r\n", g_state);
        g_state = HMI_STATE_IDLE;
        g_sub   = HMI_SUB_IDLE;
        break;
    }
}

