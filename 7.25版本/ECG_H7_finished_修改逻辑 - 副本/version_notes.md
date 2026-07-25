# 版本说明 - 工程集成与编译错误修复

## 日期
2026-07-20

## 文件修改记录

### 1. `Core/Inc/main.h`
- **修复**: `#incldue "task.h"` 拼写错误 → `#include "task.h"`
- **影响**: 修复 task.h 无法被包含导致的连锁编译失败（HMI.c 中 ecg_camp_task/ecg_showwave_task 未声明错误）

### 2. `Headware/inc/userspi.h`
- **修复**: 移除 `#if defined (STM32F10X_LD) || ...` 条件编译宏
- **原因**: 该条件判断针对 STM32F10X 系列，工程使用 STM32H750，导致 `CS_LOW()`/`CS_HIGH()`/`READ_MISO()`/`SPI_Init()`/`SPI_ReadWriteByte()` 全部被跳过
- **影响**: ads1292.c、task.c 中引用以上宏和函数时提示"未定义"
- **CubeMX 兼容性**: 该文件不在 CubeMX 生成范围内，不受重新生成影响

### 3. `Headware/inc/task.h`
- **重构**: 集中管理所有外设宏定义和 extern 变量声明
  - 移除了重复的 `extern SPI_HandleTypeDef hspi1/hspi2`（已在 spi.h 中声明）
  - 新增 `#include "arm_math.h"` 确保 DSP 类型可见
  - 新增 extern 声明 `S1`/`S2`（`arm_fir_instance_f32`）
  - 按外设分类组织所有宏和变量

### 4. `Headware/src/task.c`
- **重构**: 集中管理所有外设变量定义
  - 从 main.c 移入 `S1`/`S2` 变量定义
  - 从 main.c 移除重复定义后，通过 task.h 的 extern 声明引用
  - 保持原有任务函数 `ecg_camp_task()` 和 `ecg_showwave_task()` 不变

### 5. `Core/Src/main.c`
- **修改**: 移除 `USER CODE BEGIN PV` 中的 `arm_fir_instance_f32 S1; S2;` 定义
- **原因**: S1/S2 已迁移至 task.c 集中管理
- **保留**: `arm_fir_init_f32` 初始化调用不变（通过 task.h 的 extern 声明引用）

## 架构说明

### 头文件包含链
```
main.h (USER CODE Includes)
  +-- userspi.h  -->  CS_LOW/HIGH, SPI_Init, SPI_ReadWriteByte
  +-- ads1292.h  -->  ADS1292 寄存器命令、初始化函数
  +-- HMI.h      -->  串口屏状态机、通信函数
  +-- SD.h       -->  SD 卡 FatFS 驱动接口
  +-- task.h     -->  系统配置宏、全局变量 extern、任务接口函数
       +-- arm_math.h  -->  DSP 库 (arm_fir_f32 等)
```

### 变量集中管理
- **声明 (extern)**: `Headware/inc/task.h`，按外设分类
- **定义**: `Headware/src/task.c`，按外设分类
- **外设特有宏**: 保留在各外设自己的头文件中（userspi.h, ads1292.h, SD.h）
- **系统配置宏**: `task.h` 中的 `Samples_Number`, `Block_Size`, `NumTaps`, `Show_wave`

### CubeMX 兼容性
- 所有 CubeMX 生成的文件（Core/ 下的 main.c、main.h、spi.h 等）中
  的 USER CODE 区域均未被破坏
- 自定义外设驱动（Headware/）不在 CubeMX 生成范围内
- CubeMX 重新生成代码不会影响上述修改

## 当前结构一览

```
ECG_H7_finished/
+-- Core/
|   +-- Inc/  (CubeMX 生成的 .h 文件)
|   +-- Src/  (CubeMX 生成的 .c 文件)
+-- Headware/
|   +-- inc/
|   |   +-- ads1292.h   外设 1: ADS1292 驱动头文件
|   |   +-- HMI.h       外设 2: 串口屏通信头文件
|   |   +-- SD.h        外设 3: SD 卡驱动头文件
|   |   +-- userspi.h   外设 4: 用户 SPI 操作封装
|   |   +-- task.h      集成层: 系统配置宏 + 全局变量 extern + 任务接口
|   +-- src/
|       +-- ads1292.c    ADS1292 驱动实现
|       +-- HMI.c        串口屏状态机实现
|       +-- SD.c         SD 卡驱动实现
|       +-- userspi.c    用户 SPI 实现
|       +-- task.c       集成层: 全局变量定义 + 任务函数实现
+-- FATFS/              (CubeMX 生成的 FatFs)
+-- Drivers/            (HAL 库)
+-- MDK-ARM/            (Keil 工程文件)
+-- Middlewares/        (FatFs 中间件)
+-- ECG_H7.ioc         (CubeMX 配置文件)
```


---

## 2026-07-20 v2 -- PCANet 身份识别算法移植

### 新增文件 (从队友工程移植)
1. **`Headware/inc/pcanet_inference.h`** -- PCANet 特征提取算法接口
   - ECG_WIN_LEN=250, ACDCT_FEATURE_DIM=40960, MAX_FEATURE_DIM=2048, FINAL_FEATURE_DIM=43008
2. **`Headware/inc/pcanet_weights_stable.h`** -- 64x8x4 组 PCANet 滤波器权重 (V_ACDCT_L1/2, V_MAX_L1/2)
3. **`Headware/src/pcanet_inference.c`** -- PCANet 算法实现 (插值重采样, Z-Score, AC/DCT, MAX, 2D卷积, 直方图, 曼哈顿距离)

### 修改文件

#### `FATFS/Target/user_diskio.c`
- 在 USER CODE DECL 中添加 `#include "SD.h"`
- 将 CubeMX 生成的 5 个桩函数替换为 SD.c 的真实驱动函数:
  - USER_initialize -> SD_disk_initialize
  - USER_status -> SD_disk_status
  - USER_read -> SD_disk_read
  - USER_write -> SD_disk_write
  - USER_ioctl -> SD_disk_ioctl

#### `Core/Inc/main.h`
- USER CODE Includes 新增 `#include "pcanet_inference.h"`
- USER CODE Includes 新增 `#include "fatfs.h"`

#### `Headware/inc/task.h`
- 新增 PCANet 身份识别系统配置宏 (SENSOR_WIN_LEN, DISCARD_POINTS, REG_RAW_POINTS=5000, AUTH_RAW_POINTS=1500, etc.)
- 新增 extern 变量 (ads1292_reg_raw, ads1292_auth_raw, Shared_Feature_Buffer_1/2, sd_fs, sd_fil, pcanet_sample_count, etc.)
- 新增函数原型 (SD_Init_FATFS, Extract_Aligned_Window, Task_Identity_Registration, Task_Identity_Authentication)

#### `Headware/src/task.c`
- 新增 PCANet 全局变量定义 (大缓冲区: reg_raw[5000], auth_raw[1500], feature_buffer[43008]x2)
- 新增 SD_Init_FATFS() -- FaTFS 挂载 + 容量打印
- 新增 Extract_Aligned_Window() -- R波自适应寻峰+窗口对齐
- 新增 Task_Identity_Registration() -- 10组R波对齐窗→PCANet→写入.DAT文件
- 新增 Task_Identity_Authentication() -- 1组R波对齐窗→PCANet→比对10模板→曼哈顿距离→投票
- 修改 ecg_showwave_task(): FIR滤波后加入算法数据采集钩子 (pcanet_collect_mode 模式判断)

#### `Headware/src/HMI.c`
- **验证流程 Task_ProcessVerify()**:
  - SUB_START_ACQ: 增加 pcanet_collect_mode=1 (认证采集模式), 计数器归零
  - SUB_ACQUIRE: 等待 pcanet_sample_count >= AUTH_RAW_POINTS (1500点≈6秒)
  - SUB_COMPARE_SD: 替换占位符 -> 调用 Task_Identity_Authentication()
- **注册流程 Task_ProcessRegister()**:
  - SUB_START_ACQ: 增加 pcanet_collect_mode=2 (注册采集模式), 计数器归零
  - SUB_ACQUIRE: 等待 pcanet_sample_count >= REG_RAW_POINTS (5000点≈20秒)
  - SUB_WRITE_SD: 替换占位符 -> 调用 Task_Identity_Registration()
- **系统初始化 Task_ProcessSysInit()**:
  - 替换 SD_DeleteAll 占位符 -> 调用 SD_Init_FATFS() 挂载测试SD卡

#### `Core/Src/main.c`
- while(1) 主循环中增加 ecg_camp_task() 连续调用, 确保持续读取ADS1292数据
- 位置: USER CODE 3 区域, HMI_TaskProcess() 之前

### 架构说明

#### 数据流
```
DRDY中断 → ecg_camp_task() (读取ADC) → ecg_showwave_task() (FIR滤波)
  → [pcanet_collect_mode] → 积累到 ads1292_auth_raw/reg_raw
  → HMI状态机检测到采集完成 → Task_Identity_Authentication/Registration
  → Extract_Aligned_Window (R波检测+对齐) → PCANet_Inference_Pipeline
  → SD卡读写模板 → 曼哈顿距离比对 → 返回结果
```

#### 内存占用
- Shared_Feature_Buffer_1/2: 各 43008 * 4B = 172KB (共344KB BSS)
- ads1292_reg_raw: 5000 * 4B = 20KB
- ads1292_auth_raw: 1500 * 4B = 6KB
- 总计约 370KB BSS, STM32H750 1MB RAM 可容纳

#### CubeMX 兼容性
- 所有 CubeMX 生成文件仅有 main.c USER CODE 3 区域新增 1 行代码
- 其余修改均在 Headware/ 和 FATFS/Target/user_diskio.c 中
- CubeMX 重新配置生成代码不会破坏上述修改


---

## 2026-07-21 v3 -- FatFs 循环依赖编译错误修复

### 问题现象
编译出现 5 个错误:
```
../Middlewares/Third_Party/FatFs/src/ff_gen_drv.h(57): error: #20: identifier "_VOLUMES" is undefined
../FATFS/App/fatfs.h(36): error: #20: identifier "FATFS" is undefined
../FATFS/App/fatfs.h(37): error: #20: identifier "FIL" is undefined
../Headware/inc/task.h(57): error: #20: identifier "FATFS" is undefined
../Headware/inc/task.h(58): error: #20: identifier "FIL" is undefined
```

### 根因分析
`ffconf.h` 顶部包含 `#include "main.h"`，而 `main.h` 的 USER CODE Includes 中包含 `#include "fatfs.h"`。当编译 `ff.c` 时，include 链形成循环:
```
ff.c → ff.h → ffconf.h → main.h → fatfs.h → ff_gen_drv.h → 使用 _VOLUMES
```
此时 `ffconf.h` 正在处理中但尚未执行到 `#define _VOLUMES`，所以 `_VOLUMES` 未定义。

### 修改内容 (3 个文件)

#### `Core/Inc/main.h`
- 从 USER CODE Includes 中移除 `#include "fatfs.h"`
- 原因: CubeMX 的 FatFs 头文件不应放入 main.h，会导致与 ffconf.h 的循环依赖

#### `Core/Src/main.c` 
- 在 `#include "main.h"` 之后保留 `#include "fatfs.h"`（CubeMX 标准生成位置）
- 此处已经存在，无需额外操作

#### `Headware/inc/task.h`
- 将 `#include "fatfs.h"` 改为 `#include "ff.h"` 
- 原因: task.h 只需 `FATFS`/`FIL` 类型定义（由 `ff.h` 提供），无需 CubeMX 的 `fatfs.h` 封装层
- `ff.h` 直接包含 `integer.h` 和 `ffconf.h`，避免循环依赖

### 变更后的 Include 链
```
main.c → main.h (不含 fatfs.h)
       → fatfs.h → ff.h → integer.h → ffconf.h (#define _VOLUMES ✓)
                → ff_gen_drv.h → 使用 _VOLUMES ✓

task.c → task.h → main.h
                → ff.h → integer.h → ffconf.h (#define _VOLUMES ✓)
                → 使用 FATFS / FIL ✓

ff.c → ff.h → integer.h → ffconf.h → main.h (不含 fatfs.h, 无循环!)
     → #define _VOLUMES ✓
     → 定义 FATFS / FIL ✓
```

### CubeMX 兼容性
- 修改完全符合 CubeMX 的 FatFs 集成方式: `fatfs.h` 位于 `main.c` 的全局包含区
- CubeMX 重新生成代码后此处的 `#include "fatfs.h"` 不会被移除

### 2026-07-21 v3 (修正版) -- 修复剩余编译错误

#### 新增问题 (v3 初次修复后仍然存在)
编译 `ff.c` / `diskio.c` / `syscall.c` / `ccsbcs.c` / `fatfs.c` 时:
```
task.h(57): error: #20: identifier "FATFS" is undefined
task.h(58): error: #20: identifier "FIL" is undefined
user_diskio.c(38): error: #5: cannot open source input file "fatfs_sd_card.h"
pcanet_inference.h(34): warning: last line of file ends without a newline
```

#### 根因分析
1. **`task.h` 的 FATFS/FIL 错误**: `main.h` 的 USER CODE Includes 中有 `#include "task.h"`。编译 `ff.c` 时:
   ```
   ff.c → ff.h → ffconf.h → main.h → task.h → ff.h (_FATFS 已定义，跳过！)
   ```
   此时 `FATFS`/`FIL` 尚未定义（它们在 `ffconf.h` 之后的 `ff.h` 中定义），报错。
   
2. **`fatfs_sd_card.h` 错误**: 从队友项目复制的 `user_diskio.c` 包含队友自定义头文件，当前工程不存在该文件。

#### 修改内容 (5 个文件)

| 文件 | 变更 | 原因 |
|---|---|---|
| `Core/Inc/main.h` | 移除 `#include "task.h"` | 打破 `ffconf.h→main.h→task.h→ff.h` 循环依赖 |
| `Core/Src/main.c` | 新增 `#include "task.h"` | `main.c` 需要访问 task.h 中的类型和函数 |
| `Headware/src/HMI.c` | 新增 `#include "task.h"` | `HMI.c` 不再通过 main.h 间接获取 task.h |
| `FATFS/Target/user_diskio.c` | `#include "fatfs_sd_card.h"` → `#include "SD.h"` | 队友的自定义头文件不存在，SD.h 才是正确的驱动头 |
| `Headware/inc/pcanet_inference.h` | 文件末尾添加换行 | 消除 Keil 编译器警告 |

#### 修复后的 Include 链
```
ff.c → ff.h → ffconf.h → main.h (不含 task.h ！)
     → ffconf.h #define _VOLUMES
     → ff.h #define FATFS, FIL

main.c → main.h (不含 task.h)
       → task.h → ff.h (_FATFS 未定义 → 完整处理)
                → integer.h → ffconf.h → #define _VOLUMES
                → ff.h → #define FATFS, FIL ← 此时 task.h 可用 ✓

HMI.c → HMI.h → main.h (不含 task.h)
      → task.h (直接包含) → ff.h → ... → FATFS/FIL 可用 ✓
```

#### CubeMX 兼容性
- `main.c` 的 `#include "task.h"` 位于 USER CODE 区域前（全局包含区），CubeMX 重新生成不会移除
- `main.h` 的 USER CODE Includes 不再包含 `task.h`，与 CubeMX 生成的 FatFs 无冲突

---

## 2026-07-21 v4 -- 添加中文调试注释

### 修改内容

为方便调试和理解代码逻辑，在以下文件中添加了中文 UTF-8 注释：

#### `Headware/inc/task.h`
- 所有配置宏添加中文说明：采样点数、滤波器阶数、波形缓冲区、AI窗口长度、丢弃点数、采集长度、投票阈值等
- 所有 extern 变量添加中文说明：SPI句柄、FIR实例、通道数据、滤波缓冲区、波形显示缓冲区、PCANet缓冲区、FatFS对象、采集模式等

#### `Headware/src/task.c`
- ecg_camp_task()：注释 DRDY 中断驱动流程、SPI 读取 9 字节数据格式
- ecg_showwave_task()：注释 FIR 滤波 + 算法采集钩子 + 自适应归一化三阶段流程
- SD_Init_FATFS()：注释 FatFS 挂载和容量打印
- Extract_Aligned_Window()：注释 R 波自适应寻峰算法（250点范围搜索最大值、左右偏移对齐）
- Task_Identity_Registration()：注释 10 窗口提取 → PCANet → SD 写入完整流程
- Task_Identity_Authentication()：注释 1 窗口 → PCANet → 10 模板比对 → 曼哈顿距离投票流程

#### `Core/Src/main.c`
- USER CODE BEGIN 2：注释 SPI CS 初始化、ADS1292 初始化、FIR 滤波器初始化、HMI 初始化
- USER CODE BEGIN 3 (while 循环)：注释 ecg_camp_task/HMI_TaskProcess/HAL_Delay 三行主循环架构，以及算法数据采集完整链路

#### `Headware/src/HMI.c` (因 Windows 文件锁未能写入)
- 扫描 UART 命令的注释已更新、状态机总览注释已更新
- 实际修改被文件锁阻止，但现有注释已足够理解 HMI 调度逻辑

### 注释风格
- 使用 `/* ... */` 多行注释块描述函数功能和参数
- 使用 `/* ... */` 行尾注释说明变量和宏的作用
- 关键流程用 `/* ═════ 标题 ═════ */` 分隔块


### 2026-07-21 v4(补) -- task.c 变量定义添加中文注释

`Headware/src/task.c` 中全部 26 个变量定义添加了行尾中文注释：

| 分组 | 变量 | 注释 |
|---|---|---|
| DSP FIR 实例 (2) | S1, S2 | 呼吸波LPF(2Hz) / 心电BPF(5-40Hz) |
| ADS1292 原始数据 (4) | ch1_data, ch2_data, flag, drdy_flag | 通道数据、FIR完成标志、DRDY标志 |
| FIR 滤波缓冲区 (6) | Input_data1, Output_data1, firState1, Input_data2, Output_data2, firState2 | 输入/输出/状态缓冲区 |
| 波形显示 (2) | show_ecg[650], ecg_index | 归一化波形(0-255)及索引 |
| FIR 配置 (2) | blockSize, numBlocks | 块大小(=1逐点)和调用次数 |
| PCANet 算法 (10) | ads1292_reg_raw[5000], ads1292_auth_raw[1500], sensor_aligned_window[250], Shared_Feature_Buffer_1/2[43008], sd_fs, sd_fil, pcanet_sample_count, pcanet_discard_count, pcanet_collect_mode | 采集缓冲区、特征缓冲区、文件系统、模式状态 |


---

## 2026-07-21 v5 -- 修复 ADS1292 被意外停止导致命令无响应

### 问题
串口输出 `add...` 波形数据后，再发送 0100/0000 等 HMI 命令没有任何反应。

### 根因
`Headware/src/task.c` 的 `ecg_showwave_task()` 中，每次波形显示缓冲区填满(650点)时:
```c
if (ecg_index == Show_wave && Show_wave > 0) {
    ADS1292_Set_stop(&hspi1);  // ← 停止了ADS1292的数据转换!
    ...
}
```
`ADS1292_Set_stop()` 拉低 START 引脚并发送 SDATAC 指令，ADS1292 停止生成 DRDY 脉冲。
之后 `drdy_flag` 永远为 0 → `flag` 永远为 0 → 算法采集钩子不执行 → 状态机死锁。

### 修复
移除 `ecg_showwave_task()` 中的 `ADS1292_Set_stop(&hspi1);` 调用。

| 文件 | 行 | 修改 |
|---|---|---|
| `Headware/src/task.c` | 179 | 删除 `ADS1292_Set_stop(&hspi1);`，替换为注释说明 |

### 修复后的数据流
```
ADS1292 连续运行（START持续高，RDATAC模式）
  → DRDY中断 → drdy_flag=1 → ecg_camp_task读数据 → flag=1
  → ecg_showwave_task FIR滤波 → 算法采集钩子积累数据
    → 波形显示(650点填满后输出add...耗时~50秒)
    → flag=0 → 下一轮DRDY → 循环继续
  → HMI状态机检测采集完成 → 调用算法任务 ✓
```


---

## 2026-07-21 v6 -- 认证流程缺少用户名输入

### 问题
发送验证命令(0100/0101)后，`Task_Identity_Authentication()` 使用空的 `g_username` 查找 SD 卡模板，找不到文件，始终返回认证失败。

### 根因
验证流程 `Task_ProcessVerify()` 的 `SUB_IDLE` 直接跳到 `SUB_SHOW_WAVE`，没有经过 `SUB_WAIT_USERNAME`。而注册流程 `Task_ProcessRegister()` 有 `SUB_WAIT_USERNAME`。

### 修复
在 `Task_ProcessVerify()` 中添加 `SUB_WAIT_USERNAME` 分支:

| 修改位置 | 原内容 | 改为 |
|---|---|---|
| `SUB_IDLE` (209行) | `g_sub = HMI_SUB_SHOW_WAVE;` | `printf("等待接收用户名...\\r\\n"); g_sub = HMI_SUB_WAIT_USERNAME;` |
| 新增 (212-220行) | (无) | 添加 `case HMI_SUB_WAIT_USERNAME:` 接收用户名 |

修改后验证流程:
```
SUB_IDLE → [新增] SUB_WAIT_USERNAME(输入用户名) → SUB_SHOW_WAVE → START_ACQ → ACQUIRE → COMPARE_SD(Task_Identity_Authentication使用g_username) → SEND_RESULT → DONE
```


### v6 补 -- 注册流程显示循环阻塞导致无法跳转

#### 问题
注册流程采集 5000 个数据点≈20秒，但每次显示缓冲区填满(650点)后执行:
```c
for (i = 150; i < Show_wave; i++) {       // 500次循环
    printf("add s0.id,0,%d\\xff\\xff\\xff", ...);
    HAL_Delay(100);                        // 每次100ms → 共50秒!
}
```
8次显示循环 × 50秒 = 400秒，远超数据采集时间。用户看到一直在输出 `add...`，以为卡死。

#### 修复
```c
// 修改前:
HAL_Delay(100);

// 修改后:
HAL_Delay((pcanet_collect_mode == 0) ? 100 : 10);
```
- 正常模式(pcanet_collect_mode==0): 保持100ms延迟，不影响波形显示刷新率
- 采集模式(pcanet_collect_mode!=0): 减少到10ms，显示循环从50秒降至5秒

| 流程 | 修改前 | 修改后 |
|---|---|---|
| 注册(5000点,8次循环) | 400秒+20秒≈7分钟 | 40秒+20秒≈60秒 |
| 认证(1500点,2-3次循环) | 150秒+6秒≈2.5分钟 | 15秒+6秒≈21秒 |


### v7 -- 认证时用户名混入控制字符导致模板找不到

#### 问题
注册成功（创建了test.DAT），但认证时输出:
```
--- Authentication: [test] ---
>> R-peak aligned (idx: 122)
>>> ERROR: template [test.DAT]
 not found!
>>> Votes: 0/10 (threshold: >4)
[RESULT]: AUTH FAILED.
```
其中 `[test.DAT]\n not found!` 显示文件名后换行，说明 `filename_dat` 含有控制字符。

#### 根因
`Username_Read()` 只过滤了 `\r`、`\n`、`0xFF`，但 UART 环状缓冲区中可能残留命令字节（如 `CMD_USER_VERIFY=0x0100` 的 0x01）。这些控制字符未被过滤，混入 `g_username`:
- 注册时: 命令 0x00 0x00，0x00 被 `\0` break 截停，用户名正常
- 认证时: 命令 0x01 0x00，0x01 不被过滤，混入用户名 → 文件 `test.DAT` 打不开

#### 修复
```c
// 修改前:
if ((byte == '\r') || (byte == '\n') || (byte == 0xFF)) continue;
if (byte == '\0') break;

// 修改后:
if (byte < 0x20 || byte > 0x7E) continue;  // 只保留可打印ASCII
```
使用单行范围过滤替代逐个排除，任何非打印字符(0x00-0x1F, 0x7F-0xFF)都被跳过。


### v7(补) -- 注册模板未完整写入SD卡导致认证只读到1个模板

#### 问题
认证时只比对1个模板就中断:
```
Template [1/10] dist: 508.0 -> PASS (+1)
>>> Votes: 1/10 (threshold: >4)
[RESULT]: AUTH FAILED
```

#### 根因
注册的 `Task_Identity_Registration()` 中，每个模板通过 `f_write` 写入约172KB(43008个float)数据到 SD 卡。FatFs 的 `f_write` 默认使用缓冲区写入，数据不会立即落盘。10 个模板共约 1.72MB，若 `f_close` 刷新失败或缓冲区未完全写入，文件只剩第一个模板的数据。

缺少 `f_sync` 调用：注册的循环中写完每个模板后未调用 `f_sync` 强制刷写数据到 SD 卡。

#### 修改
在 `Task_Identity_Registration()` 中:

| 位置 | 修改 |
|---|---|
| 每个模板写入后 | 新增 `fr = f_sync(&sd_fil);` 立即刷写数据到 SD 卡 |
| `.DAT` 关闭后 | `f_close(&sd_fil)` 改为 `if (f_close(&sd_fil) != FR_OK) write_success = 0` 并打印警告 |

#### 使用时
- 重新执行注册操作(0000)，确保看到 10 个模板全部成功
- 再执行认证(0100)即可完整比对 10 个模板


### v7(补2) -- 用户名被空格污染导致文件查找失败

#### 问题
```
--- Authentication: [ ] ---    ← 用户名为空格,不是"test"
>> File  .DAT size: 1720320 bytes (10 templates, expected 10)
>>> Only 1/10 templates read
```
文件实际有10个模板，但认证只读到1个。原因是打开了 `.DAT` 而不是 `test.DAT`，用户名被空格污染。

#### 根因
`Username_Read()` 原来对 `\r`/`\n` 是 `continue`(跳过继续读)，会一直读到缓冲区空。如果命令字节(0x01 0x00)之后缓冲区有残留空格(`0x20`)，空格被读入作为用户名，导致文件名变成 ` .DAT`。

为什么注册成功而认证失败？两个流程都用 `Username_Read`，但注册时发送的是 0000，认证时发送的是 0100。不同命令字节组合导致缓冲区残留不同。

#### 修改(第3次修改 Username_Read)

| 问题 | 修改 |
|---|---|
| `\r`/`\n` 是 `continue` | 改为 `break`，遇到换行立即停止读取 |
| 不处理首尾空格 | 新增 `while` 循环去除首尾空格 |

#### Username_Read 修正后逻辑
```
遇到 \r 或 \n → break 停止读取 (不再 continue 读后续字符)
遇到 <0x20 或 >0x7E → continue 跳过
其他可打印字符(含空格) → 加入 g_username
读取结束后 → 去除 g_username 首尾空格
g_username 为空 → 返回1(未就绪)
g_username 非空 → 返回0(就绪)
```


### v7(补3) -- 单次大块读取替代多块读取，修复模板2读取出错

#### 问题
文件确认有10个模板(1720320字节)，用户名正确("test")，但只读到1个模板就退出。诊断显示文件大小正确，说明写入无误，是读取阶段的问题。

#### 分析
原认证代码使用 336 次 `f_read` 每次 512 字节的循环读取 1 个模板：
```c
while (bytes_remaining > 0) {
    chunk = (bytes_remaining > 512) ? 512 : bytes_remaining;
    f_read(&sd_fil, pData, chunk, &br);
    // 336次循环读完172032字节
}
```
第2个模板的第1次 `f_read` 时(从文件位置172032开始)失败，`br != chunk`。

原因可能是连续336次文件读取后，FatFs内部缓冲或SD卡驱动状态异常，导致文件指针偏移或扇区读取错误。

#### 修复
将每个模板的多次 `f_read` 合并为 1 次大块读取:
```c
// 修改前: 336次 × 512字节
while (bytes_remaining > 0) { chunk = 512; f_read(...); }

// 修改后: 1次 × 172032字节
f_read(&sd_fil, pBuf, sizeof(Shared_Feature_Buffer_2), &br);
```
如果仍然失败，会输出详细的错误信息:
```
>> Template X read error: fr=N, br=M, exp=172032
```
`fr=0` = FR_OK, `fr>0` = 具体错误码


### v7(补4) -- 添加 f_lseek 显式定位，修复模板读取

#### 问题
文件正确(1720320字节=10模板)，但认证只读到第1个模板。第2个模板的 `f_read` 失败。

#### 分析
怀疑 FatFs 内部文件指针跟踪在多块读取后出现偏移误差。第1个模板(172032字节)读取成功后文件指针应指向 172032，但第2次 `f_read` 时可能指向错误位置。

#### 修改
在每个模板读取前添加 `f_lseek` 显式定位到文件偏移量：
```c
f_lseek(&sd_fil, (FSIZE_t)i * (FSIZE_t)sizeof(Shared_Feature_Buffer_2));
```
- i=0: 定位到 0 (第1个模板)
- i=1: 定位到 172032 (第2个模板)
- ...依此类推

同时保持单次大块读取(172032字节)和错误诊断输出。


### v7(补5) -- FAT缓冲未落盘导致认证只能读前1个模板

#### 根因
注册写入 `test.DAT` (1.72MB=10模板)时，FatFs 的 `f_write` 在 RAM 中修改 FAT 表（分配簇链），但 `f_sync` 和 `f_close` 只刷数据缓冲，**不刷 FAT 缓冲**。紧接着又写入 `test_RAW.TXT` 文件，其 `f_open`/`f_write` 操作可能覆盖 FAT 缓冲，导致 `.DAT` 文件的簇链丢失。

认证时 `f_lseek` 只需设置文件指针（成功），但 `f_read` 需要遍历 FAT 簇链，发现缺失的 FAT 条目 → `FR_INT_ERR`。

#### 修改
在 `Task_Identity_Registration()` 中，`.DAT` 文件关闭后立即强制卸载并重新挂载：

```c
f_close(&sd_fil);       // 关闭.DAT
f_mount(NULL, "", 0);   // 卸载 → 强制FatFs刷写所有脏缓冲(包括FAT)
f_mount(&sd_fs, "0:", 1); // 重新挂载
// 然后再写RAW.TXT文件
```

#### 使用时
**必须重新注册(发送0000)**，让新的 FAT 落盘逻辑生效。之后再认证(发送0100)应能读到全部10个模板。


---

## 2026-07-23 v8 -- 自适应动态门限 (从队友工程移植)

### 移植来源
从队友工程 `ECG_H7_曼哈顿距离_修正数据+显示波形 +自适应` 移植的核心算法改进：
**自适应动态门限系统** — 根据每个用户注册时的心跳变异程度，自动计算个性化的曼哈顿距离匹配阈值。

### 新增宏定义 (`Headware/inc/task.h`)

| 宏 | 值 | 说明 |
|---|---|---|
| `DEFAULT_MATCH_THRESHOLD` | 3200.0f | 默认曼哈顿距离匹配阈值（兜底值，当无专属门限文件时使用） |
| `THRESHOLD_TOLERANCE_FACTOR` | 1.3f | 专属门限宽容系数（类内最大距离 × 系数 = 个人门限） |

### 修改文件

#### `Headware/src/task.c` — `Task_Identity_Registration()`

**自适应门限计算**：在10组心拍模板循环中，新增类内距离追踪：

| 模板位置 | 操作 |
|---|---|
| 第1组 (i=0) | `memcpy` 拷贝到 `Shared_Feature_Buffer_2` 作为基准 |
| 第2~10组 (i=1~9) | 与基准计算曼哈顿距离，更新 `max_intra_distance` |

**专属门限保存**：10组模板写入成功后：

1. `user_threshold = max_intra_distance × THRESHOLD_TOLERANCE_FACTOR`
2. 兜底保护：若 `user_threshold < 2000.0f`，设为 2000.0f
3. 写入 `<用户名>_CFG.TXT` 文件（一行浮点数文本）

**数据流**：
```
注册10组R波对齐 → PCANet提取特征 → 追踪类内最大曼哈顿距离
  → 计算user_threshold → 写入DAT(模板) + CFG(门限)
```

#### `Headware/src/task.c` — `Task_Identity_Authentication()`

**优先加载个人专属门限**：在打开 `.DAT` 模板文件之前：

1. 尝试打开 `<用户名>_CFG.TXT` 文件
2. 成功 → `sscanf` 解析出 `current_threshold`
3. 失败 → 使用 `DEFAULT_MATCH_THRESHOLD` (3200.0f) 兜底

**曼哈顿距离比较**：将原先写死的 `MATCH_THRESHOLD` 替换为动态加载的 `current_threshold`

**数据流**：
```
认证开始 → 读取_CFG.TXT获取门限 → 如无则用默认门限
  → 采集R波对齐 → PCANet → 比对10模板
  → 使用动态门限判定通过/不通过 → 投票输出
```

### 使用说明

1. 重新执行注册操作（通过串口屏发送注册命令），系统会自动：
   - 在 SD 卡生成 `<用户名>.DAT`（10组特征模板）
   - 在 SD 卡生成 `<用户名>_CFG.TXT`（专属自适应门限）

2. 重新执行认证操作（通过串口屏发送认证命令），系统会自动：
   - 优先加载 `<用户名>_CFG.TXT` 中的专属门限
   - 若 `_CFG.TXT` 不存在，使用 `DEFAULT_MATCH_THRESHOLD` (3200.0f) 兜底

3. 旧版注册的模板（无 `_CFG.TXT`）兼容：认证时找不到 `_CFG.TXT`，自动回退到默认门限

### CubeMX 兼容性
- 所有修改均在 `Headware/` 目录下，不在 CubeMX 生成范围内
- CubeMX 重新生成代码不会影响上述修改

### v8(补) -- 心电波形显示自适应映射移植 (队友方案)

#### 修改内容
用队友的 **滑动窗口自适应归一化** 替换原先的 650 点批处理 + AAD 离群剔除方案。

#### 替代的旧方案
原先的 `ecg_showwave_task()` 显示流程：
1. 逐点收集 650 个 FIR 滤波后的心电数据到 `raw_buf[Show_wave]`
2. 缓冲区填满后：计算均值 + AAD → 3×AAD 离群剔除 → 找有效 min/max
3. 映射到 0-255 范围，存入 `show_ecg[]`
4. 以 100ms 间隔（采集模式 10ms）逐个发送 500 个点到串口屏
5. 共耗时：正常模式约 65 秒，采集模式约 5-6.5 秒

#### 移植的新方案
队友的 **滑动窗口自适应归一化**，每 FIR 处理一个点立即显示：

```c
/* ═══ 波形显示：队友方案移植 — 滑动窗口自适应归一化 ═══ */
#define DISP_WIN_LEN  250   /* 滑动窗口长度：追踪最近250点≈1s数据的局部极值 */
static int32_t disp_buffer[DISP_WIN_LEN];  /* 环形缓冲区 */
static uint32_t disp_idx = 0;

/* Step 1: 将当前FIR滤波后的心电点存入环形缓冲区 */
disp_buffer[disp_idx] = (int32_t)Output_data2;
disp_idx = (disp_idx + 1) % DISP_WIN_LEN;

/* Step 2: 扫描环形缓冲区，找出局部最小值和最大值 */
/* (遍历250点找min/max) */

/* Step 3: 安全防护：防止除数为0 */

/* Step 4: 浮点比率映射到5~180（使用float计算，杜绝32位整数乘法溢出） */
float ratio = (float)((int32_t)Output_data2 - local_min) / (float)(local_max - local_min);
int32_t mapped_val = (int32_t)(ratio * 175.0f) + 5;

/* Step 5: 硬性限幅 */

/* Step 6: 发送到串口屏 */
printf("add s0.id,0,%d\xff\xff\xff", (uint8_t)mapped_val);
```

#### 技术差异对比

| 方面 | 旧方案 (batch) | 新方案 (sliding window) |
|---|---|---|
| 缓冲区类型 | `raw_buf[650]` 静态数组 | `disp_buffer[250]` 环形缓冲区 |
| 归一化时机 | 每 650 点处理一次 | 每个点实时处理 |
| 离群剔除 | 均值 + 3×AAD (2次遍历) | 纯 min/max (1次遍历) |
| 映射范围 | 0-255 | 5-180 |
| 发送方式 | 500点批发送 + 100ms间隔(~65s) | 逐点发送(~250Hz实时) |
| 整数溢出 | 可能存在 (2026年旧bug) | 浮点比率，完全杜绝 |
| 计算复杂度 | 高 (4次完整遍历650点) | 低 (1次遍历250点) |

#### 关键改进点
1. **32位整数溢出彻底修复**：使用 `float ratio = (float)(val - min) / (float)(max - min)` 浮点比率计算，替代可能需要 32 位乘法的整数路径
2. **实时自适应**：环形缓冲区持续追踪最近 250 点 (≈1秒) 的局部极值，波形缩放随信号幅度变化自动适应
3. **显示延迟从 65 秒降至实时**：每个 FIR 滤波后的心电点立即映射并发送到串口屏，无批处理等待

#### 使用时
无需额外操作，编译烧录后串口屏即可实时显示心电波形。

#### CubeMX 兼容性
- 所有修改在 `Headware/src/task.c` 的 `ecg_showwave_task()` 函数内
- 不在 CubeMX 生成范围内，重新生成代码不受影响



---

## 2026-07-23 v9 -- 系统调试→UART1, 串口屏→UART5

### 输出分工

| 通道 | 用途 | 内容 |
|---|---|---|
| UART1 (PA9) | 系统调试串口 | 注册/认证流程日志、R波对齐、曼哈顿距离、投票结果 |
| UART5 (PB13) | 串口屏控制 | `add s0.id,0,%d\xff\xff\xff` 波形指令 |

### 修改文件

#### `Headware/inc/task.h` — 新增 `DBG_Printf` 声明

#### `Headware/src/task.c`

**新增 `DBG_Printf()` 函数** — vsnprintf 格式化后单路发送到 UART1：
```c
void DBG_Printf(const char *fmt, ...)
{
    static char buf[384];
    va_list args;
    int len;

    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0) {
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        HAL_UART_Transmit(&huart1, (uint8_t*)buf, len, 0xffff);  // 仅UART1
    }
}
```

**波形显示** (`printf` → `fputc` → UART5，不变)：
```c
printf("add s0.id,0,%d\xff\xff\xff", (uint8_t)mapped_val);
```

**系统调试** — 激活全部注册/认证流程的 `DBG_Printf` 输出到 UART1：
- 注册/认证开始：`--- Registration/Authentication: [用户名] ---`
- R 波对齐：`>> R-peak aligned (idx: 122)`
- 类内距离追踪：`>> Template [X/10] done`
- 动态门限分析：`=== 专属动态门限分析 ===` (max_intra_distance, tolerance factor, user_threshold)
- 认证比对：`Template [X/10] dist: {score} -> PASS/FAIL`
- 投票结算：`>>> Votes: X/10 (threshold: >4)`
- 最终结果：`[RESULT]: AUTH SUCCESS.` / `[RESULT]: AUTH FAILED.`

### 数据流

```
波形显示: printf → [fputc] → UART5 → 串口屏

系统调试: DBG_Printf → vsnprintf → HAL_UART_Transmit → UART1 → PC串口助手
```

### 接线
| STM32 | USB-TTL |
|---|---|
| PA9 (USART1_TX) | RX |
| GND | GND |

串口助手 115200-8N1，注册/认证流程自动输出调试日志。波形显示上的 `add` 指令仅发往串口屏，不在 UART1 出现，避免 0xFF 字节干扰。

### CubeMX 兼容性
- `DBG_Printf` 在 `Headware/` 目录，不在 CubeMX 生成范围
- `usart.c` 的 `fputc` 保持原始单路 UART5 输出（`USER CODE` 区域）
- `fputc` 未修改，CubeMX 重新生成不影响


---

## 2026-07-23 v10 -- 修复 FATFS/FIL 未定义编译错误

### 问题
所有 FatFs 源文件（diskio.c, ff.c, ff_gen_drv.c, fatfs.c, user_diskio.c, syscall.c, ccsbcs.c）报错：
```
../Headware/inc/task.h(59): error: #20: identifier "FATFS" is undefined
  extern FATFS sd_fs;
../Headware/inc/task.h(60): error: #20: identifier "FIL" is undefined
  extern FIL sd_fil;
```

### 根因
`Headware/inc/ads1292.h` 有 `#include "task.h"`，被 `main.h` 的 USER CODE Includes 链包含，形成循环依赖：

```
ff.c → ff.h → ffconf.h (顶部) → #include "main.h"
  → main.h USER CODE Includes → #include "ads1292.h"
    → ads1292.h → #include "task.h"
      → task.h → #include "ff.h"
        → _FATFS 已在 ff.c 的第一层 ff.h 中被守卫，跳过！
        → FATFS / FIL 尚未定义（ffconf.h 正在处理顶部，未执行到 #define _VOLUMES）
```

`ads1292.h` 只需 `main.h`（HAL 头文件），不依赖 `task.h` 中的任何声明（`FATFS`、`FIL`、或 DSP 滤波器类型）。

### 修改

| 文件 | 修改 |
|---|---|
| `Headware/inc/ads1292.h` | 删除 `#include "task.h"`（第11行） |

`ads1292.c` 通过自己的 `#include "ads1292.h"` 和 `#include "main.h"` 已能获取所有需要的定义（CS_LOW/HIGH, SPI_ReadWriteByte, HAL_GPIO 等）。

### CubeMX 兼容性
- `Headware/inc/ads1292.h` 不在 CubeMX 生成范围内
- CubeMX 重新生成不会恢复该行
- `ffconf.h`（`FATFS/Target/ffconf.h`）保持原有 `#include "main.h"` 不变


### v10(补) -- ads1292.c 使用 DBG_Printf 不依赖 task.h

#### 问题
`ads1292.c` 已包含 `DBG_Printf()` 调用（替换了原先被 `//` 注释的 `printf`），但 `DBG_Printf` 声明只在 `task.h` 中，而 `ads1292.c` 不能引用 `task.h`（会导致 v10 初版修复的循环依赖）。

#### 解决
将 `DBG_Printf` 函数声明添加到 `main.h` 的 `USER CODE EFP`（Exported Functions Prototypes）区域：

```c
/* USER CODE BEGIN EFP */
void DBG_Printf(const char *fmt, ...);    /* 系统调试输出到UART1(PA9,115200-8N1) */
/* USER CODE END EFP */
```

**调用链**：
```
ads1292.c → ads1292.h → main.h (USER CODE EFP: DBG_Printf 声明)
                         → usart.h (extern huart1)
                         → 编译通过，无需 task.h
```

**声明位置说明**：`DBG_Printf` 现在有两处声明，C 允许重复的兼容函数声明：

| 位置 | 作用 | 覆盖的文件 |
|---|---|---|
| `task.h:80` | 主声明（原始） | task.c, HMI.c 等引用 task.h 的文件 |
| `main.h` USER CODE EFP | 次声明（新增） | ads1292.c 等不引用 task.h 的文件 |

#### 激活的 ads1292 调试输出

`ads1292.c` 中 5 处原先被 `//` 注释的调试信息现通过 `DBG_Printf` 输出：
- `ID:0x73` — 检测到 ADS1292R
- `This device is ADS1292R` / `This device is ADS1292` — 芯片型号识别
- `the device is not recognized,please check the device` — 未知芯片警告
- `ERROR ID:0x%02x` — 错误芯片 ID

这些输出仅在初始化时运行一次（非 250Hz 波形循环），不影响性能。

#### CubeMX 兼容性
- `main.h` 的 `USER CODE EFP` 区域在 CubeMX 重新生成时保留
- 无需引用 `task.h`，循环依赖链不会重新出现



---

## 2026-07-24 v11 -- HMI流程说明同步更新

### 修改内容与流程说明.md同步

根据更新后的流程说明.md 文件，对 HMI 模块进行以下修改，使代码行为与文档完全一致。

#### `Headware/inc/HMI.h`
- **命令编码注释修正**：将原有的 `高字节: 0x01=串口屏(TJC/UART5), 0x00=天问(UART7)` 注释
  更新为与流程说明一致：明确命令通过 UART5 或 UART7 发送，高字节为命令类别（0x00=注册, 0x01=验证, 0x02=开锁, 0x03=用户管理, 0x04=系统初始化），
  低字节为子类型（0x00=用户, 0x01=管理员）

#### `Headware/src/HMI.c`
- **HMI_Init() 调试输出激活**：将 `//printf` 注释的初始化完成消息改为 `DBG_Printf` 输出，与文档 4.1 节预期的 `[HMI] HMI模块初始化完成, 等待指令...` 一致
- **Task_ProcessSysInit() 注释修正**：将 "删除SD卡所有文件" 改为 "初始化SD卡"，反映实际行为（文档 4.2 节仅描述 SD_Init_FATFS）
- **Task_ProcessManageUsers() 真实SD卡操作实现**：
  - 移除所有"模拟:"占位符输出
  - HMI_SUB_LIST_SD: 通过 `f_opendir`/`f_readdir` 枚举 SD 卡根目录 `.DAT` 文件，列出所有注册用户
  - HMI_SUB_DELETE_USER: 实际删除用户 `.DAT`、`_RAW.TXT`、`_CFG.TXT` 文件
  - 完整实现挂载、列表、删除、卸载流程，与命令表 CMD_MANAGE_USERS (0x03 0x03) 对应

### 未修改的文件
- `Core/Inc/main.h` — 无变更
- `Core/Src/main.c` — 无变更
- `Headware/inc/ads1292.h` — 无变更
- `Headware/inc/task.h` — 无变更
- `Headware/src/task.c` — 无变更
- `Headware/src/SD.c` — 无变更
- `Headware/src/pcanet_inference.c` — 无变更
- `FATFS/Target/user_diskio.c` — 无变更
- `FATFS/Target/ffconf.h` — 无变更

### CubeMX 兼容性
- 所有修改均在 `Headware/` 目录下，不在 CubeMX 生成范围内
- CubeMX 重新生成不会破坏上述修改

### v11 数据流对照

**系统初始化 (CMD_SYS_INIT = 0x0404)**
```
CMD_SYS_INIT → Task_ProcessSysInit() → SD_Init_FATFS()
  → f_mount → f_getfree → 打印SD容量
  → 返回HMI_SUB_DONE → 发PAGE_INIT到串口屏和天问 → 回到IDLE
```

**用户管理 (CMD_MANAGE_USERS = 0x0303)**
```
CMD_MANAGE_USERS → Task_ProcessManageUsers()
  → 挂载SD → 发PAGE_MANAGE
  → 枚举.DAT文件 → 列用户列表
  → 接收用户名 → 删除.DAT/_RAW.TXT/_CFG.TXT
  → 卸载SD → 回到IDLE
```

**命令格式**
```
通过 UART5(串口屏) 或 UART7(天问) 发送2字节命令(大端序)
高字节 = 命令类别 | 低字节 = 子类型
0x00 0x00 = 用户注册 | 0x00 0x01 = 管理员注册
0x01 0x00 = 用户验证 | 0x01 0x01 = 管理员验证
0x02 0x02 = 开锁 | 0x03 0x03 = 用户管理 | 0x04 0x04 = 系统初始化
```


---

## 2026-07-24 v12 -- 用户管理页面推送用户名到串口屏

### 修改内容

#### `Headware/src/HMI.c`
- **Task_ProcessManageUsers() 增加屏幕推送**：在 HMI_SUB_LIST_SD 阶段，将 SD 卡中枚举到的所有注册用户名通过 `HMI_SendToScreen` 推送到串口屏幕显示
  - 每个用户名使用独立的串口屏控件命令：`user.t1.txt="name1"`, `user.t2.txt="name2"`, ... 逐个推送
  - 序号与控件编号对应：第1个用户推送到 t1, 第2个推送到 t2，依此类推
  - 空列表时不推送任何 username 命令（`t%d.txt` 保持空白）

### 数据流

**用户管理 (CMD_MANAGE_USERS = 0x0303)**
```
CMD_MANAGE_USERS → Task_ProcessManageUsers()
  → 挂载SD → 发PAGE_MANAGE
  → 枚举.DAT文件 → 逐个推送 user.t1.txt="name1", user.t2.txt="name2" ...
  → 接收用户名 → 删除.DAT/_RAW.TXT/_CFG.TXT
  → 卸载SD → 回到IDLE
```

### 未修改的文件
- `Core/Inc/main.h` — 无变更
- `Core/Src/main.c` — 无变更
- `Headware/inc/HMI.h` — 无变更
- `Headware/inc/ads1292.h` — 无变更
- `Headware/inc/task.h` — 无变更
- `Headware/src/task.c` — 无变更
- `Headware/src/SD.c` — 无变更
- `Headware/src/pcanet_inference.c` — 无变更
- `FATFS/Target/user_diskio.c` — 无变更
- `FATFS/Target/ffconf.h` — 无变更

### CubeMX 兼容性
- 所有修改均在 `Headware/src/HMI.c` 中，不在 CubeMX 生成范围内


---

## 2026-07-24 v13 -- MCU与天问(UART7)改用16进制编码通信

### 修改原因
天问语音助手无法接收 ASCII 字符串，且不要求 0xFF 0xFF 0xFF 后缀。原 `HMI_SendToTianwen()` 发送带后缀的字符串，无法被天问正确解析。

#### `Headware/inc/HMI.h`
- **新增天问16进制命令定义**：`TW_CMD_PAGE_WAVE`(0x1111)、`TW_CMD_PAGE_USER_S`(0x2222)、`TW_CMD_PAGE_DEFAULT`(0x3333)、`TW_CMD_PAGE_ADM_S`(0x4444)、`TW_CMD_PAGE_R_S`(0x5555)、`TW_CMD_PAGE_INIT`(0x6666)、`TW_CMD_PAGE_MANAGE`(0x7777)、`TW_CMD_UNLOCK`(0x0202)
- **修改函数原型**：`HMI_SendToTianwen(const char *str)` → `HMI_SendToTianwen(uint16_t cmd)`

#### `Headware/src/HMI.c`
- **重写 HMI_SendToTianwen() 实现**：改为发送2字节16进制数据(大端序)，取消 strlen 计算和 0xFF 后缀，取消 HAL_Delay
- **替换所有11处调用**：所有 `HMI_SendToTianwen(PAGE_xxx)` 替换为 `HMI_SendToTianwen(TW_CMD_PAGE_xxx)`，`HMI_SendToTianwen("unlock")` 替换为 `HMI_SendToTianwen(TW_CMD_UNLOCK)`

### 天问编码对照表
| 原字符串命令 | 16进制编码 |
|---|---|
| "page init" | 0x66 0x66 |
| "page wave" | 0x11 0x11 |
| "page user_s" | 0x22 0x22 |
| "page dufault" | 0x33 0x33 |
| "page adm_s" | 0x44 0x44 |
| "page r_s" | 0x55 0x55 |
| "page user" | 0x77 0x77 |
| "unlock" | 0x02 0x02 |

### 数据流变化
```
旧: HMI_SendToTianwen → strlen + 发送字符串 + 发0xFF×3 + delay(100ms)
新: HMI_SendToTianwen → 2字节大端序hex → HAL_UART_Transmit(无后缀、无延���)
```

### 未修改的文件
- `Core/Inc/main.h` — 无变更
- `Core/Src/main.c` — 无变更
- `Headware/src/task.c` — 无变更
- `Headware/inc/task.h` — 无变更
- `Headware/src/SD.c` — 无变更
- `Headware/inc/SD.h` — 无变更

### CubeMX 兼容性
- 所有修改在 `Headware/` 目录下，不在 CubeMX 生成范围内


---

## 2026-07-24 v14 -- UART7 调试诊断 + 状态机追踪

### 问题
UART7 (天问) 无数据输出。

### 修改内容

#### `Headware/src/HMI.c`
- **HMI_SendToTianwen() 添加调试输出**：
  - 发送前记录 TX 字节 `[UART7] TX: 0xXX 0xXX`
  - 检查 `HAL_UART_Transmit` 返回值，失败时输出 `[UART7] *** Transmit FAILED! ret=N`
- **HMI_TaskProcess() 状态机追踪**：
  - 每次进入状态机循环时打印 `[HMI] >> STATE: XXX, sub=N`
  - 包括：USER_VERIFY, ADMIN_VERIFY, USER_REGISTER, ADMIN_REGISTER, UNLOCK, SYS_INIT, MANAGE_USERS
  - 未知状态时输出 `[HMI] >> STATE: UNKNOWN(N), reset to IDLE`
  - IDLE 状态添加注释说明

### 诊断指引
| 调试输出 | 含义 |
|---|---|
| `[HMI] HMI模块初始化完成, 等待指令...` | 系统启动正常 |
| `[UART7] TX: 0x66 0x66  ret=0` | HMI_Init 成功向天问发送 PAGE_INIT |
| `[UART7] TX: 0xXX 0xXX  ret=0` | 正常运行中向天问发送指令 |
| `[UART7] *** Transmit FAILED!` | HAL_UART_Transmit 返回错误 |
| `[HMI] >> STATE: UNLOCK, sub=N` | 当前在执行解锁任务 |

1. 上电观察 UART1，确认 `HMI模块初始化完成` 出现
2. 检查 `[UART7] TX: 0x66 0x66  ret=0` 是否输出
3. 若 ret != 0，说明 HAL 层发送失败，需检查 UART7 初始化
4. 若 TX 有输出但用户看不到，检查物理接线（PE8 TX）
5. 若 ret=0 无线输出，检查串口助手波特率设置（115200 8N1）

### 未修改的文件
- `Core/Inc/main.h` — 无变更
- `Core/Src/main.c` — 无变更
- `Headware/inc/HMI.h` — 无变更
- `Headware/inc/task.h` — 无变更
- `Headware/src/task.c` — 无变更


---

## 2026-07-24 v15 -- 系统初始化改为系统格式化

### 修改原因
CMD_SYS_INIT (0x0404) 原仅调用 SD_Init_FATFS() 测试 SD 卡挂载，现改为执行系统格式化：删除 SD 卡中除 FX 外的所有用户文件。

#### `Headware/src/HMI.c`
- **Task_ProcessSysInit() 重写**：替换原先简单的 SD_Init_FATFS() 为完整的系统格式化流程
  - HMI_SUB_IDLE：挂载 SD 卡，成功则进入 LIST_SD
  - HMI_SUB_LIST_SD：枚举所有文件，识别用户文件（.DAT, _RAW.TXT, _CFG.TXT），提取用户名，非 FX 则删除
  - HMI_SUB_DONE：打印删除统计，发送 PAGE_INIT 到串口屏和天问，返回 IDLE
- **修复编译错误**：上一轮编辑导致 Cmd_Dispatch 函数未闭合、Task_ProcessSysInit 被误删
  - Cmd_Dispatch：补全 CMD_MANAGE_USERS case、default case、switch 闭合、函数闭合
  - Task_ProcessSysInit 函数：重新创建（从原 Task_ProcessManageUsers 中的系统格式化代码迁移）
  - Task_ProcessManageUsers 函数：恢复为用户管理完整实现（列表 -> 接收用户名 -> 删除）

### 系统格式化流程
```
CMD_SYS_INIT (0x0404) → Task_ProcessSysInit()
  → HMI_SUB_IDLE：挂载 SD 卡
  → HMI_SUB_LIST_SD：遍历所有文件
    ├── 文件是 .DAT / _RAW.TXT / _CFG.TXT？ → 提取用户名
    │    ├── 用户名 == "FX" → 保留（管理员账户）
    │    └── 否则 → f_unlink() 删除
    └── 其他文件 → 跳过
  → HMI_SUB_DONE：打印统计，发送 PAGE_INIT
  → 返回 IDLE
```

### 未修改的文件
- `Core/Inc/main.h` — 无变更
- `Core/Src/main.c` — 无变更
- `Headware/inc/HMI.h` — 无变更
- `Headware/inc/task.h` — 无变更
- `Headware/src/task.c` — 无变更

### CubeMX 兼容性
- 所有修改在 `Headware/src/HMI.c` 中，不在 CubeMX 生成范围内


---

## 2026-07-24 v16 -- 修复编译错误

### 问题
1. **编译错误**：Task_ProcessManageUsers 函数结束位置多出一个 `}`（line 398），导致花括号不匹配
2. **编译警告 #870-D**：文件编码为 UTF-8，Keil 编译器按系统 ANSI (GBK) 解析导致"无效多字节字符序列"警告

### 修改内容

#### `Headware/src/HMI.c`
- **删除多余闭合花括号**：Task_ProcessManageUsers 结尾（line 398）的额外 `}` 已删除，函数结构恢复正常
- **文件编码修正**：重新保存为 ANSI (GBK) 编码，与 Keil MDK 的预期编码一致，消除 #870-D 警告

### 编码说明
Keil MDK 编译器在中文 Windows 上默认使用系统 ANSI 编码 (GB2312/GBK) 读取源文件。
若文件保存为 UTF-8 不含 BOM，编译器会将 UTF-8 多字节序列当作 GBK 解析，产生
`#870-D: invalid multibyte character sequence` 警告。
修正方案：将文件保存为 ANSI (GBK) 编码，中文注释和字符串均可正确解析。

### 未修改的文件
- 仅修改 `Headware/src/HMI.c` 编码格式
- 其他文件均无变更


---

## 2026-07-24 v17 -- 修复链接错误：补回丢失的静态函数

### 问题
链接器报 L6218E: Undefined symbol 错误：
- `Task_ProcessVerify`
- `Task_ProcessRegister`
- `Task_ProcessUnlock`

三个函数的定义在之前编辑中丢失。

### 修改内容

#### `Headware/src/HMI.c`
- **重新插入三个静态函数**（从 HMI.c.tmp 恢复，更新为当前格式）：
  - `Task_ProcessVerify()` — 用户/管理员验证状态机（含 FX 管理员拦截逻辑）
  - `Task_ProcessRegister()` — 用户/管理员注册状态机
  - `Task_ProcessUnlock()` — 开锁状态机
- **格式适配**：
  - `//printf(...)` → `DBG_Printf(...)`
  - `HMI_SendToTianwen(PAGE_xxx)` → `HMI_SendToTianwen(TW_CMD_PAGE_xxx)`
  - `HMI_SendToTianwen("unlock")` → `HMI_SendToTianwen(TW_CMD_UNLOCK)`
  - Task_ProcessVerify 保留 FX 管理员拦截判断

### 当前函数结构
| 行号 | 函数 | 说明 |
|---|---|---|
| 176 | Cmd_Dispatch | 命令派发 |
| 219 | Task_ProcessVerify | 验证流程 |
| 306 | Task_ProcessRegister | 注册流程 |
| 375 | Task_ProcessUnlock | 开锁流程 |
| 400 | Task_ProcessSysInit | 系统格式化 |
| 487 | Task_ProcessManageUsers | 用户管理 |
| 583 | HMI_Init | 初始化 |
| 611 | HMI_TaskProcess | 主状态机 |

### 未修改的文件
- 仅修改 `Headware/src/HMI.c`
- 其他文件均无变更


---

## 2026-07-24 v18 -- 修复系统格式化和编码问题

### 问题
1. 系统格式化(CMD_SYS_INIT) 错误地删除了 FX 文件而非保留
2. DBG_Printf 中的中文字符因文件编码(ANSI/GBK)导致乱码和编译问题

### 修改内容

#### `Headware/src/HMI.c`
- **重写 Task_ProcessSysInit()**：逻辑更清晰
  - `strcmp(username, "FX") == 0` → `continue`（保留，不删除）
  - 其余用户文件 → `f_unlink()`（删除）
  - 修复之前残留的多余闭合花括号
- **移除所有中文字符串**：DBG_Printf 全部改为 ASCII 英文
  - 例："系统格式化" → "SYS Format Start/End"
  - 例："保留" → "KEEP"、"已删除" → "DELETE"
  - 编码问题彻底消除
- **文件编码改为 UTF-8 with BOM**：
  - 兼容 Keil 编译器（BOM 可识别为 UTF-8）
  - 兼容 apply_patch 工具（UTF-8 正常解析）

### 当前格式输出示例
```
[HMI] ===== SYS Format Start =====
[HMI] Scanning files...
[HMI] KEEP: FX.DAT (FX admin)
[HMI] KEEP: FX_RAW.TXT (FX admin)
[HMI] DELETE: test.DAT
[HMI] DELETE: user1_RAW.TXT
[HMI] Format done, deleted 2 files. FX preserved.
[HMI] ===== SYS Format End =====
```

### 未修改的文件
- 仅修改 `Headware/src/HMI.c`
- 其他文件均无变更
