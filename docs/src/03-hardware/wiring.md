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

### 接口一览

| 用途 | 主控引脚 | 设备节点 | 配置 |
|------|----------|----------|------|
| 底盘（ESP32-C3） | A18 / A19（UART1） | `/dev/ttyS1` | `[motor]` |
| 机械臂舵机 | A28 / A29（UART2） | `/dev/ttyS2` | `[arm]` |

> 引脚复用（哪些脚要做 UART）见[开发板说明](../06-development/dev-board.md)。

## 更多原理图

硬件原理图位于 `hardware/` 目录：

- `LicheeRV_Nano-70418_Schematic.pdf` - 主控板原理图
- `sg2000_trm_cn.pdf` - SG2000 技术参考手册
- `众灵舵机使用手册-250508.pdf` - 舵机使用说明
