#include "capp/context.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

int main() {
    char directory[] = "/tmp/aka-upstream-lua-XXXXXX";
    const char* root = mkdtemp(directory);
    assert(root);
    std::filesystem::create_directory(std::string(root) + "/demo");
    capp::AppContext ctx;
    ctx.app_dir = root;
    auto run = [&](const std::string& source, const std::string& expected,
                   const std::string& message) {
        std::ofstream(std::string(root) + "/demo/validation.lua") << source;
        csrc::Json params;
        params["n"] = 10;
        assert(capp::script_run(ctx, "validation", params).getb("ok"));
        csrc::Json status;
        for (int i = 0; i < 2000; ++i) {
            status = capp::script_status(ctx);
            if (status.gets("state") != "running") break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(status.gets("state") == expected);
        assert(status.gets("message").find(message) != std::string::npos);
        assert(pthread_join(ctx.script_tid, nullptr) == 0);
        ctx.script_tid_valid = false;
    };
    run("local sum=0; for i=1,100000 do sum=sum+i end; return tostring(sum)",
        "done", "5000050000");
    run("local p=params(); local n=0; for i=1,p.n do n=n+i end; return 'lua-ok:'..n",
        "done", "lua-ok:55");
    run("assert(os==nil and io==nil and package==nil and require==nil); return 'sandbox-ok'",
        "done", "sandbox-ok");
    run("this is not valid Lua", "failed", "error:");
    std::filesystem::remove_all(root);
    std::puts("Lua: instruction hook, parameters, sandbox and errors: PASSED");
}
