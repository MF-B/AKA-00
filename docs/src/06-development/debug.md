# 调试方法

## 串口连接

```bash
# 安装 minicom
sudo apt install minicom

# 连接串口
minicom -D /dev/ttyUSB0 -b 115200
```

## 网络连接

### USB RNDIS 网口

```bash
# 查看网口
ip a show

# 如果主机是 10.245.118.100，则开发板是 10.245.118.1
ssh root@10.245.118.1
```

### WiFi SSH

```bash
ssh root@<机器人IP>
```

## 日志查看

capp **不写日志文件**，输出直接走 stdout：开机由 `/etc/init.d/S99webstart` 拉起时打在
**串口控制台**上；手工启动想留一份就自己重定向：

```bash
AKA_HOME=$HOME/AKA-00 $HOME/AKA-00/init.sh > /root/aka.log 2>&1 &
tail -f /root/aka.log
```

日志详细程度由 `config.toml` 的 `[logging] level` 控制（默认 `info`）。

## 测试硬件

板子自带一套板测工具（部署目录的 `tools/`，`make` 打包时一并放进去）：

```bash
./tools/tt_pid_test /dev/ttyS1 40 3000   # 底盘：直发速度看车动不动（绕过 Web 层）
./tools/cam_probe                        # 摄像头：验证能否出帧
./tools/screen_test                      # 板载屏：写屏带宽/颜色（仅带屏版）
```

> 这些工具会**独占**对应设备，跑之前先停 capp（`$AKA_HOME/stop.sh`）。

## 网络诊断

```bash
# 检测网络连通性
ping www.baidu.com

# 检测外网访问
curl www.baidu.com
```

## HTTPS 证书

**不用手工生成**：`init.sh` 启动前会调 `https_init.sh`，缺 `cert.pem`/`key.pem` 时自动
签一张（EC prime256v1，10 年），已经有的不会被覆盖。要换成 CA 签发的证书，把两个文件
放到 `$AKA_HOME/` 即可；端口在 `config.toml` 的 `[web] https_port`（默认 443，
设 0 关闭 HTTPS）。手工重签的命令见[初始化配置](../04-setup/connection.md)。
