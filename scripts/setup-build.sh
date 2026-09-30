#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C
# shellcheck source=build-common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/build-common.sh"

usage() {
    cat <<'USAGE'
用法：./scripts/setup-build.sh [选项]
准备 x86_64 Linux 构建环境：固定版本 Xuantie、TPU SDK、Node 和第三方源码。
默认只写项目的 .build-env/（AKA_BUILD_ENV_DIR 可指定其他缓存目录）。
  --install-system-deps       用 apt/dnf 安装系统依赖（需要 root 或 sudo）
  --check                     检查环境及交叉编译/静态链接，不下载或安装依赖
  --toolchain-archive PATH     使用本地工具链压缩包，仍校验固定 SHA-256
  -h, --help                  显示帮助
USAGE
}

install_system=0
check_only=0
archive_override=""
while (($#)); do
    case "$1" in
        --install-system-deps) install_system=1 ;;
        --check) check_only=1 ;;
        --toolchain-archive)
            [[ $# -ge 2 && -f "$2" ]] || aka_die "--toolchain-archive 需要一个存在的文件。"
            archive_override="$(realpath "$2")"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) aka_die "未知选项：$1（使用 --help 查看用法）" ;;
    esac
    shift
done
[[ "$check_only" = 0 || ( "$install_system" = 0 && -z "$archive_override" ) ]] ||
    aka_die "--check 不能与安装选项一起使用。"
aka_check_platform

if [[ "$install_system" = 1 ]]; then
    admin=()
    if [[ "$EUID" != 0 ]]; then
        command -v sudo >/dev/null || aka_die "安装系统依赖需要 sudo 或 root。"
        admin=(sudo)
    fi
    if command -v apt-get >/dev/null; then
        "${admin[@]}" apt-get update
        "${admin[@]}" apt-get install -y build-essential cmake curl git ca-certificates xz-utils file util-linux tar gzip
    elif command -v dnf >/dev/null; then
        "${admin[@]}" dnf install -y gcc gcc-c++ make cmake curl git ca-certificates xz file util-linux tar gzip
    else
        aka_die "当前发行版没有 apt/dnf，请手动安装 docs/build.md 中列出的系统依赖。"
    fi
fi
aka_check_host_tools

setup_tmp=""
probe_tmp=""
cleanup() {
    [[ -z "$setup_tmp" ]] || rm -rf -- "$setup_tmp"
    [[ -z "$probe_tmp" ]] || rm -rf -- "$probe_tmp"
}
trap cleanup EXIT

download_verified() {
    local url="$1" expected="$2" dest="$3"
    if [[ -f "$dest" ]]; then
        aka_verify_file "$expected" "$dest" || aka_die "缓存校验失败：$dest；删除该文件后重试。"
        return
    fi
    echo "下载：${dest##*/}"
    curl -fL --retry 3 --connect-timeout 20 --output "$setup_tmp/download" "$url"
    aka_verify_file "$expected" "$setup_tmp/download" || aka_die "下载的 SHA-256 不匹配：$url"
    mv -- "$setup_tmp/download" "$dest"
}

install_archive() {
    local archive="$1" dest="$2" expected="$3"
    if [[ -f "$dest/.archive-sha256" ]] && [[ "$(cat "$dest/.archive-sha256")" = "$expected" ]]; then
        return
    fi
    mkdir -p "$setup_tmp/extract"
    tar -xf "$archive" -C "$setup_tmp/extract" --strip-components=1
    printf '%s\n' "$expected" > "$setup_tmp/extract/.archive-sha256"
    rm -rf -- "$dest"
    mv -- "$setup_tmp/extract" "$dest"
}

checkout_locked() {
    local url="$1" commit="$2" dest="$3"
    if aka_git_ready "$dest" "$commit"; then return; fi
    [[ ! -e "$dest" ]] || aka_die "已有源码不是锁定版本或有修改：$dest；请移走后重新初始化。"
    echo "获取固定提交：$url ($commit)"
    git init -q "$setup_tmp/repo"
    git -C "$setup_tmp/repo" remote add origin "$url"
    git -C "$setup_tmp/repo" fetch --depth 1 origin "$commit"
    git -C "$setup_tmp/repo" checkout -q --detach FETCH_HEAD
    [[ "$(git -C "$setup_tmp/repo" rev-parse HEAD)" = "$commit" ]] || aka_die "Git 提交校验失败。"
    mv -- "$setup_tmp/repo" "$dest"
}

if [[ "$check_only" = 0 ]]; then
    mkdir -p "$AKA_DOWNLOAD_DIR"
    exec 9>"$AKA_BUILD_ENV_DIR/.setup.lock"
    flock 9
    setup_tmp="$(mktemp -d "$AKA_BUILD_ENV_DIR/.setup.XXXXXX")"
    if [[ -n "$archive_override" ]]; then
        aka_verify_file "$AKA_TOOLCHAIN_SHA256" "$archive_override" || aka_die "本地工具链压缩包 SHA-256 不匹配。"
        if [[ ! "$archive_override" -ef "$AKA_DOWNLOAD_DIR/$AKA_TOOLCHAIN_ARCHIVE" ]]; then
            cp -- "$archive_override" "$AKA_DOWNLOAD_DIR/$AKA_TOOLCHAIN_ARCHIVE"
        fi
    fi
    download_verified "$AKA_TOOLCHAIN_URL" "$AKA_TOOLCHAIN_SHA256" "$AKA_DOWNLOAD_DIR/$AKA_TOOLCHAIN_ARCHIVE"
    install_archive "$AKA_DOWNLOAD_DIR/$AKA_TOOLCHAIN_ARCHIVE" "$AKA_TOOLCHAIN_DIR" "$AKA_TOOLCHAIN_SHA256"
    download_verified "$AKA_NODE_URL" "$AKA_NODE_SHA256" "$AKA_DOWNLOAD_DIR/$AKA_NODE_ARCHIVE"
    install_archive "$AKA_DOWNLOAD_DIR/$AKA_NODE_ARCHIVE" "$AKA_NODE_DIR" "$AKA_NODE_SHA256"
    checkout_locked "$AKA_TPU_URL" "$AKA_TPU_COMMIT" "$AKA_TPU_DIR"
    checkout_locked "$AKA_MBEDTLS_URL" "$AKA_MBEDTLS_COMMIT" "$AKA_MBEDTLS_DIR"
    git -C "$AKA_MBEDTLS_DIR" submodule update --init --depth 1 framework
    download_verified "$AKA_JPEG_URL" "$AKA_JPEG_SHA256" "$AKA_DOWNLOAD_DIR/jpegsrc.v9f.tar.gz"
fi
aka_check_ready
aka_use_environment

# 不运行 RISC-V 程序；实际编译并静态链接 SDK，提前发现主机架构/库/CPU 选项不兼容。
probe_tmp="$(mktemp -d)"
cat > "$probe_tmp/probe.cpp" <<'CPP'
#include <cviruntime.h>
int main() {
    CVI_MODEL_HANDLE model = nullptr;
    return CVI_NN_RegisterModel("/dev/null", &model);
}
CPP
"${TOOLCHAIN_PREFIX}g++" -std=c++17 -mcpu=c906fdv -mabi=lp64d -static \
    -I"$TPU_SDK_DIR/include" "$probe_tmp/probe.cpp" -o "$probe_tmp/probe" \
    -L"$TPU_SDK_DIR/lib" -Wl,--start-group -lcviruntime-static -lcvikernel-static -lcvimath-static -lz \
    -Wl,--end-group -lpthread -lm
"${TOOLCHAIN_PREFIX}readelf" -h "$probe_tmp/probe" | grep -q 'Machine:.*RISC-V' || aka_die "交叉产物不是 RISC-V。"
if "${TOOLCHAIN_PREFIX}readelf" -l "$probe_tmp/probe" | grep -q INTERP; then
    aka_die "TPU 链接检查产物不是静态二进制。"
fi
echo "构建环境就绪：Xuantie $AKA_TOOLCHAIN_VERSION / Node $AKA_NODE_VERSION / TPU $AKA_TPU_COMMIT"
echo "缓存目录：$AKA_BUILD_ENV_DIR"
echo "下一步：./scripts/build.sh --screen（或 --noscreen / --all）"
