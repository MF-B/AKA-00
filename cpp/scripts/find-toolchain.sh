#!/bin/sh
# =============================================================================
# find-toolchain.sh — 定位 riscv64-unknown-linux-musl 交叉工具链
#
# stdout 打印工具链前缀（以 "-" 结尾，可直接拼 gcc/g++/ar/strip）：
#     PREFIX="$(sh scripts/find-toolchain.sh)"   # /path/bin/riscv64-unknown-linux-musl-
#
# 查找顺序（先命中先用；候选须有可执行的 <prefix>gcc/g++/ar）：
#   1. $TOOLCHAIN_PREFIX        显式指定（环境变量或 make 变量；无效直接报错）
#   2. PATH 里已有的 riscv64-unknown-linux-musl-gcc
#   3. $AKARS_TOOLCHAIN_DIR     chenlongos/akars 自带工具链目录
#   4. $HOME/code/*/toolchains/*/、$HOME/code/*/*/toolchains/*/   同机 SDK 仓库
#   5. $HOME/toolchains/*/、$HOME/toolchains/*/*/
#   6. /opt/、/usr/local/、$HOME 下的 riscv64-linux-musl-x86_64
#   7. /home/junbo_dai/riscv64-linux-musl-x86_64/   原始开发机 orb 内默认路径
#
# 候选必须同时有 gcc/g++/ar。全找不到时 stderr 列出路径并 exit 1，不退回本机 g++。
#
# 注意：工具链须支持 T-Head 扩展（构建用 -mcpu=c906fdv）；Xuantie 官方 toolchain
# （如 akars/toolchains/xuantie-v3.4.0）满足，musl.cc 的通用 GCC 不支持该 -mcpu。
# =============================================================================
set -eu

SEARCHED=""

# 归一化：工具链根目录或 bin 目录 → 前缀；前缀 → 补齐结尾 "-"
prefix_of() {
    case "$1" in
        "")                           printf '' ;;
        *riscv64-unknown-linux-musl-) printf '%s' "$1" ;;
        *riscv64-unknown-linux-musl)  printf '%s-' "$1" ;;
        */bin|*/bin/)                 printf '%s/riscv64-unknown-linux-musl-' "${1%/}" ;;
        *)                            printf '%s/bin/riscv64-unknown-linux-musl-' "${1%/}" ;;
    esac
}

# PATH 中的前缀也转换为编译器所在的路径，便于 Makefile 调用同一组工具。
resolve_prefix() {
    p="$(prefix_of "$1")"
    cc_path="$(command -v "${p}gcc" 2>/dev/null || true)"
    [ -z "$cc_path" ] || p="${cc_path%gcc}"
}

valid_prefix() {
    [ -x "${p}gcc" ] && [ -x "${p}g++" ] && [ -x "${p}ar" ]
}

# 探测候选：$1 = 目录或前缀，$2 = 说明。命中即打印前缀并退出，否则记入排查列表
try() {
    [ -n "${1:-}" ] || return 1
    resolve_prefix "$1"
    if valid_prefix; then
        printf '%s\n' "$p"
        exit 0
    fi
    SEARCHED="${SEARCHED}  $2${p}
"
    return 1
}

case "${1:-}" in
    -h|--help)
        echo "用法: sh find-toolchain.sh     # 打印交叉工具链前缀（末尾带 -）"
        echo "      TOOLCHAIN_PREFIX=/path/to/bin/riscv64-unknown-linux-musl- sh find-toolchain.sh"
        exit 0 ;;
    "") ;;
    *) echo "find-toolchain: 未知参数: $1" >&2; exit 2 ;;
esac

# 1. 显式指定（无效即报错，不再兜底，避免悄悄用错工具链）
if [ -n "${TOOLCHAIN_PREFIX:-}" ]; then
    resolve_prefix "$TOOLCHAIN_PREFIX"
    if valid_prefix; then
        printf '%s\n' "$p"
        exit 0
    fi
    echo "find-toolchain: TOOLCHAIN_PREFIX=$TOOLCHAIN_PREFIX 无效：需要 ${p}gcc、${p}g++、${p}ar" >&2
    exit 1
fi

# 2. PATH
cc_path="$(command -v riscv64-unknown-linux-musl-gcc 2>/dev/null || true)"
[ -z "$cc_path" ] || try "${cc_path%gcc}" "PATH 中的 " || true

# 3. akars
[ -z "${AKARS_TOOLCHAIN_DIR:-}" ] || try "$AKARS_TOOLCHAIN_DIR" "AKARS_TOOLCHAIN_DIR=" || true

# 4-7. 同机 SDK 仓库与常见安装位置
for d in \
    "$HOME"/code/*/toolchains/*/ \
    "$HOME"/code/*/*/toolchains/*/ \
    "$HOME"/toolchains/*/ \
    "$HOME"/toolchains/*/*/ \
    /opt/riscv64-linux-musl-x86_64 \
    /usr/local/riscv64-linux-musl-x86_64 \
    "$HOME"/riscv64-linux-musl-x86_64 \
    /home/junbo_dai/riscv64-linux-musl-x86_64
do
    try "$d" "候选 " || true
done

{
    echo "find-toolchain: 找不到 riscv64-unknown-linux-musl 交叉工具链（需 <prefix>gcc、<prefix>g++、<prefix>ar）。"
    echo "已排查："
    printf '%s' "$SEARCHED"
    echo "请显式指定：TOOLCHAIN_PREFIX=/path/to/bin/riscv64-unknown-linux-musl- $0"
    echo "或把工具链 bin 目录加入 PATH（须支持 -mcpu=c906fdv 的 Xuantie 官方版本）。"
} >&2
exit 1
