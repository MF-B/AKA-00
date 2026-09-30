#!/bin/sh
# =============================================================================
# find-tpu-sdk.sh — 定位 CVITEK TPU SDK（cviruntime，SG2002 上跑 YOLO 用）
#
# stdout 打印 SDK 根目录（含 include/ 与 lib/）：
#     SDK="$(sh scripts/find-tpu-sdk.sh)"        # /path/to/tpu-sdk-sg200x
#
# 查找顺序（先命中先用；候选须同时有 include/cviruntime.h 与
# cviruntime/cvikernel/cvimath/z 四份静态库，只认 .so 的 SDK 不算）：
#   1. $TPU_SDK_DIR            显式指定（环境变量或 make 变量；无效直接报错）
#   2. 项目 .build-env/（由 scripts/setup-build.sh 准备）
#   3. $AKARS_TOOLCHAIN_DIR/tpu-sdk-sg200x      chenlongos/akars 自带 SDK
#   4. $HOME/code/*/toolchains/tpu-sdk-sg200x、$HOME/code/*/*/toolchains/tpu-sdk-sg200x
#   5. 上述 toolchains/ 下再嵌套一层的 tpu-sdk-sg200x
#   6. /opt/tpu-sdk-sg200x、/usr/local/tpu-sdk-sg200x、$HOME/tpu-sdk-sg200x
#   7. $HOME/cvitek_tpu_sdk、/home/junbo_dai/cvitek_tpu_sdk   官方 SDK 默认位置
#
# 全找不到 → stderr 列出排查过的路径并 exit 1。
# WITH_TPU=1（默认）时调用方应据此报错（板子部署要靠真 TPU 推理，不能用桩）；
# WITH_TPU=0 只编桩，调用方不调用此脚本。
# =============================================================================
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck source=../../scripts/build-versions.env
. "$ROOT/scripts/build-versions.env"
BUILD_ENV="${AKA_BUILD_ENV_DIR:-$ROOT/.build-env}"
case "$BUILD_ENV" in /*) ;; *) BUILD_ENV="$ROOT/$BUILD_ENV" ;; esac

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    echo "用法: sh find-tpu-sdk.sh     # 打印 TPU SDK 根目录"
    echo "      TPU_SDK_DIR=/path/to/tpu-sdk-sg200x sh find-tpu-sdk.sh"
    exit 0
fi
if [ -n "${1:-}" ]; then
    echo "find-tpu-sdk: 未知参数: $1" >&2
    exit 2
fi

SEARCHED=""

valid_sdk() {
    [ -f "$1/include/cviruntime.h" ] || return 1
    for lib in libcviruntime-static.a libcvikernel-static.a libcvimath-static.a libz.a; do
        [ -s "$1/lib/$lib" ] || return 1
    done
}

# 探测候选：$1 = SDK 根目录，$2 = 说明。命中即打印并退出，否则记入排查列表
try() {
    [ -n "${1:-}" ] || return 1
    if valid_sdk "$1"; then
        printf '%s\n' "$1"
        exit 0
    fi
    SEARCHED="${SEARCHED}  $2$1
"
    return 1
}

# 1. 显式指定（无效即报错，不再兜底，避免悄悄链错 SDK）
if [ -n "${TPU_SDK_DIR:-}" ]; then
    if valid_sdk "$TPU_SDK_DIR"; then
        printf '%s\n' "$TPU_SDK_DIR"
        exit 0
    fi
    echo "find-tpu-sdk: TPU_SDK_DIR=$TPU_SDK_DIR 无效：需 include/cviruntime.h 和 lib/{libcviruntime-static.a,libcvikernel-static.a,libcvimath-static.a,libz.a}" >&2
    exit 1
fi

try "$BUILD_ENV/tpu-sdk-$AKA_TPU_COMMIT" "项目构建环境 " || true

# 同机 SDK 仓库与常见安装位置
if [ -n "${AKARS_TOOLCHAIN_DIR:-}" ]; then
    try "$AKARS_TOOLCHAIN_DIR/tpu-sdk-sg200x" "AKARS_TOOLCHAIN_DIR=" || true
fi
for d in \
    "$HOME"/code/*/toolchains/tpu-sdk-sg200x \
    "$HOME"/code/*/*/toolchains/tpu-sdk-sg200x \
    "$HOME"/code/*/toolchains/*/tpu-sdk-sg200x \
    "$HOME"/code/*/*/toolchains/*/tpu-sdk-sg200x \
    /opt/tpu-sdk-sg200x \
    /usr/local/tpu-sdk-sg200x \
    "$HOME"/tpu-sdk-sg200x \
    "$HOME"/cvitek_tpu_sdk \
    /home/junbo_dai/cvitek_tpu_sdk
do
    try "$d" "候选 " || true
done

{
    echo "find-tpu-sdk: 找不到 CVITEK TPU SDK（需 cviruntime.h 和 cviruntime/cvikernel/cvimath/z 静态库）。"
    echo "已排查："
    printf '%s' "$SEARCHED"
    echo "请显式指定：TPU_SDK_DIR=/path/to/tpu-sdk-sg200x make"
    echo "或只编桩（下板无 TPU 推理）：make WITH_TPU=0"
} >&2
exit 1
