# API 文档

## 控制接口

```
GET /api/control?action=<action>&speed=<speed>&time=<time>&distance=<distance>&angle=<angle>
```

### 参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|
| action | string | 是 | up / down / left / right / stop / grab / release |
| speed | int | 否 | 电机百分比（1~100），默认 50 |
| time | int | 否 | 持续时间（毫秒），无 distance/angle 时生效 |
| distance | float | 否 | **移动距离（厘米 cm）**，up/down 有效 |
| angle | float | 否 | **转动角度（度 °）**，left/right 有效 |

> **优先级**：`distance`/`angle` > `time`。传了 distance 或 angle 就忽略 time。
>
> **所有"会动"的请求都只回一个 `completed`**（true = 这次动作执行完了，失败时多一个
> `error` 说明原因；细节看 `/api/motor/status` 或日志）。三个入口一致：
> `?distance=`/`?angle=`、`?time=`、以及 `POST /api/demo/init|run` 带 `"wait": true`。
>
> 返回时机：
>
> | 请求 | 何时返回 | 返回 |
> |---|---|---|
> | `?distance=` / `?angle=` | **阻塞**到 ESP32 固件闭环报结果（最多 30s） | `{"completed": …}` |
> | `?...&time=` | **阻塞**到动作做完并自动停车 | `{"completed": …}` |
> | `?action=up`（不给 distance/time） | **立刻返回**（持续运动，靠 `?action=stop` 停） | `{status: success}` |
> | `?action=grab` / `release` | **阻塞**到夹爪那套序列做完（ZP10S 约 3.5s） | `{"completed": …}` |
>
> `grab`/`release` **不排队**：上一段还没做完时，后来的请求直接回
> `{"completed":false,"error":"夹爪正忙：上一段动作还没做完（这次没做，也没排队）"}`（400），
> 不会攒成一串挨个执行（实测连点 5 次 → 只执行 1 次）。
>
> `distance`/`angle` 的结论来自固件（它自己闭环 + 回状态），不是主机猜的 ——
> 车被卡住会回 `aborted`/`timeout` 而不是 `completed`。
>
> `speed` 是直接发给 ESP32 PID 控制器的目标百分比（`setMotorSpeed(±100)` → `target_rpm = speed × 150 / 100`）。`100%` 对应约 `0.49 m/s`，由 `PWM_RPM_MAX=150 RPM × 轮径62mm × π / 60` 推出。

### 距离运动示例

```bash
# 前进 30 厘米，速度 50%
curl "http://<ip>/api/control?action=up&distance=30&speed=50"

# 后退 15 厘米，速度 30%
curl "http://<ip>/api/control?action=down&distance=15&speed=30"

# 左转 90 度，速度 40%
curl "http://<ip>/api/control?action=left&angle=90&speed=40"

# 右转 45 度（用默认 speed=50）
curl "http://<ip>/api/control?action=right&angle=45"
```

### 时间运动示例

```bash
# 前进 2 秒，速度 50%
curl "http://<ip>/api/control?action=up&speed=50&time=2000"

# 停止
curl "http://<ip>/api/control?action=stop"
```

### 抓取

```bash
curl "http://<ip>/api/control?action=grab"
curl "http://<ip>/api/control?action=release"
```

### speed 物理含义对照

`speed` 是 ESP32 PID 控制器的目标百分比。所有路径（摇杆 / 方向键 / REST+time / REST+distance）共用同一套物理含义：

| speed | 目标 RPM | 约合线速度 |
|-------|---------|-----------|
| 30 | 45 | 0.15 m/s |
| 50 | 75 | 0.24 m/s |
| 100 | 150 | 0.49 m/s |

---

## 电机直接控制

```
GET /api/motor/direct?left=<left>&right=<right>&duration=<duration>
```

| 参数 | 类型 | 说明 |
|------|------|------|
| left | int | 左轮 -100~100 |
| right | int | 右轮 -100~100 |
| duration | float | 持续时间（秒），0 为持续 |

```bash
# 全速前进
curl "http://<ip>/api/motor/direct?left=100&right=100"

# 原地右转
curl "http://<ip>/api/motor/direct?left=50&right=-50"

# 前进 1.5 秒
curl "http://<ip>/api/motor/direct?left=80&right=80&duration=1.5"
```

---

## 电机状态

```
GET /api/motor/status
```

```json
{
  "left_speed": 0.0,
  "right_speed": 0.0,
  "left_target": 50,
  "right_target": 50,
  "gripper_status": "stopped"
}
```

---

## 速度配置

```
GET /api/config/speed
POST /api/config/speed
```

```json
{"forward_speed": 50, "turn_speed": 50}
```

---

## 摄像头

### 状态

```
GET /api/camera/status
```

```json
{"camera_on": true}
```

### 打开 / 关闭

```
POST /api/camera/open
POST /api/camera/close
```

```json
{"camera_on": true}     // open 成功；打不开返回 500
{"camera_on": false}    // close
```

> 摄像头是**全局唯一**的一份：屏显示、浏览器取流、单帧推理共用它。
> 关闭会同时熄屏（屏上显示待机图），对前端透明；打开后屏自动实时出图。

### 抓拍（单张图）

```
GET /api/camera/snapshot
```

```json
{
  "image": "<base64 JPEG>",
  "width": 640,
  "height": 360,
  "format": "jpeg",
  "m": 2671.82,
  "c": -2.82
}
```

| 字段 | 说明 |
|------|------|
| image | 整帧图片的 base64（JPEG） |
| width / height | 图片像素尺寸 |
| format | 固定为 `jpeg` |
| m / c | 距离标定常数：`D = m / P + c`（`P` = 目标在画面中的像素尺寸，`D` = 距离）。配合检测框用，见[距离标定](dist-calibration.md) |

> 摄像头没开会**自动打开**（注意有副作用：`camera_on` 变成 true、板载屏开始实时出图），所以这里
> 拿到的是**实时帧**。打不开时（设备被占用/不存在）返回 `500` + `{"error":"camera not available"}`。
>
> MJPEG 摄像头是**原帧直通**（不重新编码，quality 参数对它无效）；只有 YUYV 摄像头才编码，quality=70。

### 视频流（MJPEG）

```
GET /api/camera/stream?fps=<fps>
```

| 参数 | 类型 | 说明 |
|------|------|------|
| fps | int | 发送帧率上限，默认 15，超出 1~30 会被夹到边界 |

响应为 `multipart/x-mixed-replace; boundary=frame` 的 MJPEG 流（浏览器 `<img src>` 直接用），持续到客户端断开。

> 默认直通摄像头原始 MJPEG 帧（服务端零解码零编码）；只有配置了 `[camera] stream_scale = true`
> 才会在服务端缩放到 `stream_width x stream_height` 后重编码下发 —— 那是拿 CPU 换 WiFi 带宽。
>
> 有人在看流时，板载屏会自动降帧到 `[display] fps_streaming`（0 = 暂停显示）让出 CPU 给浏览器：
> 单核 SoC 上"全屏写屏 + 浏览器取流"会互相拖慢，所以默认浏览器优先。

### 底盘速度 / 综合状态

两个**历史命名**的接口，回的实际是底盘与电机状态（不是摄像头信息），保留是为了兼容前端：

```
GET /api/camera/speed
GET /api/camera/all_status?timestamp=<timestamp>
```

```json
{
  "left_speed": 0.0,
  "right_speed": 0.0,
  "left_target": 50,
  "right_target": 50,
  "gripper_status": "stopped",
  "gripper_target": 0,
  "timestamp_ms": 1730000000000
}
```

`all_status` 在此之上多三个字段：`motor`（电机连接状态）、`image`（base64 JPEG，quality=25）、
`image_format`；传给它的 `timestamp` 会原样回显。`gripper_status` 是夹爪运行状态，夹爪未连接时是 `unknown`。

> **这个接口不会打开摄像头**（与 `snapshot` 不同）：摄像头关着时它读的是内存里缓存的最后一帧，
> 因此 `image` 依然是关闭前那一张、HTTP 依然 200，而 `camera_on` 为 `false`。实测：关闭后连续两次
> 取图，图片字节完全相同。响应里没有帧时间戳，要判断实时性只能看 `camera_on`。
> 从来没出过帧时 `image` 为 `null`。

---

## 单帧推理（物体检测）

```
GET /api/detect?model=<模型名>
```

取当前摄像头的一帧跑一次模型，返回检测框的四个角。

### 参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| model | string | 是 | 模型名，对应 `$AKA_HOME/demo/models/<模型名>.cvimodel`（如 `tennis`、`block`）。只允许字母数字与 `_ - .`，不允许 `/` 与 `..` |
| conf | float | 否 | 置信度阈值，默认 **0.25**。给的值会夹到 0.01~0.99 |
| iou | float | 否 | NMS 的 IoU 阈值，默认 **0.45**。管「两个框算不算同一个目标」（去重），不是置信度 |

> 摄像头没开会自动打开（与 `/api/camera/snapshot` 行为一致）；但**刚打开时可能还没出帧**，
> 这时返回 `{"ok":false,"error":"no frame"}`，隔一下重试即可。
>
> `conf` 管「这个框够不够可信」：调低减少漏检、调高压掉误检。
> `iou` 管「两个框要不要合成一个」：同一个目标画出两个框就调低它，
> 挨着的两个目标被吃掉一个就调高它。
>
> 两个参数给错值（不是数、或不在 0~1）返回 **400**，不会悄悄用默认值 —— 调参时最怕
> 「以为生效了其实没生效」。不给就是 0.25 / 0.45，与老版本行为完全一致。

### 响应

```json
{
  "ok": true,
  "count": 1,
  "boxes": [{"x1": 236.0, "y1": 88.5, "x2": 436.0, "y2": 283.5}]
}
```

| 字段 | 说明 |
|------|------|
| ok | 成功为 `true` |
| count | 框的个数；`0` 是**正常结果**（画面里没有目标） |
| boxes | 框列表，按分数降序、已完成类别内 NMS |

> **坐标是原图像素**（采集分辨率，默认 640x360），与 `GET /api/camera/snapshot` 返回的图
> 同一坐标系 —— 可以直接把框画到那张图上核对。
>
> 只回框的四个角，不回类别/分数/耗时。

### 失败

一律 `400` 或 `500` + `{"ok":false,"error":"..."}`：

| 情况 | HTTP | error 示例 |
|------|------|-----------|
| 没给 model | 400 | `缺少 model 参数（例：/api/detect?model=tennis）` |
| model 名字非法 | 400 | `model 名字非法（只允许字母数字与 _ - .）：../etc/passwd` |
| 模型不存在 / 加载失败 | 500 | `注册模型失败（CVI_NN_RegisterModel rc=…）：/root/AKA-00/demo/models/xxx.cvimodel` |
| 摄像头不可用 / 暂无帧 | 500 | `camera not available` / `no frame` |

### 示例

```bash
# 检测网球（模型 = $AKA_HOME/demo/models/tennis.cvimodel）
curl "http://<ip>/api/detect?model=tennis"

# 换另一颗模型
curl "http://<ip>/api/detect?model=block"
```

```json
{"boxes":[{"x1":234,"x2":434,"y1":88.5,"y2":285.5}],"count":1,"ok":true}
```

### 说明

- **模型只有一个来源**：部署目录下的 `demo/models/`（`make package` 整目录照搬）。裸名字只在
  库里查，不存在就报错，没有隐式回退。
- 接口是**同步**的：每个请求现场取帧 → 推理 → 返回。模型首次请求时加载，之后常驻；
  只有 `?model=` 变了才重新加载。
- TPU 是单实例，`/api/detect` 与 Demo 共用检测器并串行执行；另起 TPU 进程会造成资源竞争。
- 换自己的模型时对一下规格。本仓库 `demo/models/tennis.cvimodel` 板上实测：输入
  `640x480`、`YUV420_PLANAR`、8 位量化；输出 `[1,5,6300,1]` FP32、单类别
  （`6300 = 80×60 + 40×30 + 20×15`，即三个 stride 的网格点数之和）。
  输入尺寸与格式都从模型张量里读，不写配置 —— 模型吃什么就喂什么。

---

## 模型管理

给外部调用方（平台）用：把模型送进部署目录的 `demo/models/` —— 也就是 `/api/detect` 唯一认的那个模型库。

### 上传模型（平台 → 小车，推荐）

```
POST /api/models/upload?name=<模型名>
Content-Type: application/octet-stream
（body = 模型文件的二进制内容）
```

```bash
# raw body：平台直接推文件（推荐）
curl --data-binary @tennis.cvimodel "http://<ip>/api/models/upload?name=tennis"

# multipart：浏览器 / form 客户端也行
curl -F "file=@tennis.cvimodel" "http://<ip>/api/models/upload?name=tennis"
```

| 参数 | 位置 | 必填 | 说明 |
|------|------|------|------|
| name | query | 是 | 模型名，落成 `$AKA_HOME/demo/models/<name>.cvimodel`。只允许字母数字与 `_ - .`，不允许 `/` 与 `..` |
| 文件 | body | 是 | 模型二进制（raw body，或 multipart 里名为 `file` 的字段） |

```json
{"ok": true, "name": "tennis", "path": "/root/AKA-00/demo/models/tennis.cvimodel", "size": 3540016}
```

同步接口：文件收完、校验通过、写盘换入之后才返回（3.5MB 的模型在内网上是一瞬间的事，不需要进度查询）。

> **为什么是"推"而不是"拉"**：小车在机器人的内网里（通常是热点/局域网），平台未必能被它反向访问；
> 这就是模型进入板子的**唯一**方式：由平台把文件推过来（不需要小车去访问平台，
> 也不需要板上有任何"模型商店/下载"的界面）。

**同名覆盖，且覆盖即生效**：`/api/detect` 每次请求都会 stat 模型文件，大小或 mtime 变了就重新加载
—— 换新版本不用重启 capp（代价是那一次请求多等一次模型加载）。

> 文件先落成 `.part`，校验通过后原子换入（`rename`）—— 传到一半、内容不对、中途断电都不会
> 破坏正在用的那颗模型。
>
> 校验两道：文件头必须是 `CviModel`（挡住"上传了别的文件"）；大小上限 **32MB**
> （请求体是整块读进内存的，板上可用内存约 50MB；模型实际约 3.5MB）。
>
> 校验只看文件头，所以「文件头对、内容是坏的」这种能被装上 —— 这时 `/api/detect` 会明确报
> `注册模型失败（CVI_NN_RegisterModel rc=…）`，重新传一个正确的即可，不需要别的清理动作。

| 失败 | HTTP | error 示例 |
|------|------|-----------|
| 没给 name | 400 | `name 参数必填（例：?name=tennis）` |
| name 非法 | 400 | `name 非法（只允许字母数字与 _ - .）：../etc/passwd` |
| 内容不是 cvimodel | 400 | `不是 cvimodel（文件头不是 CviModel）` |
| 请求体为空 | 400 | `请求体为空（把模型文件放进 body）` |
| 超过 32MB | 413 | `文件过大：34603008 字节，上限 32MB` |

### 删除模型

```
POST /api/models/delete
{"name": "tennis"}
```

```json
{"ok": true, "name": "tennis", "cards": ["追网球接近"]}
```

| 失败 | HTTP | error 示例 |
|------|------|-----------|
| 没给 name | 400 | `name 必填（要删的模型名，不带 .cvimodel）` |
| name 非法 | 400 | `模型名非法（只允许字母数字与 _ - .）：../tennis` |
| 没有这个模型 | 400 | `没有这个模型：demo/models/tennis.cvimodel` |

**只删 `demo/models/<名字>.cvimodel` 这一个文件**（Demo 页上模型标签右上角那个 ✕ 走的就是它）。
用它建过的卡**不会跟着删**：卡片配置不动，只是变成 `ready=false`、点开始报 `模型文件缺失` ——
重传一个同名模型就原地复活。响应的 `cards` 是"正在用它的卡片名"，界面拿它提示后果。

> 板上删掉**包里自带**的模型只到下次 OTA 为止：升级按文件名取并集，同名会用包里的版本
> （见 `cpp/scripts/build-ota.sh`）。想让它彻底不来，得从 `cpp/board/demo/models/` 里去掉。

### 训练平台直传（浏览器 → 小车）

训练平台（`yolotrain.chenlongrobot.com`）训练完，浏览器把模型**直传小车**（同一局域网），
车端不做任何运行切换，只落盘 —— 后续验证人工做。与上面那个接口的区别：名字在表单里
（走 query 的旧接口是给 curl / 云端推模型用的），响应字段是 `status/name/size`。
模型落盘即可用：JSON 动作配置与模型独立，上传后选择动作和模型即可运行 ——
传完要么在 Demo 页新建一张卡片（动作 × 这个模型），要么直接
`POST /api/demo/init {"action":"grab","model":"orange"}`。

```
POST /api/model/upload
Content-Type: multipart/form-data

file = <模型二进制，文件名固定 model.cvimodel>    （必填）
name = <槽位名，如 orange>                        （必填）
```

```bash
curl -F "file=@model.cvimodel" -F "name=orange" "http://<ip>/api/model/upload"
```

成功：

```json
{"status":"ok","name":"orange","size":12865136,
 "path":"/root/AKA-00/demo/models/orange.cvimodel",
 "script":"","script_created":false,"actions":["approach","grab"]}
```

（`script` / `script_created` 是为兼容训练平台那份契约保留的字段，现在恒为 `""` / `false`；
`actions` 是当前可用的动作清单，方便平台侧提示"能用哪些动作"。都不属于必须消费的字段。）

| 失败 | HTTP | 响应 |
|------|------|------|
| file 为空 / 后缀不是 `.cvimodel` | 400 | `{"status":"error","message":"invalid file"}` |
| name 为空 / 含 `/`、`..` 等 | 400 | `{"status":"error","message":"invalid name"}` |
| 文件头不是 CviModel / 超过 32MB | 400 / 413 | `{"status":"error","message":"不是 cvimodel（文件头不是 CviModel）"}` |

落盘与副作用：

- 模型 → `demo/models/<name>.cvimodel`（**同名覆盖**，原子换入，坏包不会顶掉正在用的）
- 动作使用仓库预定义的 `demo/grab.json` / `demo/approach.json`；传完模型后可在
  Demo 页新建一张卡片（动作 × 这个模型），或直接
  `POST /api/demo/init {"action":"grab","model":"<名字>"}` 跑一下。
- CORS 与 `OPTIONS` 预检由服务器统一处理（所有响应带 `Access-Control-Allow-Origin: *`，
  预检回 200），浏览器跨域直传不需要额外配置。

## Demo（C++ 状态机 + JSON）

一张 Demo 卡片 = 动作配置 × 模型 + 参数覆盖：

- 动作配置是 `demo/<动作>.json`：`approach` 到位停车，`grab` 到位抓取。
- 模型是 `demo/models/<模型>.cvimodel`。
- 卡片是 `demo/configs/<卡片名>.json`，可在 Demo 页或配置接口创建，名字允许中文。

C++ 执行追踪流程，JSON 设置参数，构建和运行均不需要 Lua。动作默认值在每次启动时重读；
运行中修改文件，要在下一次启动才生效。参数优先级是：内置默认值 → 动作默认值 →
卡片覆盖 → 本次请求显式覆盖。高级字段与流程判断见 [视觉与 Demo 使用说明](vision-demo.md)。

### 列表

```
GET /api/demo/list
```

```json
{
  "demos": [
    {"name":"追网球接近","action":"approach","model":"tennis",
     "ready":true,"script":"approach","path":"/root/AKA-00/demo/models/tennis.cvimodel",
     "kind":"card","error":""}
  ],
  "actions": [{"id":"approach","name":"接近瞄准"},{"id":"grab","name":"追到就夹"}],
  "models": ["orange","tennis"]
}
```

动作列表只包含解析、校验通过的 JSON，显示名来自 `name` 字段，缺省使用文件名。
卡片仍会列出配置无效或资源缺失的条目，用 `ready:false` 和 `error` 说明原因。

### 卡片配置

```
GET  /api/demo/config?name=追网球接近
POST /api/demo/config
POST /api/demo/delete {"name":"追网球接近"}
```

新建或修改的请求：

```json
{"name":"追网球接近","action":"approach","model":"tennis",
 "target_size":300,"speed":20,"turn_speed":18,"grab_offset":65,
 "forward_pulse_ms":700,"mode":"once"}
```

| 字段 | 说明 |
|---|---|
| name | 卡片名，用作配置文件名 |
| action / model | 必填；动作配置 ID / 模型 ID |
| target_size | 到位时目标框宽，原图像素；还须对准夹爪 |
| speed / turn_speed | 前后移动 / 转向速度，整数百分比；有效值 1～100，执行时最高 70 |
| mode | 卡片或请求设置：`once`（默认，一轮，最多 5 分钟）/ `loop`（持续多轮） |
| 其他参数 | 可设置夹爪偏移、转向/后退脉冲、丢失等待和检测阈值等 |

GET 返回合并后的全部有效参数，POST 成功返回 `ok:true` 和有效参数。
同名、同动作、同模型更新会保留未提交的已有覆盖值，网页基础调参不会清掉高级参数。
删除只删卡片，动作和模型文件保留。

OTA 升级时，同名卡片使用板上已有版本；`demo/*.json` 动作默认配置按新包替换，
包括板上自行新增的动作配置。持久调参应放在卡片中，新增动作配置应加入仓库后打包。

### 启动

```jsonc
// 跑已保存卡片；显式参数只覆盖本次运行
{"name":"追网球接近","grab_offset":70,"wait":false}

// 不建卡，直接指定动作和模型
{"action":"grab","model":"tennis","target_size":280,"speed":20,"wait":false}
```

两种请求都发到 `POST /api/demo/init`。一次只允许一个 Demo；
已在运行时返回 HTTP 409 和 `status:"already_running"`。

`once` 默认等流程结束再响应：

```jsonc
{"completed":true}
{"completed":false,"error":"failed: 推理失败：camera not available"}
{"completed":false,"error":"timeout: 到最大执行时间（5 分钟）"}
```

`completed:true` 表示流程正常结束，包含“目标丢失”，并不保证到位或夹到实物；
结束原因应读取 `GET /api/demo/status` 的 `message`。
夹爪没有位置反馈，只能确认抓取序列已结束。

`wait:false` 立刻返回：

```json
{"status":"started","name":"追网球接近","script":"approach","action":"approach",
 "model":"tennis","pid":682,"pgid":682,"completed":false}
```

`wait` 只决定 HTTP 请求是否等待。`loop` 默认立刻返回，显式 `wait:true` 返回 HTTP 400。
请求最多等 310 秒；执行器每个 tick 和推理返回后检查 5 分钟时限。若底层设备调用阻塞，
请求可能先返回等待超时，工作线程要等调用返回才能退出。

### 直接运行动作

```
POST /api/demo/run
```

```json
{"action":"grab","params":{"model":"tennis","target_size":300,"speed":20,"mode":"once"},"wait":false}
```

旧 `script` 字段仍可代替 `action`。动作配置和已知参数在启动前校验，
`params.model` 必须是合法模型名；模型文件由推理时加载。
等待语义与 `init` 一致，立刻返回时使用兼容的响应：

```json
{"ok":true,"state":"running","script":"grab","mode":"once","completed":false}
```

### 状态和停止

```
GET  /api/demo/status
GET  /api/demo/name
POST /api/demo/stop
```

状态示例：

```json
{"state":"running","script":"approach","model":"tennis","card":"追网球接近",
 "mode":"once","round":1,"calls":42,"action":"forward","phase":"forward","message":"启动",
 "notes":{"box_w":"212","offset":"-33"}}
```

| 字段 | 说明 |
|---|---|
| state | `idle` / `running` / `done` / `failed` / `aborted` |
| script | 动作 ID；为兼容现有客户端保留字段名 |
| model / card | 模型 ID / 发起运行的卡片名，直接运行时卡片为空 |
| mode / round | 执行方式 / 当前轮次 |
| message | 结束原因；丢失目标也是正常结束 |
| phase | `detect`、`forward`、`back`、`turn_left`、`turn_right`、`gap`、`arrived`、`grabbing`，结束后与 state 一致 |
| calls / action | 检测和动作执行计数 / 最近一次命令 |
| notes | 字符串观测值：`box_w`、`offset` 或 `lost_ms` |

`name` 接口返回 `{name,action,model}`。`stop` 返回
`{"status":"stopped","name":"追网球接近","action":"approach"}`，
没有运行中的 Demo 时返回 `status:"already_stopped"`。

停止接口先刹停仍由 Demo 控制的底盘，再通知工作线程退出；人工接管后不会覆盖人工动作。
速度最高 70%，运动等待期间每 80ms 维持速度指令。推理失败、相机不可用、夹爪忙和底盘掉线
使状态变为 `failed`；停止、服务退出、人工接管和一次执行超时使状态变为 `aborted`。
`loop` 的正常轮次结束后继续，发生失败或中断时退出。

| 启动失败 | HTTP | 说明 |
|---|---|---|
| 卡片不存在或配置读不了 | 400 | `没有这张卡片（或配置读不了）：demo/configs/xxx.json` |
| 动作配置不存在或无效 | 400 | 如 `动作配置打不开：demo/approach.json` |
| 名字或参数非法 | 400 | 错误中说明字段和范围 |
| init 指定的模型文件不存在 | 400 | `模型不存在：demo/models/apple.cvimodel` |
| 已有 Demo 在运行 | 409 | 先停止，等当前运行结束后再启动 |
| loop 要求 wait:true | 400 | 循环执行不能同步等待 |

## WiFi

都作用在 **`wlan1`** 上（`wlan0` 是那台给用户连的 AP 热点，不动它）。

### 扫描网络

```
GET /api/wifi/scan
```

```json
{"list": [{"ssid": "…", "id": "…", "signal": -52, "secured": true, "is_connected": false}],
 "connected": "…"}
```

阻塞最多约 5 秒；排序是「已连接优先，其次信号从强到弱」，同名网络只留最强的那个。

### 连接

```
POST /api/wifi/connect
Content-Type: application/json

{"ssid": "WiFi名", "password": "密码"}
```

无密码时 `password` 传空字符串。阻塞约 8 秒等关联、再加最多 6 秒等 DHCP，成功回
`{"ip": "…"}`（还没拿到地址时回 `"获取中..."`）；失败回 **408** +
`{"error":"连接超时" | "连接失败，请检查密码或信号" | "未找到该网络"}`。

连接成功会把 `{ssid, password}` 记进 `/etc/aka-wifi.json`（`0600`），capp 每次启动在
后台重放一次，所以**不用每次开机都重连**（只保留最后一个；`rm` 掉即"忘记网络"）。

### 状态

```
GET /api/wifi/status     # {"ssid": "…"|null, "ip": "…"}
GET /api/wifi/ip         # {"ip": "…"}（没连上时回热点地址 192.168.4.1）
```

### 系统 IP

```
GET /api/system/ip       # {"ip": "…"}
```

---

## OTA 固件升级

### 当前版本

```
GET /api/ota/version       # {"version": "v0.6.1", "updated": 1730000000, "service": "AKA-00"}
```

### 检查更新

```
GET /api/ota/check
```

去 `config.toml` 的 `[ota] check_url` 取版本信息，比语义版本号（退而比 `updatedAt`）：

```json
{"current_version": "…", "update_available": true, "latest_version": "…",
 "hardware_desc": "…", "software_desc": "…", "url": "…"}
```

没有可用更新时回 **404** `{"status":"error","message":"未找到可用更新"}`。

### 在线升级

```
POST /api/ota/upgrade
```

异步下载固件并安装，**立刻**回 `{"status":"ok","task_id":"…"}`；已经是新版则回
`{"status":"ok","message":"已是最新版本"}`。固件与暂存都在 `$AKA_HOME/.ota`（不放 `/tmp`，
那是内存盘）。

### 直接上传固件升级（不联网）

```
POST /api/ota/update       # multipart/form-data，文件部分就是固件
```

回 `{"status":"ok","task_id":"…","message":"upload received, installing..."}`；没有文件回
**400** `{"status":"error","message":"no firmware file"}`。

### OTA 状态

```
GET /api/ota/status                              # 没有进行中的任务时 {"status":"idle"}
GET /api/ota/upgrade/progress?task_id=<id>       # {"progress": 0-100, "status": "downloading|installing|done|error", "message": "…"}
```

---

## 系统信息

```
GET /api/system/info
```

```json
{"ip": "192.168.4.1", "mac": "b8:27:eb:xx:xx:xx"}
```

```
GET /api/system/heartbeat
```

返回 CPU、内存、磁盘、运行时间等信息。
