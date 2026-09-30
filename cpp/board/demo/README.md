# Demo：C++ 状态机 + JSON 参数

Demo 的“看目标 → 转向 → 接近 → 到位停车或抓取”由 C++ 状态机执行。
动作默认参数放在 `demo/<动作名>.json`，按模型调整的参数放在
`demo/configs/<卡片名>.json`。调参数后重新运行即可，不需要重新编译。
JSON 使用项目已有的解析器，构建和运行均不需要 Lua。

## 动作配置

例如 `approach.json`：

```json
{
  "name": "接近瞄准",
  "type": "track",
  "on_arrival": "stop",
  "params": {
    "target_size": 300,
    "speed": 25,
    "turn_speed": 25,
    "grab_offset": 50,
    "forward_pulse_ms": 600
  }
}
```

`name` 是界面显示名，省略时使用文件名。当前支持的 `type` 是 `track`。
`on_arrival` 必填：`stop` 表示到位停车，`grab` 表示停车后执行抓取；
`params` 必须是对象，省略的调参字段使用下表中的内置默认值。
文件必须是合法 JSON，大小为 1～65536 字节。

仓库自带 `approach.json`（接近停车）和 `grab.json`（到位抓取）。
复制一份配置、换文件名和 `name`、调整参数，就能增加同类动作，例如更慢的接近动作。
动作 ID 是去掉 `.json` 的文件名，只允许字母、数字及 `_ - .`，最多 64 字节；
以 `_` 开头的文件不会出现在动作列表中。

## 可调参数

像素值均指推理使用的原图坐标，不是浏览器缩小后的画面。正偏移表示目标在画面中心右侧。

| 字段 | 默认值 | 单位和用途 | 有效范围 |
|---|---:|---|---|
| `target_size` | 300 | px；目标框达到这个宽度且对准夹爪时到位 | 1～100000 |
| `speed` | 25 | %；前进、后退速度 | 整数 1～100；实际最高 70 |
| `turn_speed` | 25 | %；原地转向速度 | 整数 1～100；实际最高 70 |
| `center_margin` | 80 | px；偏出画面中心多少就进行粗调转向 | 0～100000 |
| `align_margin` | 25 | px；到位时允许的夹爪对准误差 | 0～100000 |
| `grab_offset` | 50 | px；夹爪瞄准位置相对画面中心的偏移 | -100000～100000 |
| `turn_pulse_k` | 0.5 | ms/px；转向时长 = 系数 × 对准误差绝对值 | 0.000001～1000 |
| `turn_pulse_min` | 300 | ms；粗调和精调转向的最短时长 | 整数 1～60000 |
| `turn_pulse_max` | 400 | ms；粗调转向的最长时长 | 整数 1～60000 |
| `fine_pulse_max` | 500 | ms；距离到位但未对准时，精调转向的最长时长 | 整数 1～60000 |
| `forward_pulse_ms` | 600 | ms；每次前进的时长 | 整数 1～60000 |
| `too_close_k` | 1.5 | 框宽超过 `target_size × too_close_k` 时先后退 | 1.000001～100 |
| `back_pulse_k` | 2.0 | ms/px；后退时长 = 系数 × 超出太近阈值的宽度 | 0.000001～1000 |
| `back_pulse_min` | 300 | ms；后退的最短时长 | 整数 1～60000 |
| `back_pulse_max` | 700 | ms；后退的最长时长 | 整数 1～60000 |
| `lost_ms` | 1500 | ms；连续丢失目标超过此时长时结束本轮 | 整数 1～60000 |
| `loop_gap_ms` | 30 | ms；运动脉冲结束或未检测到目标后，下一次检测前的间隔 | 整数 1～60000 |
| `grab_wait_ms` | 4000 | ms；启动抓取后至少等待多久，还须等待夹爪序列实际结束 | 整数 0～60000 |
| `round_gap_ms` | 200 | ms；`mode=loop` 中两轮之间的间隔 | 整数 1～60000 |
| `conf` | 0.25 | 检测置信度阈值 | 0.000001～0.999999；推理层再限制为 0.01～0.99 |
| `iou` | 0.45 | NMS IoU 阈值 | 0.000001～0.999999；推理层再限制为 0.01～0.99 |
| `on_arrival` | 来自动作配置 | 卡片或请求也可覆盖：`stop` / `grab` | 字符串 |

所有数字必须有限，不能用字符串代替。`turn_pulse_min` 不能超过
`turn_pulse_max` 或 `fine_pulse_max`；`back_pulse_min` 不能超过 `back_pulse_max`。
未识别的参数会被忽略。

状态机每次选择面积最大的有效目标框，按以下优先级判断：

1. 太近：先后退，再检测。
2. 距离到位且对准夹爪：刹车，按 `on_arrival` 停车或抓取。
3. 偏出居中范围：粗调转向。
4. 距离到位但未对准：精调转向。
5. 其余情况：前进。

转向和后退时长向下取整，再限制到配置的上下限。丢失目标时原地等待。
执行器以 20ms 间隔推进状态，实际时长会受调度和设备调用耗时影响。

## 按卡片调参

原有卡片格式继续可用，也可以增加高级参数：

```json
{
  "action": "approach",
  "model": "tennis",
  "target_size": 280,
  "speed": 20,
  "turn_speed": 18,
  "grab_offset": 65,
  "forward_pulse_ms": 700,
  "mode": "once"
}
```

优先级为：内置默认值 → 动作 JSON 默认值 → 卡片参数 → 本次请求显式参数。
`mode` 在卡片或请求中设置：`once` 是默认值，跑一轮；`loop` 持续运行多轮。
动作配置在每次启动时重新读取，运行中和循环的后续轮次使用启动时的参数快照。

网页继续提供基础参数编辑。高级参数可通过卡片 JSON 或 `/api/demo/config` 设置：

```bash
curl -X POST 'http://<板子IP>/api/demo/config' \
  -H 'Content-Type: application/json' \
  -d '{"name":"tennis-approach","action":"approach","model":"tennis","grab_offset":65,"forward_pulse_ms":700}'

curl 'http://<板子IP>/api/demo/config?name=tennis-approach'
```

GET 返回合并后的有效参数。POST 更新同名、同动作、同模型的卡片时，会保留未提交的已有参数，
所以在网页上修改速度不会清掉高级调参。要恢复某个字段对动作默认值的继承，
从卡片 JSON 中删除该字段；只在 POST 中省略它会保留旧值。

启动时临时覆盖不会写回卡片：

```bash
curl -X POST 'http://<板子IP>/api/demo/init' \
  -H 'Content-Type: application/json' \
  -d '{"name":"tennis-approach","grab_offset":70,"wait":false}'

curl -X POST 'http://<板子IP>/api/demo/run' \
  -H 'Content-Type: application/json' \
  -d '{"action":"grab","params":{"model":"tennis","target_size":280,"speed":20},"wait":false}'
```

`run` 的旧 `script` 字段仍可作为动作 ID，状态响应中的 `script` 字段也保留。
`once` 默认等结束再响应，`wait:false` 立刻返回；`loop` 默认立刻返回，不能要求 `wait:true`。
`GET /api/demo/status` 提供 `phase`、最近的 `action`，以及目标框宽/偏移/丢失时间 `notes`。
`done` 和 `completed:true` 表示流程正常结束，包括“目标丢失”；应同时读取 `message`。
夹爪没有位置反馈，抓取序列结束也不能确认实物是否被夹到。

## 执行约束和升级

速度最高 70%，`once` 最多执行 5 分钟；这两个限制写在执行器中，JSON 不能解除。
一次只能运行一个 Demo。停止、服务退出、人工接管、底盘掉线和一次执行超时
都会终止流程；人工接管后 Demo 不再驱动或刹停人工动作。
运动等待期间每 80ms 维持一次速度指令。推理调用返回后会再次检查停止和控制权。
设备调用本身若阻塞，工作线程要等调用返回才能退出；停止接口可先刹停仍归 Demo 控制的底盘。
已经启动的夹爪序列按驱动时序完成，服务退出时会等待它结束。夹爪执行异常会使 Demo 失败。

OTA 默认保留 `demo/configs/` 中板上的同名卡片参数，模型库取并集；
`demo/*.json` 动作默认配置按新包替换。板上自行新增的根目录动作配置也不会自动保留。
需要长期保留的调参放在卡片里；新增动作配置应放进仓库 `cpp/board/demo/` 后重新打包。

## 修改流程和移植

- 调数值、到位是否抓取、增加同类动作：修改 JSON。
- 改判断顺序、增加新的阶段或动作类型：修改
  [demo_machine.cpp](../../capp/src/demo_machine.cpp)，必要时扩展参数解析和执行器，再编译。
- 状态机核心只接收时间、目标观测和夹爪完成信号，输出动作命令；没有线程、睡眠、设备 I/O
  或动态分配。它可以独立以 C++17、`-fno-exceptions -fno-rtti` 编译。
- [demo_runner.cpp](../../capp/src/demo_runner.cpp) 负责 POSIX 线程、计时、摄像头、推理和电机接口。
  移植到 Chenlong/tgoskits 时仍需适配这些平台与设备接口，RISC-V Linux 构建通过不能替代目标系统验证。

开发机上可运行无需硬件的验证：

```bash
make -C cpp/capp test-demo
```

测试覆盖状态转换与时限、JSON 校验、旧卡片、覆盖优先级、接口兼容、停止、
人工接管、定时停车、速度上限、推理失败、底盘掉线和循环执行。
