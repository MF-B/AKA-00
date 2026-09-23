// csrc/https_client.cpp — mbedtls 客户端 TLS 实现（详见头文件注释）

#include "csrc/https_client.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include "csrc/log.hpp"

namespace csrc {

namespace {
std::string g_ca_bundle;
}  // namespace

void set_ca_bundle(const std::string& path) {
    g_ca_bundle = path;
    CAM_INFO("[https] CA 包: %s", path.c_str());
}
const std::string& ca_bundle() { return g_ca_bundle; }

namespace {

std::string tls_strerr(int rc) {
    char buf[160];
    mbedtls_strerror(rc, buf, sizeof buf);
    return std::string(buf);
}

/// 非阻塞 connect + poll —— 把超时落到实处。
/// 不用 mbedtls_net_connect：它是阻塞的、没有超时，网络不通时会把调用线程挂死
/// （OTA 检查是在 HTTP 线程里跑的）。
/// 返回连上的 fd（阻塞模式、已设收发超时），失败返回 -1。
int tcp_connect_timeout(const std::string& host, int port, int timeout_sec) {
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[8];
    std::snprintf(portstr, sizeof portstr, "%d", port);

    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), portstr, &hints, &res) != 0 || !res) return -1;

    int fd = -1;
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        int fl = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        bool ok = false;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            ok = true;
        } else if (errno == EINPROGRESS) {
            struct pollfd pf;
            pf.fd = fd;
            pf.events = POLLOUT;
            pf.revents = 0;
            if (::poll(&pf, 1, timeout_sec * 1000) > 0) {
                int err = 0;
                socklen_t len = sizeof err;
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) ok = true;
            }
        }
        if (ok) {
            ::fcntl(fd, F_SETFL, fl);   // 回到阻塞模式，TLS 握手/读写用 socket 超时兜底
            break;
        }
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return -1;

    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

/// 一次 TLS 会话所需的全部 mbedtls 上下文（RAII 收尾）
struct TlsConn {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt ca;
    bool handshaked = false;

    TlsConn() {
        mbedtls_net_init(&net);
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
        mbedtls_x509_crt_init(&ca);
        net.fd = -1;
    }
    ~TlsConn() {
        if (handshaked) mbedtls_ssl_close_notify(&ssl);
        mbedtls_net_free(&net);          // 关 fd
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_entropy_free(&entropy);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_x509_crt_free(&ca);
    }
    TlsConn(const TlsConn&) = delete;
    TlsConn& operator=(const TlsConn&) = delete;
};

bool tls_connect(TlsConn& c, const std::string& host, int port, int timeout_sec, std::string& err) {
    int rc = mbedtls_ctr_drbg_seed(&c.drbg, mbedtls_entropy_func, &c.entropy,
                                   (const unsigned char*)"aka-00-tls", 10);
    if (rc != 0) {
        err = "ctr_drbg_seed 失败: " + tls_strerr(rc);
        return false;
    }

    rc = mbedtls_ssl_config_defaults(&c.conf, MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) {
        err = "ssl_config_defaults 失败: " + tls_strerr(rc);
        return false;
    }
    mbedtls_ssl_conf_rng(&c.conf, mbedtls_ctr_drbg_random, &c.drbg);

    if (g_ca_bundle.empty()) {
        err = "CA 包未设置（capp 启动时该调 csrc::set_ca_bundle）";
        return false;
    }
    rc = mbedtls_x509_crt_parse_file(&c.ca, g_ca_bundle.c_str());
    if (rc != 0) {
        err = "解析 CA 包失败 " + g_ca_bundle + ": " + tls_strerr(rc);
        return false;
    }
    mbedtls_ssl_conf_ca_chain(&c.conf, &c.ca, nullptr);
    // 证书校验是必须的 —— 不做"跳过校验"的降级，那等于没有 https
    mbedtls_ssl_conf_authmode(&c.conf, MBEDTLS_SSL_VERIFY_REQUIRED);

    rc = mbedtls_ssl_setup(&c.ssl, &c.conf);
    if (rc != 0) {
        err = "ssl_setup 失败: " + tls_strerr(rc);
        return false;
    }
    mbedtls_ssl_set_hostname(&c.ssl, host.c_str());   // SNI + 证书 CN/SAN 校验
    mbedtls_ssl_set_bio(&c.ssl, &c.net, mbedtls_net_send, mbedtls_net_recv, nullptr);

    int fd = tcp_connect_timeout(host, port, timeout_sec);
    if (fd < 0) {
        err = "连接 " + host + ":" + std::to_string(port) + " 失败";
        return false;
    }
    c.net.fd = fd;

    while ((rc = mbedtls_ssl_handshake(&c.ssl)) != 0) {
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        err = "TLS 握手失败: " + tls_strerr(rc);
        uint32_t vf = mbedtls_ssl_get_verify_result(&c.ssl);
        if (vf != 0) {
            char vb[256];
            mbedtls_x509_crt_verify_info(vb, sizeof vb, "  ", vf);
            err += "\n";
            err += vb;
        }
        return false;
    }
    c.handshaked = true;

    uint32_t flags = mbedtls_ssl_get_verify_result(&c.ssl);
    if (flags != 0) {   // VERIFY_REQUIRED 下握手一般已挡住，这里兜一道并留痕
        char vb[256];
        mbedtls_x509_crt_verify_info(vb, sizeof vb, "  ", flags);
        err = std::string("证书校验未通过: ") + vb;
        return false;
    }
    return true;
}

bool tls_write_all(TlsConn& c, const std::string& data, std::string& err) {
    size_t sent = 0;
    while (sent < data.size()) {
        int rc = mbedtls_ssl_write(&c.ssl, (const unsigned char*)data.data() + sent,
                                   data.size() - sent);
        if (rc > 0) {
            sent += (size_t)rc;
            continue;
        }
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        err = "TLS 写失败: " + tls_strerr(rc);
        return false;
    }
    return true;
}

/// tls_read 的薄封装：>0 读到字节；0 = 对端正常关闭；<0 = 出错（err 已填）
int tls_read(TlsConn& c, char* buf, size_t n, bool& eof, std::string& err) {
    eof = false;
    for (;;) {
        int rc = mbedtls_ssl_read(&c.ssl, (unsigned char*)buf, n);
        if (rc > 0) return rc;
        if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            eof = true;
            return 0;
        }
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        err = "TLS 读失败: " + tls_strerr(rc);
        return -1;
    }
}

/// 响应体的去处：内存 或 文件
struct Sink {
    std::string* mem = nullptr;
    std::ofstream* file = nullptr;
    std::function<void(int)> progress;
    long long total = -1;      // <0 表示未知
    long long written = 0;
    int last_pct = -1;

    void put(const char* p, size_t n) {
        if (file) {
            if (file->is_open()) file->write(p, (std::streamsize)n);
        } else if (mem) {
            mem->append(p, n);
        }
        written += (long long)n;
        if (progress && total > 0) {
            int pct = (int)(written * 100 / total);
            if (pct != last_pct) {
                last_pct = pct;
                progress(pct);
            }
        }
    }
};

/// 十六进制块长（chunked 用）
bool parse_hex(const std::string& s, size_t& out) {
    if (s.empty()) return false;
    size_t v = 0;
    for (char ch : s) {
        int d;
        if (ch >= '0' && ch <= '9') d = ch - '0';
        else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
        else return false;
        v = v * 16 + (size_t)d;
    }
    out = v;
    return true;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

/// 真正的一次 https 往返（不跟重定向，由调用方处理）
HttpResult https_once(const Url& u, const std::string& method, const std::string& body,
                      const std::string& content_type, const std::string& dest_path,
                      std::function<void(int)> progress_cb, int timeout_sec,
                      std::string& location_out) {
    HttpResult r;
    location_out.clear();

    TlsConn c;
    std::string err;
    if (!tls_connect(c, u.host, u.port, timeout_sec, err)) {
        r.error = err;
        return r;
    }

    std::string req = method + " " + u.path + " HTTP/1.1\r\n"
                      "Host: " + u.host + "\r\n"
                      "User-Agent: AKA-00-CPP/1.0\r\n"
                      "Accept: */*\r\n";
    if (!body.empty() || method == "POST") {
        req += "Content-Type: " + (content_type.empty() ? "application/json" : content_type) + "\r\n";
        req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    req += "Connection: close\r\n\r\n";
    req += body;

    if (!tls_write_all(c, req, err)) {
        r.error = err;
        return r;
    }

    // ── 响应头 ──
    std::string head;
    char buf[4096];
    size_t head_end = std::string::npos;
    for (;;) {
        bool eof = false;
        int n = tls_read(c, buf, sizeof buf, eof, err);
        if (n < 0) {
            r.error = err;
            return r;
        }
        if (n == 0) break;   // 对端关了
        head.append(buf, (size_t)n);
        head_end = head.find("\r\n\r\n");
        if (head_end != std::string::npos) break;
        if (head.size() > 256 * 1024) {
            r.error = "响应头过大";
            return r;
        }
    }
    if (head_end == std::string::npos) {
        r.error = head.empty() ? "对端没有返回任何数据" : "响应头不完整";
        return r;
    }

    size_t line_end = head.find("\r\n");
    std::string status_line = head.substr(0, line_end);
    int status = 0;
    {
        size_t sp = status_line.find(' ');
        if (sp != std::string::npos) status = atoi(status_line.c_str() + sp + 1);
    }
    r.status = status;

    long long content_length = -1;
    bool chunked = false;
    {
        size_t pos = line_end + 2;
        while (pos < head_end) {
            size_t e = head.find("\r\n", pos);
            if (e == std::string::npos || e > head_end) break;
            std::string line = head.substr(pos, e - pos);
            pos = e + 2;
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string k = lower(trim(line.substr(0, colon)));
            std::string v = trim(line.substr(colon + 1));
            if (k == "content-length") content_length = atoll(v.c_str());
            else if (k == "transfer-encoding" && lower(v).find("chunked") != std::string::npos) chunked = true;
            else if (k == "location") location_out = v;
        }
    }

    // 3xx：把剩下的丢掉，交给调用方处理重定向
    if (status >= 300 && status < 400) {
        r.ok = false;
        r.error = "重定向 " + std::to_string(status);
        return r;
    }

    // ── 响应体 ──
    std::ofstream fout;
    Sink sink;
    if (!dest_path.empty()) {
        fout.open(dest_path, std::ios::binary | std::ios::trunc);
        if (!fout.is_open()) {
            r.error = "打不开下载目标 " + dest_path;
            return r;
        }
        sink.file = &fout;
    } else {
        sink.mem = &r.body;
    }
    sink.progress = progress_cb;
    sink.total = chunked ? -1 : content_length;
    if (progress_cb && sink.total > 0) progress_cb(0);

    std::string buf_str = head.substr(head_end + 4);   // 响应头之后已经读到的部分

    if (chunked) {
        // 边读边解块。buf_str 作为待解析缓冲。
        std::string pending = buf_str;
        bool done = false;
        while (!done) {
            size_t nl = pending.find("\r\n");
            while (nl == std::string::npos) {
                bool eof = false;
                int n = tls_read(c, buf, sizeof buf, eof, err);
                if (n < 0) { r.error = err; return r; }
                if (n == 0) { r.error = "chunked 响应提前结束"; return r; }
                pending.append(buf, (size_t)n);
                nl = pending.find("\r\n");
            }
            size_t sz = 0;
            if (!parse_hex(trim(pending.substr(0, nl)), sz)) {
                r.error = "chunked 块长解析失败";
                return r;
            }
            pending.erase(0, nl + 2);
            if (sz == 0) {
                done = true;
                break;
            }
            while (pending.size() < sz + 2) {
                bool eof = false;
                int n = tls_read(c, buf, sizeof buf, eof, err);
                if (n < 0) { r.error = err; return r; }
                if (n == 0) { r.error = "chunked 响应提前结束"; return r; }
                pending.append(buf, (size_t)n);
            }
            sink.put(pending.data(), sz);
            pending.erase(0, sz + 2);   // 跳过块尾 CRLF
        }
    } else if (content_length >= 0) {
        if (!buf_str.empty()) {
            size_t take = (size_t)std::min<long long>((long long)buf_str.size(), content_length);
            sink.put(buf_str.data(), take);
            buf_str.erase(0, take);
        }
        while (sink.written < content_length) {
            bool eof = false;
            int n = tls_read(c, buf, sizeof buf, eof, err);
            if (n < 0) { r.error = err; return r; }
            if (n == 0) break;
            size_t want = (size_t)std::min<long long>((long long)n, content_length - sink.written);
            sink.put(buf, want);
        }
        if (sink.written < content_length) {
            r.error = "响应体不完整（" + std::to_string(sink.written) + "/" +
                      std::to_string(content_length) + "）";
            return r;
        }
    } else {
        if (!buf_str.empty()) sink.put(buf_str.data(), buf_str.size());
        for (;;) {
            bool eof = false;
            int n = tls_read(c, buf, sizeof buf, eof, err);
            if (n < 0) { r.error = err; return r; }
            if (n == 0) break;
            sink.put(buf, (size_t)n);
        }
    }

    if (fout.is_open()) {
        fout.flush();
        bool bad = fout.bad();
        fout.close();
        if (bad) {
            r.error = "写下载文件失败";
            return r;
        }
    }
    if (progress_cb) progress_cb(100);

    r.ok = status >= 200 && status < 300;
    if (!r.ok) r.error = "HTTP " + std::to_string(status);
    return r;
}

}  // namespace

HttpResult https_exchange(const Url& u, const std::string& method, const std::string& body,
                          const std::string& content_type, const std::string& dest_path,
                          std::function<void(int)> progress_cb, int timeout_sec) {
    Url cur = u;
    for (int hop = 0; hop < 4; hop++) {
        std::string location;
        HttpResult r = https_once(cur, method, body, content_type, dest_path,
                                  progress_cb, timeout_sec, location);
        bool redirect = (r.status == 301 || r.status == 302 || r.status == 303 ||
                         r.status == 307 || r.status == 308) && !location.empty();
        if (!redirect) return r;
        if (hop == 3) {
            r.error = "重定向超过 3 跳";
            return r;
        }
        Url next;
        if (!Url::parse(location, next)) {
            r.error = "重定向目标解析失败: " + location;
            return r;
        }
        if (next.scheme != "https") {
            r.error = "重定向到非 https（" + location + "），拒绝跟随";
            return r;
        }
        CAM_INFO("[https] 跟随重定向 %d → %s", r.status, location.c_str());
        cur = next;
        if (progress_cb) progress_cb(0);
    }
    HttpResult r;
    r.error = "重定向超过 3 跳";
    return r;
}

}  // namespace csrc
