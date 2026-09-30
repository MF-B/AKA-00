#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C
# shellcheck source=build-common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/build-common.sh"

usage() {
    cat <<'USAGE'
用法：./scripts/build.sh [--screen | --noscreen | --all] [-j JOBS]
先执行 ./scripts/setup-build.sh。此入口统一构建前端、RISC-V 后端和安装器。
  --screen       带屏版（默认） → cpp/dist/aka-00-server
  --noscreen     不带屏版       → cpp/dist-noscreen/aka-00-server
  --all          依次生成两种版本，避免共用 static/ 导致混包
  -j, --jobs N   编译并行度（默认不超过 4）
  -h, --help     显示帮助
USAGE
}

variant=""
jobs="$(nproc)"
((jobs <= 4)) || jobs=4
while (($#)); do
    case "$1" in
        --screen|--noscreen|--all)
            [[ -z "$variant" ]] || aka_die "只能选择一种构建范围。"
            variant="${1#--}" ;;
        -j|--jobs)
            [[ $# -ge 2 && "$2" =~ ^[1-9][0-9]*$ ]] || aka_die "并行度需要正整数。"
            jobs="$2"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) aka_die "未知选项：$1（使用 --help 查看用法）" ;;
    esac
    shift
done
variant="${variant:-screen}"
aka_check_platform
aka_check_host_tools
aka_check_ready
aka_use_environment

# static/ 和 third_party/ 为两种版本共用：同一工作区的统一构建必须串行。
exec 8>"$AKA_ROOT/.build-env.lock"
flock 8
export AKA_BUILD_JOBS="$jobs"
# Make 不会因编译参数/工具链版本变化自动重编。统一入口记录依赖版本和安装路径，
# 切换它们时清掉交叉产物，保留下载缓存、源码及本机开发产物。
build_key="$( { cat "$AKA_ROOT/scripts/build-versions.env"; printf '%s\n' "$TOOLCHAIN_PREFIX" "$TPU_SDK_DIR" "$AKA_MBEDTLS_SOURCE"; } | sha256sum | cut -d ' ' -f 1)"
key_file="$AKA_ROOT/cpp/third_party/.managed-build-key"
if [[ ! -f "$key_file" ]] || [[ "$(cat "$key_file")" != "$build_key" ]]; then
    echo "构建依赖已变化或尚未记录，重新编译交叉产物。"
    rm -rf -- "$AKA_ROOT/cpp/csrc/build-cross" "$AKA_ROOT/cpp/csrc/build-cross-noscreen" \
        "$AKA_ROOT/cpp/capp/build-cross" "$AKA_ROOT/cpp/capp/build-cross-noscreen" \
        "$AKA_ROOT/cpp/third_party/jpeg" "$AKA_ROOT/cpp/third_party/mbedtls"
    mkdir -p "$(dirname "$key_file")"
    printf '%s\n' "$build_key" > "$key_file"
fi
npm --prefix "$AKA_ROOT/frontend" ci --no-audit --no-fund

build_variant() {
    local selected="$1" dist target npm_target
    if [[ "$selected" = screen ]]; then
        dist="$AKA_ROOT/cpp/dist"; target=ota; npm_target=build
    else
        dist="$AKA_ROOT/cpp/dist-noscreen"; target=ota-noscreen; npm_target=build:noscreen
    fi
    echo "── 构建 $selected ──"
    # 显式设置前后端编译开关，不受外部 shell 的 WITH_SCREEN/WITH_TPU 干扰。
    WITH_SCREEN="$([[ "$selected" = screen ]] && echo 1 || echo 0)" \
        npm --prefix "$AKA_ROOT/frontend" run "$npm_target"
    make -C "$AKA_ROOT/cpp" "$target" -j"$jobs" WITH_TPU=1 \
        TOOLCHAIN_PREFIX="$TOOLCHAIN_PREFIX" TPU_SDK_DIR="$TPU_SDK_DIR"
    "${TOOLCHAIN_PREFIX}readelf" -h "$dist/AKA-00/aka-capp" | grep -q 'Machine:.*RISC-V' || aka_die "打包的后端不是 RISC-V。"
    if "${TOOLCHAIN_PREFIX}readelf" -l "$dist/AKA-00/aka-capp" | grep -q INTERP; then
        aka_die "打包的后端不是静态二进制。"
    fi
    [[ -s "$dist/AKA-00/static/index.html" ]] || aka_die "包内缺少网页。"
    {
        printf 'variant=%s\n' "$selected"
        printf 'source_commit=%s\n' "$(git -C "$AKA_ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
        if git -C "$AKA_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
            if [[ -n "$(git -C "$AKA_ROOT" status --porcelain)" ]]; then
                printf 'source_dirty=1\n'
            else
                printf 'source_dirty=0\n'
            fi
        else
            printf 'source_dirty=unknown\n'
        fi
        printf 'toolchain=%s\nnode=%s\ntpu_commit=%s\nmbedtls_commit=%s\n' \
            "$AKA_TOOLCHAIN_VERSION" "$AKA_NODE_VERSION" "$AKA_TPU_COMMIT" "$AKA_MBEDTLS_COMMIT"
        (cd "$dist" && sha256sum aka-00-server AKA-00/aka-capp)
    } > "$dist/build-manifest.txt"
    echo "安装器：$dist/aka-00-server"
}
if [[ "$variant" = all ]]; then
    build_variant screen
    build_variant noscreen
else
    build_variant "$variant"
fi
