# 在新 Linux 上构建 AKA-00

正式构建入口是仓库根的 `scripts/setup-build.sh` 和 `scripts/build.sh`。
目标是 SG2002 的 RISC-V Linux；原生工具链运行在 x86_64 Linux 上。
Ubuntu 24.04 和 Fedora 的依赖安装分别使用 apt 和 dnf。

## 原生构建

```bash
git clone https://github.com/MF-B/AKA-00.git
cd AKA-00

# 首次执行：安装系统工具，下载并校验项目的固定版本依赖
./scripts/setup-build.sh --install-system-deps

# 前端 → RISC-V 后端 → 打包 → 自解压安装器
./scripts/build.sh --screen
```

`--install-system-deps` 需要 root 或 sudo，只安装 Make、CMake、Git、curl、C++ 编译器、
tar/xz 等系统工具。Node 和交叉工具链安装到项目的 `.build-env/`，无需系统 Node、nvm、
Rust、其他项目或全局 PATH 配置；不会修改 shell 启动文件。
已经装好系统工具时只需 `./scripts/setup-build.sh`。

```bash
./scripts/setup-build.sh --check    # 不下载；检查依赖并试编译/静态链接 TPU SDK
./scripts/build.sh --noscreen       # 无屏版
./scripts/build.sh --all -j 4       # 顺序生成带屏和无屏版
```

统一入口使用 `npm ci` 和 `frontend/package-lock.json` 安装前端依赖，匹配前后端的屏幕
开关，默认启用真实 TPU 推理。两种网页共用 `static/`，所以 `--all` 逐个构建并立即打包；
同一工作区的统一构建通过文件锁串行执行。

| 范围 | 部署目录 | 自解压安装器 |
|---|---|---|
| `--screen`（默认） | `cpp/dist/AKA-00/` | `cpp/dist/aka-00-server` |
| `--noscreen` | `cpp/dist-noscreen/AKA-00/` | `cpp/dist-noscreen/aka-00-server` |

每个产物目录还包含 `build-manifest.txt`，记录依赖版本、源码提交、本地修改状态和
安装器/后端的 SHA-256。`source_dirty=1` 表示构建时工作区有未提交的修改。
修改工具链、SDK 或锁定版本后，统一入口会清除受影响的交叉编译产物，再重新编译；
下载缓存和本机开发产物保留。

## 依赖与缓存

版本集中在 [`scripts/build-versions.env`](../scripts/build-versions.env)：

- Xuantie GCC V3.4.0（支持 `-mcpu=c906fdv`），按 SHA-256 校验。
- Node.js 24.13.0，按 SHA-256 校验；统一入口自动使用此版本。
- CVITEK TPU SDK 固定到 Git 提交，检查 cviruntime/cvikernel/cvimath/z 静态库。
- libjpeg 9f 源包按 SHA-256 校验，mbedTLS 及 framework 子模块固定到 Git 提交。

首次准备需要访问 Xuantie 下载站、nodejs.org、GitHub 和 ijg.org，首次前端构建还需要
访问 npm 包源。后续准备复用下载缓存；环境检查 `--check` 不需要网络。

默认缓存位于 `.build-env/`，已经加入 `.gitignore`。可指定独立目录，两条命令使用
同一设置即可：

```bash
export AKA_BUILD_ENV_DIR="$HOME/.cache/aka-00-build"
./scripts/setup-build.sh
./scripts/build.sh --all
```

工具链包约 201 MiB，也可以手工下载后交给脚本；仍会检查固定 SHA-256：

```bash
./scripts/setup-build.sh --toolchain-archive /path/to/Xuantie-900-gcc-linux-6.6.36-musl64-x86_64-V3.4.0-20260323.tar.gz
```

如果下载中断，重跑初始化即可。已有缓存校验不通过时脚本会报出路径；删除该缓存文件
后重试。项目路径及缓存路径暂时不能包含空白字符（现有 Makefile 的限制）。

手工安装系统工具：

```bash
# Ubuntu / Debian
sudo apt-get update
sudo apt-get install -y build-essential cmake curl git ca-certificates xz-utils file util-linux tar gzip

# Fedora
sudo dnf install -y gcc gcc-c++ make cmake curl git ca-certificates xz file util-linux tar gzip
```

## Docker 构建

宿主机只需可用的 Docker 服务和 Buildx，容器使用 Ubuntu 24.04、linux/amd64。
原生初始化和构建脚本在容器内执行，使用同一份版本锁定；不需要预先执行宿主机的
`setup-build.sh`。

```bash
./scripts/build-docker.sh --screen
./scripts/build-docker.sh --noscreen
./scripts/build-docker.sh --all -j 4
```

产物导出到 `output/docker/screen/` / `output/docker/noscreen/`，每种版本都有
`aka-00-server`、`AKA-00/` 部署目录及 `build-manifest.txt`。构建缓存由 Docker 管理，
版本锁定不变时会复用准备好的依赖层。容器只用于编译，运行软件仍在 RISC-V 板上。

`.dockerignore` 只允许发送源码，排除 `images/firmware/`、`.build-env/`、日志、
`node_modules/` 和已编译的产物，因此不会把本机环境混入容器。
其他主机架构需要 Docker 能模拟 linux/amd64，编译速度取决于模拟环境。

## 现有 Makefile

`make -C cpp` 等底层目标仍然可用，自动检测优先使用项目管理的 `.build-env/`。
手动设置 `TOOLCHAIN_PREFIX` / `TPU_SDK_DIR` 可覆盖检测，旧开发机的搜索路径继续兼容。
直接调用 Makefile 时仍需自行构建前端；新环境推荐统一入口。

状态机回归测试不需要板子：

```bash
make -C cpp/capp test-demo
```

## 部署

```bash
scp -O cpp/dist/aka-00-server root@192.168.4.1:/root/
ssh root@192.168.4.1 'chmod +x /root/aka-00-server'
ssh root@192.168.4.1 'setsid /root/aka-00-server --update >/root/ota.log 2>&1 </dev/null &'
```

无屏版改用 `cpp/dist-noscreen/aka-00-server`。首次安装使用 `--init` 配置热点及开机自启，
随后重启设备；现有设备使用 `--update` 保留现场配置、卡片和用户模型。
安装器放 `/root/`，板上的 `/tmp` 是内存盘。

本地 SD 卡镜像放在 `images/firmware/`，该目录被 Git 忽略，也不参与构建或打包。
