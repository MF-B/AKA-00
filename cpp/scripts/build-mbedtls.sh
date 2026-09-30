#!/bin/sh
# =============================================================================
# 交叉编译 mbedTLS（riscv64 musl 静态）→ cpp/third_party/mbedtls/
#
# capp 的 HTTPS 服务（HttpServer::listen_tls）走 mbedTLS 做 TLS 终止。
# 系统不再自带交叉版 mbedTLS，首次交叉编译前先跑一次本脚本。
#
# 用法:
#   ./build-mbedtls.sh                            # 自动 git clone mbedtls 并交叉编译
#   ./build-mbedtls.sh /path/to/mbedtls-src       # 用本地源码目录（已 git submodule init）
#   TOOLCHAIN_PREFIX=/path/to/riscv64-unknown-linux-musl- ./build-mbedtls.sh
#
# 产物: cpp/third_party/mbedtls/{libmbedtls.a, libmbedcrypto.a, libmbedx509.a}
#
# 说明：mbedTLS 3.x 用 CMake 构建（已移除 ./configure）；其 framework/ 子模块必须
# 初始化，所以这里用 git clone 而不是下载 release tarball（release tarball 不含
# 子模块）。
# =============================================================================
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/../third_party/mbedtls"
WORK="$HERE/../third_party/build-mbedtls"
# shellcheck source=../../scripts/build-versions.env
. "$HERE/../../scripts/build-versions.env"

# 工具链：校验并归一化显式前缀，未设时自动探测。
TOOLCHAIN_PREFIX="$(sh "$HERE/find-toolchain.sh")" || exit 1
CC="${TOOLCHAIN_PREFIX}gcc"
AR="${TOOLCHAIN_PREFIX}ar"

# 源：优先命令行参数（本地目录），否则 git clone
SRC_DIR="${1:-${AKA_MBEDTLS_SOURCE:-$WORK/src}}"
if [ -n "${1:-${AKA_MBEDTLS_SOURCE:-}}" ] && [ ! -d "$SRC_DIR" ]; then
    echo "[mbedtls] 指定的源码目录不存在：$SRC_DIR" >&2
    exit 1
fi
if [ ! -d "$SRC_DIR/framework" ] || [ ! -d "$SRC_DIR/library" ]; then
    echo "[mbedtls] fetching $AKA_MBEDTLS_COMMIT (with framework submodule)..."
    mkdir -p "$WORK"
    rm -rf "$SRC_DIR"
    git init -q "$SRC_DIR"
    git -C "$SRC_DIR" remote add origin "$AKA_MBEDTLS_URL"
    git -C "$SRC_DIR" fetch --depth 1 origin "$AKA_MBEDTLS_COMMIT"
    git -C "$SRC_DIR" checkout -q --detach FETCH_HEAD
    ( cd "$SRC_DIR" && git submodule update --init --depth 1 >/dev/null )
fi
if [ "$(git -C "$SRC_DIR" rev-parse HEAD)" != "$AKA_MBEDTLS_COMMIT" ] || \
   [ "$(git -C "$SRC_DIR/framework" rev-parse HEAD)" != "$AKA_MBEDTLS_FRAMEWORK_COMMIT" ]; then
    echo "[mbedtls] 源码版本不符，需 $AKA_MBEDTLS_COMMIT / framework $AKA_MBEDTLS_FRAMEWORK_COMMIT" >&2
    exit 1
fi

rm -rf "$OUT"          # 清旧安装（含历史 lib64/ 布局残渣：Makefile.cross 固定从 <prefix>/lib 取库）
mkdir -p "$OUT"
rm -rf "$WORK/build"
mkdir -p "$WORK/build"
cd "$WORK/build"

echo "[mbedtls] configuring for riscv64 musl (static)..."
# CMAKE_INSTALL_LIBDIR 必须显式给 lib：默认值跟构建机的发行版走（Fedora 上是 lib64），
# 而 csrc/capp 的 Makefile.cross 只认 <prefix>/lib。
cmake "$SRC_DIR" \
    -DCMAKE_INSTALL_PREFIX="$OUT" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_AR="$AR" \
    -DCMAKE_C_FLAGS="-O2 -mcpu=c906fdv -mabi=lp64d" \
    -DENABLE_TESTING=Off \
    -DENABLE_PROGRAMS=Off \
    -DMBEDTLS_FATAL_WARNINGS=Off \
    >/dev/null

echo "[mbedtls] building..."
make -j"${AKA_BUILD_JOBS:-$(nproc)}" >/dev/null
make install >/dev/null

echo "[mbedtls] ✓ $OUT/lib/libmbed{tls,crypto,x509}.a"
ls -la "$OUT/lib"/libmbed*.a
