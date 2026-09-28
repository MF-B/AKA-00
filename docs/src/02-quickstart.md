# 快速开始

本文档帮助你快速开始让 AKA-00 跑起来。

## 1. 组装

参考 [硬件接线](./03-hardware/wiring.md) 完成机械臂、电机、摄像头的连接。

## 2. 通电

1. 连接电源，等待控制板指示灯亮起
2. 等待 60 秒，网络模块启动
3. 连接机器人热点（格式：`chenlong-robot-xxxxx`）
4. 浏览器访问 `192.168.4.1`，进入控制界面
5. 之后可以通过手机上的遥控器控制小车

## 3. 部署（首次/更新）

项目以单文件 `aka-00-server` 分发，拷贝到控制板执行：

```bash
# 打包（在开发机上；改了前端要先 cd frontend && npm run build）
make -C cpp ota                 # → cpp/dist/aka-00-server

# 拷贝到控制板（-O：板载 sshd 不认新版 SFTP 协议）
scp -O cpp/dist/aka-00-server root@<robot>:/root/

# 首次部署：解包 + 热点 + 开机自启
ssh root@<robot> '/root/aka-00-server --init'
```

更新部署（**保留** `config.toml`、证书、demo 卡片等现场数据）：

```bash
scp -O cpp/dist/aka-00-server root@<robot>:/root/
ssh root@<robot> 'setsid /root/aka-00-server --update >/root/ota.log 2>&1 < /dev/null'
```

> - **别放 `/tmp`**：板上 `/tmp` 是 53MB 的内存盘，20MB 的固件放那儿会把内存打爆。
> - 想让本次带的默认配置连 `config.toml` 一起覆盖，用 `AKA_OTA_RESET_CONFIG=1 ... --update`。
> - `setsid` 是因为 `--update` 结尾是 `exec init.sh`，会把服务挂在当前 ssh 会话上；
>   也可以升级完直接 `reboot`（开机由 `/etc/init.d/S99webstart` 拉起）。


## 4. 使用

启动后通过以下方式控制：

- **Web 界面**: 访问 `http://<机器人IP>/`
- **API**: 使用 `/api/control` 接口

详细说明见 [Web 界面](./05-usage/web-ui.md) 和 [API 文档](./05-usage/api.md)

## 下一步

- [配置 WiFi](./04-setup/wifi-config.md)
- [了解代码结构](./06-development/structure.md)
- [常见问题](./07-faq.md)
