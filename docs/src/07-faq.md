# 常见问题

## 连接问题

### Q: 机器人热点无法连接？

1. 确保机器人已通电且指示灯亮起
2. 等待 60 秒让网络模块完全启动
3. 确认电脑/手机 WiFi 已开启

### Q: 无法 SSH 登录？

1. 确认电脑和机器人在同一 WiFi 网络
2. 检查 IP 地址是否正确
3. 尝试使用串口连接调试

### Q: WiFi 配置页面打不开？

浏览器访问 `192.168.4.1`，确认已连接机器人热点。

---

## 运行问题

### Q: 启动失败，提示缺少依赖？

**板上没有"依赖"可缺** —— 设备端是静态二进制，不依赖 Python 或任何库。启动失败看
`init.sh` 的输出（串口控制台），常见就两种：

- `aka-capp not found or not executable` —— 包没解全或可执行位丢了，重跑一次
  `./aka-00-server --update`；
- 端口被占（日志里 `bind failed`）—— 多半起了两个实例，`ps` 看一眼 `init.sh` 与 `aka-capp`。

如果是**开发机上编译**失败，那才是环境问题（缺 riscv64-musl 工具链）：
见[开发环境与部署](../04-setup/software.md)。

### Q: 机械臂不响应？

1. 确认舵机控制板接在主控 **UART2**（`/dev/ttyS2`，115200）
2. 确认舵机供电正常 —— 舵机是独立供电，别只靠主控
3. `ls -l /dev/ttyS2` 看设备在不在，`config.toml` 的 `[arm] port` / `backend` 是否对得上
4. 直接读一下角度：`curl http://<ip>/api/arm/angles`

### Q: 电机不转动？

底盘是 **ESP32-C3 底盘板**闭环控制的，主控并不直连电机 PWM，所以按链路查：

1. 底盘板供电、串口接线（主控 UART1 `/dev/ttyS1`，115200）
2. 看 `GET /api/motor/status` 的 `motor.connected` / `motor.state`：是 `reconnecting`
   就是串口没通 —— 服务会按退避自动重连，不用重启
3. 用 `./tools/tt_pid_test /dev/ttyS1 40 3000` 绕过 Web 层直测（先停 capp）
4. 配置为空也会导致行为不对：`config.toml` 丢了/0 字节时所有配置静默取默认值，
   接口看着正常但车不动 —— `init.sh` 启动时会对空配置告警，日志里搜 `config.toml`

### Q: 摄像头无法识别？

1. 检查 USB 连接
2. 确认设备文件 `/dev/video0` 存在
3. 测试摄像头：`ls -l /dev/video0`

---

## 其他

### Q: 如何查看机器人 IP？

`GET /api/system/ip`（热点模式下网关固定 `192.168.4.1`，接路由器后的 IP 也可以看
`GET /api/wifi/status`）。

### Q: 如何开启 HTTPS？

**不用手工做**：`cpp/board/https_init.sh` 会在 capp 启动前自动生成自签证书
（EC prime256v1，10 年有效期；openssl 没编 EC 时退回 RSA-2048），缺一才生成 ——
已有的证书不会被覆盖。生成位置固定 `$AKA_HOME/cert.pem` / `key.pem`。

要手工换证书（比如换成 CA 签发的），把这两个文件放到 `$AKA_HOME/` 即可，
端口在 `config.toml` 的 `[web] https_port`（默认 443）。

### Q: 如何设置开机自启？

参考 [机器人连接](./04-setup/connection.md) 中的开机自启配置。
