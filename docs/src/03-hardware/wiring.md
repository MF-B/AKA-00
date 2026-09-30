# 硬件接线

## 接线示意

### 主控接口图
![img.png](images/lichee_rv.png)

本项目使用了以下接口：

- **底盘控制（UART1）**
    - A18：UART1 RX
    - A19：UART1 TX
- **机械臂舵机（UART2）**
    - A28：UART2 TX
    - A29：UART2 RX
- **板载屏（SPI）**
    - SCK：SPI 时钟
    - SDO：SPI 数据输出（接屏的 SDI）
    - CS：SPI 片选
    - IO5：LCD_RS（数据/命令选择）
    - IO6：LCD_RST（复位）
- VBUS 5V
- GND

### 底盘控制板接口图
![img.png](images/drv8833-2.png)
- VM：电机供电
- NC：置空
- GND：接地
- A、BO1、2：接电机
- A、BIN1、2：控制信号输入
- STBY：SLEEP控制，底电平有效

### 机械臂控制板接口图
![img.png](images/uart_contect.png)
- D：数据总线
- V：舵机供电正级
- G：舵机接地
- DC+：主控供电正级
- DC-：主控供电负极
- TX：控制输入
- RX：控制接收
- GND：接地
- A UART：UART总线控制模式
- B USB：USB总线控制模式

### 控制电路连线图
![img.png](images/hardware_connect.png)
接线前请确保断电操作。

### 机械臂 UART

- 机械臂舵机控制板接主控 **UART2**（A28 / A29），设备节点 `/dev/ttyS2`
- 波特率：115200

### 底盘 UART

- 底盘控制板（ESP32-C3）接主控 **UART1**（A18 / A19），设备节点 `/dev/ttyS1`
- 波特率：115200
- **主控不再直连电机 PWM**：A16~A19 原本那四路 PWM 现在不用了（A18/A19 复用成 UART1），
  电机驱动与编码器都归 ESP32 底盘板管

### 板载屏（SPI）

板载屏（ST7796S，320x480）走 SPI，接法如下（左＝屏的排针，右＝主控）：

| 屏（ST7796S） | 主控 | 说明 |
|---------------|------|------|
| VCC | VDD | 供电 |
| GND | GND | 地 |
| LCD_CS | CS | SPI 片选 |
| LCD_RST | IO6 | 复位 |
| LCD_RS | IO5 | 数据/命令选择（DC） |
| SDI | SDO | SPI 数据：屏的**输入**接主控的**输出**（MOSI） |
| SCK | SCK | SPI 时钟 |

- 屏是**只写**设备（没有 MISO / 触摸没有引出），capp 不直接操作 SPI 引脚，只往
  内核暴露出来的 `/dev/fb0` 写画面。
- SPI 时钟在设备树里（节点 `st7796s@0` 的 `spi-max-frequency`）：出厂 4MHz 只有
  ~420KB/s（约 2fps），本板实测**稳定上限 20MHz**（~2MB/s），**24MHz 起白屏**。
  杜邦线转接会明显拉低能跑的频率。
- 屏的参数（开关、全屏/半屏、方向、帧率）都在 `config.toml` 的 `[display]` 里配，
  没有 HTTP 接口；调参依据见 `cpp/README.md` 的「板载屏显示」一节。
- 排查接线用 `./tools/screen_test`（`colors` 看通道序/方向、`bench` 看写屏带宽）。

### 接口一览

| 用途 | 主控引脚 | 设备节点 | 配置 |
|------|----------|----------|------|
| 底盘（ESP32-C3） | A18 / A19（UART1） | `/dev/ttyS1` | `[motor]` |
| 机械臂舵机 | A28 / A29（UART2） | `/dev/ttyS2` | `[arm]` |
| 板载屏 | SCK / SDO / CS / IO5 / IO6 | `/dev/fb0` | `[display]` |

> 引脚复用（哪些脚要做 UART）见[开发板说明](../06-development/dev-board.md)。

## 更多原理图

硬件原理图位于 `hardware/` 目录：

- `LicheeRV_Nano-70418_Schematic.pdf` - 主控板原理图
- `sg2000_trm_cn.pdf` - SG2000 技术参考手册
- `众灵舵机使用手册-250508.pdf` - 舵机使用说明
