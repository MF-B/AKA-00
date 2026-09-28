# WebSocket 控制接口

底盘遥控这类高频操作走 WebSocket，比 HTTP 轮询延迟低得多。控制页的虚拟摇杆用的就是
这条通道。

## 连接

```
ws://<机器人IP>/ws/control          # HTTPS 页面上用 wss://
```

## 协议格式

双方都发**二进制帧**，第一个字节是类型标记。

### 客户端 → 服务端

| 帧 | 说明 |
|---|---|
| `0xAA` + int8 `x` + int8 `y` | 摇杆，3 字节；x 转向、y 油门，各 -128~127 |
| `0xDD` + UTF-8 JSON | 低频命令（见下），文本帧发 JSON 也接受 |

摇杆换算：`fy = y/127`、`fx = x/127`，`左 = (fy+fx)×100`、`右 = (fy-fx)×100`，限幅
到 ±100 后下发。**不带时长、不阻塞**，所以松手要自己补一帧停止。

| 操作 | 字节 | 说明 |
|------|------|------|
| 前进 50 | `AA 00 32` | X=0, Y=50 |
| 后退 30 | `AA 00 E2` | X=0, Y=-30（补码） |
| 左转 25 | `AA E7 00` | X=-25, Y=0 |
| 右转 25 | `AA 19 00` | X=25, Y=0 |
| 停止 | `AA 00 00` | X=0, Y=0 |

JSON 命令（`0xDD` + JSON）：

| type | 说明 |
|---|---|
| `{"type":"ip"}` | 查 IP，回 `{"type":"ip","ip":"…"}` |
| `{"type":"action","action":"stop","speed":50,"time":0}` | 走和 HTTP `/api/control` 同一个动作入口（不阻塞，发完就走） |
| `{"type":"raw_command","cmd":"…"}` | 透传固件原始指令（排障用） |
| `{"type":"arm_cmd","payload":{"command":"grab"}}` | 夹爪开/合（只认 `grab` / `release`），不回复 |
| `{"type":"reinitialize"}` | 重连底盘（清 PID/编码器，不掉线、不打断运行） |

其它 `type` 会被忽略（只记 debug 日志）。

### 服务端 → 客户端

| 帧 | 说明 |
|---|---|
| `0xBB` + int16LE 左轮 + int16LE 右轮 | 轮速，单位 m/s × 1000；**每 200ms 推一次** |
| `0xDD` + UTF-8 JSON | 状态与应答。连上即推 `{"type":"ip","ip":"…"}`，以及 `{"type":"motor_status","motor":{…}}`（此后仅在底盘连接状态变化时推） |

## 前端示例

```typescript
const ws = new WebSocket("ws://192.168.4.1/ws/control");
ws.binaryType = "arraybuffer";

ws.send(new Uint8Array([0xAA, 0, 50]));   // 前进
ws.send(new Uint8Array([0xAA, 0, 0]));    // 停

ws.onmessage = (event) => {
    const b = new DataView(event.data);
    if (b.getUint8(0) === 0xBB) {
        const left = b.getInt16(1, true) / 1000;
        const right = b.getInt16(3, true) / 1000;
        console.log(`左: ${left} m/s, 右: ${right} m/s`);
    }
};
```

## 生命周期与断线

- 服务端每 **4 秒** ping 一次；**90 秒**收不到任何字节才判死回收连接 —— 阈值给得宽是
  因为手机 WiFi 省电模式会让上行停摆几十秒，秒级判死只会造成反复断开重连。
- ⚠️ **断开连接不会停车**（这点和 Python 版相反）：判死只用于回收僵尸连接，不再承担
  安全停车职责。停车靠指令本身（摇杆发 0 或 `action stop`），以及 ESP32 固件侧的
  心跳看门狗。
- 同一时刻只保持**一个**控制连接：多个连接同时发会互相抢（谁后发谁生效）。
