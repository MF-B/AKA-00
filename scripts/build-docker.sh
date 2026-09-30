#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
variant=""
jobs=4
usage() {
    cat <<'USAGE'
用法：./scripts/build-docker.sh [--screen | --noscreen | --all] [-j JOBS]
只需 Docker + Buildx，无需在宿主机安装交叉工具链或 Node。
使用 Ubuntu 24.04 linux/amd64 容器，调用同一套原生初始化和构建脚本。
产物导出到 output/docker/screen/ 或 output/docker/noscreen/。
USAGE
}
die() { echo "错误：$*" >&2; exit 1; }
while (($#)); do
    case "$1" in
        --screen|--noscreen|--all)
            [[ -z "$variant" ]] || die "只能选择一种构建范围。"
            variant="${1#--}" ;;
        -j|--jobs)
            [[ $# -ge 2 && "$2" =~ ^[1-9][0-9]*$ ]] || die "并行度需要正整数。"
            jobs="$2"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "未知选项：$1（使用 --help 查看用法）" ;;
    esac
    shift
done
variant="${variant:-screen}"
command -v docker >/dev/null || die "请先安装 Docker 和 Buildx。"
docker buildx version >/dev/null || die "Docker Buildx 不可用。"
docker info >/dev/null || die "无法访问 Docker 服务。"
source_commit="$(git -C "$root" rev-parse HEAD 2>/dev/null || echo unknown)"
source_dirty=unknown
if [[ "$source_commit" != unknown ]]; then
    if [[ -n "$(git -C "$root" status --porcelain)" ]]; then source_dirty=1; else source_dirty=0; fi
fi
docker buildx build --platform linux/amd64 --target artifacts \
    --build-arg "VARIANT=$variant" --build-arg "JOBS=$jobs" \
    --build-arg "SOURCE_COMMIT=$source_commit" --build-arg "SOURCE_DIRTY=$source_dirty" \
    --output "type=local,dest=$root/output/docker" "$root"
echo "构建完成：$root/output/docker/$([[ "$variant" = all ]] && echo '{screen,noscreen}' || echo "$variant")/aka-00-server"
