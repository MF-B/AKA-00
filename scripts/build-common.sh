#!/usr/bin/env bash
# 由构建入口 source；不改用户的 shell 配置或系统 Node。

AKA_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=build-versions.env
source "$AKA_ROOT/scripts/build-versions.env"
AKA_BUILD_ENV_DIR="${AKA_BUILD_ENV_DIR:-$AKA_ROOT/.build-env}"
[[ "$AKA_BUILD_ENV_DIR" = /* ]] || AKA_BUILD_ENV_DIR="$AKA_ROOT/$AKA_BUILD_ENV_DIR"
AKA_DOWNLOAD_DIR="$AKA_BUILD_ENV_DIR/downloads"
AKA_TOOLCHAIN_DIR="$AKA_BUILD_ENV_DIR/$AKA_TOOLCHAIN_NAME"
AKA_TPU_DIR="$AKA_BUILD_ENV_DIR/tpu-sdk-$AKA_TPU_COMMIT"
AKA_NODE_DIR="$AKA_BUILD_ENV_DIR/node-v$AKA_NODE_VERSION-linux-x64"
AKA_MBEDTLS_DIR="$AKA_BUILD_ENV_DIR/mbedtls-$AKA_MBEDTLS_COMMIT"

aka_die() { echo "错误：$*" >&2; exit 1; }

aka_check_platform() {
    [[ "$(uname -s)" = Linux && "$(uname -m)" = x86_64 ]] ||
        aka_die "原生构建需要 x86_64 Linux；其他平台请使用 Docker 的 linux/amd64 构建。"
    case "$AKA_ROOT:$AKA_BUILD_ENV_DIR" in
        *[[:space:]]*) aka_die "当前 Makefile 要求项目路径和 AKA_BUILD_ENV_DIR 不含空白字符。" ;;
    esac
}

aka_check_host_tools() {
    local cmd missing=()
    for cmd in bash make cmake git curl tar xz gzip sha256sum nproc flock file g++; do
        command -v "$cmd" >/dev/null 2>&1 || missing+=("$cmd")
    done
    if ((${#missing[@]})); then
        echo "缺少系统工具：${missing[*]}" >&2
        echo "执行 ./scripts/setup-build.sh --install-system-deps，或按 docs/build.md 手动安装。" >&2
        return 1
    fi
}

aka_verify_file() {
    local expected="$1" path="$2"
    [[ -f "$path" ]] && [[ "$(sha256sum "$path" | cut -d ' ' -f 1)" = "$expected" ]]
}

aka_git_ready() {
    local path="$1" commit="$2"
    [[ -d "$path/.git" ]] &&
        [[ "$(git -C "$path" rev-parse HEAD 2>/dev/null)" = "$commit" ]] &&
        git -C "$path" diff --quiet && git -C "$path" diff --cached --quiet
}

aka_check_ready() {
    local missing=() tool lib
    for tool in gcc g++ ar readelf; do
        [[ -x "$AKA_TOOLCHAIN_DIR/bin/riscv64-unknown-linux-musl-$tool" ]] || missing+=("Xuantie $tool")
    done
    [[ -f "$AKA_TOOLCHAIN_DIR/.archive-sha256" ]] &&
        [[ "$(cat "$AKA_TOOLCHAIN_DIR/.archive-sha256")" = "$AKA_TOOLCHAIN_SHA256" ]] || missing+=("工具链版本校验记录")
    aka_git_ready "$AKA_TPU_DIR" "$AKA_TPU_COMMIT" || missing+=("固定版本 TPU SDK")
    [[ -f "$AKA_TPU_DIR/include/cviruntime.h" ]] || missing+=("TPU 头文件")
    for lib in libcviruntime-static.a libcvikernel-static.a libcvimath-static.a libz.a; do
        [[ -s "$AKA_TPU_DIR/lib/$lib" ]] || missing+=("TPU $lib")
    done
    [[ -x "$AKA_NODE_DIR/bin/node" && -x "$AKA_NODE_DIR/bin/npm" ]] &&
        [[ "$("$AKA_NODE_DIR/bin/node" --version)" = "v$AKA_NODE_VERSION" ]] || missing+=("Node $AKA_NODE_VERSION")
    aka_verify_file "$AKA_JPEG_SHA256" "$AKA_DOWNLOAD_DIR/jpegsrc.v9f.tar.gz" || missing+=("校验通过的 libjpeg 源包")
    aka_git_ready "$AKA_MBEDTLS_DIR" "$AKA_MBEDTLS_COMMIT" || missing+=("固定版本 mbedTLS 源码")
    [[ "$(git -C "$AKA_MBEDTLS_DIR/framework" rev-parse HEAD 2>/dev/null)" = "$AKA_MBEDTLS_FRAMEWORK_COMMIT" ]] || missing+=("mbedTLS framework 子模块")
    if ((${#missing[@]})); then
        printf '构建环境未就绪：%s\n' "${missing[*]}" >&2
        echo "请先运行 ./scripts/setup-build.sh" >&2
        return 1
    fi
}

aka_use_environment() {
    export PATH="$AKA_NODE_DIR/bin:$PATH"
    export TOOLCHAIN_PREFIX="$AKA_TOOLCHAIN_DIR/bin/riscv64-unknown-linux-musl-"
    export TPU_SDK_DIR="$AKA_TPU_DIR"
    export AKA_JPEG_ARCHIVE="$AKA_DOWNLOAD_DIR/jpegsrc.v9f.tar.gz"
    export AKA_MBEDTLS_SOURCE="$AKA_MBEDTLS_DIR"
    export npm_config_cache="$AKA_BUILD_ENV_DIR/npm-cache"
}
