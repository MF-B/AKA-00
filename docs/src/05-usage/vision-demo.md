# 检测 · 识别 · Demo 运行 API

给调用方（平台、脚本、别人写的程序）的一份**独立**文档：从"看一眼画面"到"让小车自己追过去
把东西夹起来"这一整条链的 HTTP 接口。**坐标单位一律是原图像素**。
C++ + JSON 版本已通过开发机测试和 RISC-V 构建，目标系统仍需实机验证。

> 想一次看全部接口（电机/机械臂/OTA/WiFi/系统）见 [API 文档](./api.md)；这篇只讲
> **摄像头 → 推理 → 跑动作**这条链，覆盖 `/api/camera/*`、`/api/detect`、`/api/models/*`
> 与 `/api/demo/*`。

## 一分钟上手

```bash
IP=<板子IP>

# ① 开摄像头（检测的前提；不开会回 "camera not available"）
curl -X POST http://$IP/api/camera/open

# ② 看一眼识别结果（框的四个角，原图像素）
curl "http://$IP/api/detect?model=tennis"
# → {"ok":true,"count":1,"boxes":[{"x1":2,"x2":172,"y1":0,"y2":360}]}

# ③ 跑一次 demo：用 tennis 模型做"接近瞄准"，框宽到 320px 且对准夹爪时到位
curl -X POST http://$IP/api/demo/init -H 'Content-Type: application/json' \
     -d '{"action":"approach","model":"tennis","target_size":320,"speed":30,"turn_speed":25}'
# → {"completed":true}          ← 默认等它跑完再返回（下面的"wait"一节）
```

③ 里**没有建任何卡片**：`action + model + 参数`凑齐就是一次完整请求。车会真的动。

---

## 1. 摄像头（检测的前提）

| 接口 | 说明 |
|------|------|
| `GET /api/camera/status` | `{"camera_on": true/false}` |
| `POST /api/camera/open` | 打开采集。成功 200 `{"camera_on":true}`；打不开则 **500 + `{"camera_on":false}`**（原因见板子日志） |
| `POST /api/camera/close` | 关闭采集（顺带停屏） |
| `GET /api/camera/snapshot` | 单帧 JPEG，**base64 塞在 JSON 里**（见下） |
| `GET /api/camera/stream` | MJPEG 流（`multipart/x-mixed-replace`），直接给 `<img src="http://<ip>/api/camera/stream">` |

`snapshot` 的响应（实测 640x360 q70 约 46KB JSON）：

```json
{"image":"/9j/4AAhQVZ...", "format":"jpeg", "width":640, "height":360, "m":0.05, "c":-2.82}
```

> `m` / `c` 是**像素→实际距离**的标定系数（`config.toml` 的 `[camera] calib_m/calib_c`），
> 用来自算"这个框离我多远"。不需要就算着玩，`detect` 不用它们。

**分辨率/帧率/画质都在 `config.toml` 的 `[camera]`**（`width/height/fps/jpeg_quality`），
接口里不能改。`stream` 默认直通摄像头原始 MJPEG（服务端零解码零编码），所以带宽约
4~7 Mbps；嫌大就调低 `jpeg_quality`。

---

## 2. 检测 / 识别：`GET /api/detect`

**"检测"和"识别"在这个系统里是同一件事**：把一帧送进模型，拿回几个框。模型认什么由它自己
训练决定（网球、方块、橘子…），接口不管语义 —— 它只把框给你。

```
GET /api/detect?model=<模型名>[&conf=0.25][&iou=0.45]
```

| 参数 | 位置 | 必填 | 说明 |
|------|------|------|------|
| model | query | 是 | 模型名（= `demo/models/<名字>.cvimodel` 的文件名，不带后缀）。只允许字母数字与 `_ - .` |
| conf | query | 否 | 置信度阈值，`(0,1)`；不给用 0.25 |
| iou | query | 否 | NMS 的 IoU 阈值，`(0,1)`；不给用 0.45 |

成功（HTTP 200）：

```json
{"ok": true, "count": 1, "boxes": [{"x1": 2, "x2": 172, "y1": 0, "y2": 360}]}
```

| 字段 | 含义 |
|------|------|
| boxes[].x1 / y1 | 左上角（原图像素，浮点） |
| boxes[].x2 / y2 | 右下角 |
| count | 框数（可能 0 个 —— 那不是错误） |

> **框宽 = `x2 - x1`，中心 = `(x1+x2)/2, (y1+y2)/2`** —— REST 版只给四角，宽高/中心/面积
> 自己算。Demo 状态机使用同一条检测链，根据框宽、中心偏移和面积决定动作。
>
> 坐标是**原图**（如 640x360），**不是**页面上 `stream` 缩放后的尺寸 —— 两个尺寸不一致时
> 别拿标尺去量屏幕。

失败：

| 情况 | HTTP | 响应 |
|------|------|------|
| 没给 model | 400 | `{"ok":false,"error":"缺少 model 参数（例：/api/detect?model=tennis）"}` |
| model 名字非法 | 400 | `{"ok":false,"error":"model 名字非法（只允许字母数字与 _ - .）：../x"}` |
| conf/iou 越界或不是数 | 400 | `{"ok":false,"error":"conf / iou 要在 0~1 之间（如 ?conf=0.6&iou=0.3）；不给就用默认 0.25 / 0.45"}` |
| **摄像头不可用**（没开、或设备打不开） | 500 | `{"ok":false,"error":"camera not available"}` |
| 摄像头开着但**这一拍还没出帧** | 500 | `{"ok":false,"error":"no frame"}` |
| 模型文件不在或坏了 | 500 | `{"ok":false,"error":"注册模型失败（CVI_NN_RegisterModel rc=…）"}` 之类 |

> **要"连续看"就轮询这个接口**（车上一帧推理约 100~340ms，取决于有没有同时写着屏）；
> 要高帧率画面用 `/api/camera/stream`，它不跑模型。
>
> `conf` 给错值**直接报错**而不是悄悄用默认 —— 调参时最怕"以为生效了其实没生效"。

---

## 3. 模型（"认什么"）

模型就是 `$AKA_HOME/demo/models/<名字>.cvimodel` 一个文件，**文件名去掉后缀就是 `model` 参数的值**。
`GET /api/demo/list` 的 `models` 字段能拿到板上现有的清单：

```bash
curl http://$IP/api/demo/list
# → {"demos":[…], "actions":[{"id":"approach","name":"接近瞄准"},…], "models":["block","orange","tennis"]}
```

### 上传

| 接口 | 谁用 | 怎么发 |
|------|------|--------|
| `POST /api/models/upload?name=<名字>` | 平台 / curl **推文件** | 名字在 query，body 就是文件裸内容（也认 multipart 的 `file` 字段） |
| `POST /api/model/upload` | **浏览器表单直传**（CORS 已开） | multipart，两个字段 `file` + `name` |

```bash
curl --data-binary @orange.cvimodel "http://$IP/api/models/upload?name=orange"
# → {"ok":true,"name":"orange","path":"/root/AKA-00/demo/models/orange.cvimodel","size":12864632}

curl -F "file=@orange.cvimodel" -F "name=orange" "http://$IP/api/model/upload"
# → {"status":"ok","name":"orange","size":12864632,"path":"…"}
```

**同名覆盖，且覆盖即生效**（下一次 `/api/detect` 就用新模型，不用重启）。校验：文件头必须是
`CviModel`（挡"传错文件"）、大小上限 **32MB**；先落 `.part` 再原子换入，传一半断了不会毁掉
正在用的那颗。

### 删除

```bash
curl -X POST http://$IP/api/models/delete -H 'Content-Type: application/json' -d '{"name":"orange"}'
# → {"ok":true,"name":"orange","cards":["追橘子"]}      ← cards = 正在用它的卡片名
```

只删这一个文件。**用它建过的卡片不会跟着删**，只是变成 `ready:false`、点开始报
`模型文件缺失` —— 重传一个同名模型就原地复活。删完记得 `GET /api/demo/list` 刷新清单。

---

## 4. Demo 运行

### 一张卡片 = 动作 × 模型

动作是 `demo/<动作>.json` 配置，模型是 `demo/models/<模型>.cvimodel`，
卡片把两者和参数覆盖存到 `demo/configs/<卡片名>.json`。
`approach` 到位停车，`grab` 到位抓取，均由 C++ 追踪状态机执行。

卡片用于保存参数反复运行，也可以直接指定动作和模型，不建卡。

### 启动：`POST /api/demo/init`

```jsonc
// 运行卡片；显式参数只临时覆盖，不写回卡片
{"name":"追网球接近","grab_offset":65,"wait":false}

// 直接运行
{"action":"approach","model":"tennis",
 "target_size":320,"speed":30,"turn_speed":25,"mode":"once","wait":false}
```

| 字段 | 说明 |
|---|---|
| name | 卡片名；给出时使用卡片的动作和模型 |
| action / model | 没有 name 时必填；动作配置 ID / 模型 ID |
| target_size | 目标框宽，原图 px；内置默认 300，到位还须对准夹爪 |
| speed / turn_speed | 前后移动 / 转向速度百分比，内置默认 25 / 25，执行时最高 70 |
| mode | `once`（默认，一轮，最多 5 分钟）/ `loop`（持续多轮） |
| 高级参数 | 与动作 JSON 的字段相同，见下一节 |
| wait | once 默认等待；loop 默认不等待 |

`once` 默认等流程结束再返回：

```jsonc
{"completed":true}
{"completed":false,"error":"failed: 推理失败：camera not available"}
{"completed":false,"error":"timeout: 到最大执行时间（5 分钟）"}
```

`completed:true` 表示流程正常结束，包含“目标丢失”，并不保证到位或夹到实物。
请同时查看状态中的 `message`；夹爪没有位置反馈，只能确认抓取序列结束。

显式 `wait:false` 立刻返回：

```json
{"status":"started","name":"追网球接近","script":"approach","action":"approach",
 "model":"tennis","pid":682,"pgid":682,"completed":false}
```

`wait` 只决定当前 HTTP 请求是否等待，不改变执行器的时限。
`loop` 默认立刻返回，显式 `wait:true` 会被 HTTP 400 拒绝。
请求最多等 310 秒；设备调用若阻塞，工作线程要等调用返回才能退出。

| 启动失败 | HTTP | 说明 |
|---|---|---|
| 卡片不存在或配置读不了 | 400 | 错误中包含 `demo/configs/<卡片名>.json` |
| 动作配置不存在或无效 | 400 | 如 `动作配置打不开：demo/approach.json` |
| 模型文件不存在 | 400 | `模型不存在：demo/models/<模型>.cvimodel` |
| 名字或参数非法 | 400 | 错误中说明字段及合法范围 |
| 已有 Demo 在运行 | 409 | 先 stop，等当前运行结束后再启动 |

### 直接运行动作：`POST /api/demo/run`

```json
{"action":"grab","params":{"model":"tennis","target_size":300,"mode":"once"},"wait":false}
```

旧 `script` 字段仍可代替 `action`。动作配置、参数与模型名会在启动前校验，
模型文件由推理时加载。`wait` 与 `init` 语义一致，立刻返回时响应为
`{ok,state,script,mode,completed:false}`。

### 看状态与停止

```json
{"state":"running","script":"approach","model":"tennis","card":"追网球接近","mode":"once",
 "round":1,"calls":92,"action":"forward","phase":"forward","message":"启动",
 "notes":{"box_w":"298","offset":"28"}}
```

`GET /api/demo/status` 的字段：

| 字段 | 含义 |
|---|---|
| state | `idle` / `running` / `done`（正常结束，包括丢失目标）/ `failed`（推理、相机、夹爪忙、底盘掉线等）/ `aborted`（停止、接管、退出或 once 超时） |
| script | 动作 ID，保留字段名兼容现有客户端 |
| model / card | 模型 / 发起运行的卡片名；直接运行时 card 为空 |
| mode / round | 执行方式 / 当前轮次 |
| message | 结束原因 |
| phase | `detect`、`forward`、`back`、`turn_left`、`turn_right`、`gap`、`arrived`、`grabbing`；结束后与 state 一致 |
| calls / action | 检测和动作执行计数 / 最近一次命令 |
| notes | 字符串观测值：目标框宽 `box_w`、相对中心偏移 `offset` 或丢失时间 `lost_ms` |

`POST /api/demo/stop` 先刹停仍由 Demo 控制的底盘，通知流程退出，返回
`status:"stopped"`；已经没有 Demo 在运行时返回 `status:"already_stopped"`。
人工接管后，Demo 停止和收尾不会覆盖人工指令。
已经启动的夹爪序列仍按驱动时序完成。

### 卡片管理

| 接口 | 说明 |
|---|---|
| `GET /api/demo/list` | 卡片、动作、模型清单；配置无效或模型缺失的卡片仍列出，ready=false |
| `GET /api/demo/config?name=<卡片名>` | 返回动作默认值与卡片覆盖合并后的全部有效参数 |
| `POST /api/demo/config` | 新建或修改，必填 name/action/model，可提交基础和高级参数 |
| `POST /api/demo/delete` | 只删卡片配置，动作与模型文件保留 |
| `GET /api/demo/name` | 返回正在运行的卡片名、动作 ID 和模型 ID |

同名、同动作、同模型修改时，未提交的已有参数会保留。因此在网页上修改基础参数不会丢失
高级调参。要恢复某个字段对动作默认值的继承，从卡片 JSON 中删除对应键。

---

## 5. 动作配置（C++ + JSON）

`$AKA_HOME/demo/<动作名>.json` 的格式：

```json
{
  "name":"接近瞄准",
  "type":"track",
  "on_arrival":"stop",
  "params":{"target_size":300,"speed":25,"turn_speed":25,"grab_offset":50}
}
```

当前支持 `type:"track"`。`on_arrival` 必填：`stop` 到位停车，`grab` 到位抓取。
`name` 是显示名，省略时使用文件名；`params` 必须是对象。
每次启动重新读取动作配置，运行中和循环的后续轮次使用启动时的参数快照。
文件大小上限为 64KiB。数值必须有限，整数参数不能传小数或字符串。

参数优先级：内置默认值 → 动作 JSON 默认值 → 卡片覆盖 → 本次请求显式覆盖。
`mode` 在卡片或请求中设置，默认 once。高级参数如下：

| 字段 | 内置默认值 | 用途 |
|---|---|---|
| center_margin / align_margin | 80 / 25 px | 粗调转向阈值 / 到位时允许的对准误差；范围 0～100000 |
| grab_offset | 50 px | 夹爪瞄准位置相对画面中心的偏移，正数表示偏右；范围 -100000～100000 |
| turn_pulse_k | 0.5 ms/px | 转向时长与对准误差的比例；范围 0.000001～1000 |
| turn_pulse_min / turn_pulse_max / fine_pulse_max | 300 / 400 / 500 ms | 转向最短时长 / 粗调最长时长 / 精调最长时长 |
| forward_pulse_ms | 600 ms | 每次前进时长 |
| too_close_k | 1.5 | 框宽超过 target_size × 此倍数时后退；范围 1.000001～100 |
| back_pulse_k | 2.0 ms/px | 后退时长与超出太近阈值的宽度之比；范围 0.000001～1000 |
| back_pulse_min / back_pulse_max | 300 / 700 ms | 后退最短 / 最长时长 |
| lost_ms | 1500 ms | 连续丢失目标超过此时长时正常结束本轮 |
| loop_gap_ms | 30 ms | 脉冲结束或未找到目标后，下一次检测前的间隔 |
| grab_wait_ms | 4000 ms | 抓取后至少等待的时长，还须等待夹爪序列实际结束 |
| round_gap_ms | 200 ms | loop 两轮之间的间隔 |
| conf / iou | 0.25 / 0.45 | 检测置信度 / NMS 阈值；范围 0.000001～0.999999，推理层再限制到 0.01～0.99 |
| on_arrival | 来自动作配置 | 卡片或请求可覆盖为 stop / grab |

所有时长为整数，范围 1～60000ms，只有 `grab_wait_ms` 允许 0。
转向最短时长不能超过粗调或精调最长时长，后退最短时长不能超过最长时长。
`target_size` 范围 1～100000px；速度为 1～100 的整数，执行器限制到最高 70。
未识别字段会被忽略。

状态机取面积最大的有效目标框，优先判断：太近后退 → 到位且对准 →
偏出中心时粗调转向 → 距离够但未对准时精调转向 → 前进。
转向时长按夹爪对准误差计算，后退时长按超出太近阈值的宽度计算，向下取整并限制到配置范围。
丢失目标时原地等待。执行器每 20ms 推进一次，实际时长还受调度和设备调用耗时影响。

复制 JSON、换文件名和显示名、调整参数，可以增加同类动作。
增加新的状态、判断规则或动作类型，需要修改 `cpp/capp/src/demo_machine.cpp`，
必要时扩展配置解析和 `demo_runner.cpp`，再编译。

OTA 保留卡片目录中板上的同名配置；动作默认配置按新包替换，板上自行新增的根目录动作配置
也不会自动保留。持久调参放在卡片中，新增动作配置放进仓库后打包。

---

## 6. 执行约束与移植边界

| 约束 | 说明 |
|---|---|
| 速度上限 ±70% | 执行器限制每次驱动指令，JSON 不能解除 |
| 一次一个 Demo | init/run 的重复启动返回 HTTP 409 |
| once 最多 5 分钟 | 每个 tick 和推理返回后检查，超时终止流程 |
| loop 持续运行 | 正常轮次结束后继续，失败或中断时退出，无总时长上限 |
| 人工接管 | 控制指令代际号改变后退出，停止和收尾不覆盖人工动作 |
| stop / 服务退出 / 底盘掉线 | 终止流程，仍由 Demo 控制的运动会停车 |
| 配置校验 | 参数在启动前校验，不运行 JSON 中的任意代码 |

状态机核心不含线程、睡眠、设备 I/O 或动态分配，可独立以 C++17、
`-fno-exceptions -fno-rtti` 编译。POSIX 线程、计时、摄像头、NPU 和电机操作位于执行器及硬件层。
移植到 Chenlong/tgoskits 时仍需适配和验证这些接口；RISC-V Linux 编译通过不代表目标系统已经可运行。

开发机验证不操作硬件：

```bash
make -C cpp/capp test-demo
```

---

## 7. 一条龙例子（可直接复制）

```bash
IP=<板子IP>; MODEL=tennis

# 1) 传一颗模型（同名覆盖、覆盖即生效）
curl --data-binary @orange.cvimodel "http://$IP/api/models/upload?name=orange"

# 2) 开摄像头，看它在哪、框多大（据此定 target_size）
curl -X POST http://$IP/api/camera/open
curl "http://$IP/api/detect?model=$MODEL&conf=0.5"

# 3) 跑一次：不建卡，直接组装；默认等跑完
curl -X POST http://$IP/api/demo/init -H 'Content-Type: application/json' \
     -d "{\"action\":\"approach\",\"model\":\"$MODEL\",\"target_size\":320,\"speed\":30,\"turn_speed\":25}"

# 4) 想边跑边看：wait=false + 轮询 status（notes 里有 box_w/offset，调参就看它）
curl -X POST http://$IP/api/demo/init -H 'Content-Type: application/json' \
     -d "{\"action\":\"approach\",\"model\":\"$MODEL\",\"target_size\":320,\"wait\":false}"
while true; do curl -s http://$IP/api/demo/status; echo; sleep 1; done

# 5) 停
curl -X POST http://$IP/api/demo/stop
```

## 8. 常见坑（都是踩过的）

| 现象 | 原因 |
|------|------|
| `/api/detect` 回 `camera not available` | 摄像头没开（先 `POST /api/camera/open`），或设备打不开 |
| `/api/detect` 回 `no frame` | 摄像头开着、但这一拍还没出帧（刚开没多久）；连续这样就是流卡住了 |
| `POST /api/camera/open` 一直 500、但 `snapshot` 还能出图 | 采集流卡死、缓存里还有旧帧（两次 snapshot 字节完全相同就是在骗你）。**重启 capp 恢复**（实测 2026-09-21；根因待查：卡死后没法从 API 侧再拉起来） |
| 框的坐标对不上屏幕 | 坐标是**原图**像素；页面上的画面可能被缩放过 |
| `init` 一直不返回 | 默认 `wait=true`，它在等流程结束（最多 310 秒）。想立刻拿控制权就 `wait:false` |
| 请求回 `timeout: 等了 310 秒…` | 底层设备调用尚未返回；可用 `POST /api/demo/stop` 刹停仍归 Demo 控制的底盘 |
| `loop` 模式传了 `wait:true` 被 400 | 循环不会自己结束，等下去是挂死连接 |
| 第二个 `init` 回 409 | 已有 Demo 在运行，先 `stop` 并等它结束 |
| 删了模型，卡片还在但点开始报错 | 卡片不跟着删（故意的）；重传同名模型即复活 |
| 传模型回 `不是 cvimodel（文件头不是 CviModel）` | 传错文件了 |
| 相机开着但屏没画面（带屏板） | 屏跟随摄像头：摄像头一开就自动起屏；`/api/display/status` 看 `running` |
