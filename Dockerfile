FROM ubuntu:24.04 AS environment

WORKDIR /opt/aka-build
ENV AKA_BUILD_ENV_DIR=/opt/aka-build/.build-env
COPY scripts/build-common.sh scripts/build-versions.env scripts/setup-build.sh ./scripts/
RUN ./scripts/setup-build.sh --install-system-deps

FROM environment AS build
WORKDIR /work
COPY scripts/ ./scripts/
COPY cpp/ ./cpp/
COPY frontend/ ./frontend/
COPY tests/ ./tests/
ARG VARIANT=screen
ARG JOBS=4
ARG SOURCE_COMMIT=unknown
ARG SOURCE_DIRTY=unknown
RUN ./scripts/build.sh "--${VARIANT}" -j "${JOBS}" \
    && mkdir -p /out \
    && for variant in screen noscreen; do \
        if [ "$variant" = screen ]; then dist=cpp/dist; else dist=cpp/dist-noscreen; fi; \
        if [ -f "$dist/aka-00-server" ]; then \
            sed -i "s/^source_commit=.*/source_commit=${SOURCE_COMMIT}/" "$dist/build-manifest.txt"; \
            sed -i "s/^source_dirty=.*/source_dirty=${SOURCE_DIRTY}/" "$dist/build-manifest.txt"; \
            cp -a "$dist" "/out/$variant"; \
        fi; \
    done

# 导出部署目录、安装器和版本记录；构建工具不进入产物。
FROM scratch AS artifacts
COPY --from=build /out/ /
