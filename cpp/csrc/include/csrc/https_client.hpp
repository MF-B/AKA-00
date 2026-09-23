// csrc/https_client.hpp — mbedtls 客户端 TLS（http_client 的 https:// 分支用）
//
// 为什么不用 curl：板子上的 libcurl 是**没编 TLS 后端**的构建 ——
//   curl 8.4.0 (riscv64-buildroot-linux-musl) libcurl/8.4.0 zlib/1.2.13
//                                                       ↑ 只有 zlib，没有 openssl/mbedtls
// 于是 `https://` 直接报 `curl: (1) Protocol "https" not supported or disabled in libcurl`。
// 而 mbedtls 本来就在 capp 里跑着**服务端** TLS（capp/src/http/server.cpp），
// 客户端复用同一份库，零新依赖。
//
// CA 包：set_ca_bundle() 由 capp 启动时设成 $AKA_HOME/cacert.pem（随包分发，
// 见 cpp/board/cacert.pem）。**不设 / 读不出来就直接失败** —— 不做"跳过校验"的降级，
// 那等于没有 https。

#pragma once

#include <functional>
#include <string>

#include "csrc/http_client.hpp"

namespace csrc {

/// 设置 CA 包路径（PEM，可含多张证书）。进程启动时调一次。
void set_ca_bundle(const std::string& path);

/// 当前 CA 包路径（诊断用）
const std::string& ca_bundle();

/// https:// 请求。method = "GET" | "POST"。
/// dest_path 非空 → 响应体写文件（下载路径，body 留空）；否则收进 result.body。
/// 会跟随最多 3 跳重定向（目标必须是 https）。
HttpResult https_exchange(const Url& u, const std::string& method,
                          const std::string& body, const std::string& content_type,
                          const std::string& dest_path,
                          std::function<void(int)> progress_cb, int timeout_sec);

}  // namespace csrc
