# 硬件参数

## 主控板

![img.png](images/sg2002.png)

| 参数 | 值                                                                                |
|------|----------------------------------------------------------------------------------|
| 型号 | [LicheeRV Nano](https://wiki.sipeed.com/hardware/zh/lichee/RV_Nano/1_intro.html) |
| CPU | 算能 SG2002  <br/>大核：1GHz RISC-V C906 / ARM A53 二选一;<br/>小核：700MHz RISC-V C906；       |
| NPU | 1 TOPS (INT8)，支持 BF16                                                               |

## 机械臂舵机和控制板

### 控制板
微雪UART串口通信控制板

![主控图](images/uart.png)

### 使用的舵机

| 参数 | 值 |
|------|-----|
| 型号 | ZL-ZP10S |
| 通信 | 串口 UART |
| 设备 | `/dev/ttyS2`（主控 UART2） |
| 波特率 | 115200 |

### 支持的舵机

当前 C++ 实现里编了驱动的两种（`config.toml` 的 `[arm] backend` 选）：

- **ZL-ZP10S**（`backend = "zp10s"`，默认）
- **STS3215**（`backend = "sts3215"`）

## 底盘控制板（ESP32-C3）

![drv8833-2.png](images/drv8833-2.png)

### 使用的电机

| 参数 | 值 |
|------|-----|
| 型号 | TT 马达 ×2（带编码器） |
| 控制方式 | ESP32-C3 底盘板闭环 PID，主控经 UART 下发/回读 |
| 通信 | `/dev/ttyS1`（主控 UART1），115200 |
| 编码器 | 4680 脉冲/轮圈 |
| 固件 | `esp32_base_control/base_control.ino`（独立工程，不在本仓库） |

> **主控不再直连电机 PWM/GPIO** —— 旧文档里那张「PWM Chip 4 / Channel 0,1,2,3」的
> 接线表已经不适用，那套是 Python 版用 GPIO 直驱的做法。
> 角度/距离闭环（`CMD_MOVE_DISTANCE`）和限速都在 ESP32 固件里做，主机只发目标值、
> 读回 rpm 与状态。

## 板载屏

| 参数 | 值 |
|------|-----|
| 型号 | ST7796S |
| 分辨率 | 320x480（RGB565） |
| 接口 | SPI，**只写**（无 MISO / 无触摸引出），内核暴露为 `/dev/fb0` |
| 接线 | 见[硬件接线](./wiring.md)的「板载屏（SPI）」 |
| 配置 | `config.toml` 的 `[display]`（开关 / 全屏半屏 / 方向 / 帧率） |

只有**带屏版**会驱动它（`make` 默认，`AKA_WITH_SCREEN=1`）；不带屏版（`make noscreen`）
把整个显示栈编译期裁掉，二进制更小且完全不碰 `/dev/fb0`，`[display]` 配置被忽略。