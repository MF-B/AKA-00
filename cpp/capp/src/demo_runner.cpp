// Platform adapter for the portable tracking state machine.
#include "capp/context.hpp"
#include "capp/demo_config.hpp"
#include "capp/demo_machine.hpp"
#include "csrc/log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <pthread.h>
#include <thread>

namespace capp {

namespace {

constexpr int kDemoMaxSpeed = 70;
constexpr int64_t kDemoMaxOnceMs = 300000;
constexpr int kDemoTickMs = 20;
constexpr int kDemoKeepaliveMs = 80;
constexpr size_t kDemoStackBytes = 1024 * 1024;

struct RunArgs {
    AppContext* ctx;
    std::string name;
    std::string model;
    TrackingConfig config;
    bool repeat;
    int64_t own_seq = 0;
};

struct RunContext {
    RunArgs& args;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    int left = 0;
    int right = 0;
    int64_t last_keepalive = 0;
    bool timer_cancelled = false;
    std::string reason;

    explicit RunContext(RunArgs& args) : args(args) {}

    int64_t elapsed_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start).count();
    }

    // Caller holds timer_mu to serialize the check with new motor commands.
    bool owns_motion_locked() {
        auto& ctx = *args.ctx;
        if (ctx.demo_abort) reason = "aborted: 收到停止请求";
        else if (ctx.shutdown) reason = "aborted: 服务退出";
        else if (ctx.motion_seq != args.own_seq) reason = "superseded: 被新的运动指令取代（人接管）";
        return reason.empty();
    }

    bool interrupted() {
        if (!reason.empty()) return true;
        auto& ctx = *args.ctx;
        {
            std::lock_guard<std::mutex> lock(ctx.timer_mu);
            if (!owns_motion_locked()) return true;
        }
        if (!motor_status_json(ctx).getb("connected")) reason = "motor: 底盘掉线";
        else if (!args.repeat && elapsed_ms() >= kDemoMaxOnceMs)
            reason = "timeout: 到最大执行时间（5 分钟）";
        return !reason.empty();
    }

    bool drive(int l, int r, bool brake = false) {
        auto& ctx = *args.ctx;
        if (!timer_cancelled) {
            if (!cancel_pending_stop_if_owned(ctx, args.own_seq)) {
                std::lock_guard<std::mutex> lock(ctx.timer_mu);
                owns_motion_locked();
                return false;
            }
            timer_cancelled = true;
        }
        std::lock_guard<std::mutex> lock(ctx.timer_mu);
        if (!owns_motion_locked()) return false;
        args.own_seq = ++ctx.motion_seq;
        ctx.demo_motion_seq = args.own_seq;
        left = std::clamp(l, -kDemoMaxSpeed, kDemoMaxSpeed);
        right = std::clamp(r, -kDemoMaxSpeed, kDemoMaxSpeed);
        if (brake) ctx.motor_pair->brake();
        else ctx.motor_pair->set_speed(left, right);
        ctx.collector.set_target_speed(left, right);
        last_keepalive = elapsed_ms();
        return true;
    }

    bool keepalive() {
        if ((left == 0 && right == 0) || elapsed_ms() - last_keepalive < kDemoKeepaliveMs) return true;
        auto& ctx = *args.ctx;
        std::lock_guard<std::mutex> lock(ctx.timer_mu);
        if (!owns_motion_locked()) return false;
        ctx.motor_pair->set_speed(left, right);
        last_keepalive = elapsed_ms();
        return true;
    }

    void finish(const std::string& message) {
        auto& ctx = *args.ctx;
        try {
            std::lock_guard<std::mutex> lock(ctx.timer_mu);
            // Once a human command changes the generation, cleanup must leave it alone.
            if ((left != 0 || right != 0) && ctx.motion_seq == args.own_seq) {
                ctx.motor_pair->brake();
                ctx.collector.set_target_speed(0, 0);
            }
        } catch (const std::exception& e) {
            if (reason.empty()) reason = std::string("error: 停车失败：") + e.what();
        } catch (...) {
            if (reason.empty()) reason = "error: 停车失败";
        }
        const std::string state = reason.empty() ? "done"
            : (reason.rfind("error:", 0) == 0 || reason.rfind("failed:", 0) == 0 ||
               reason.rfind("motor:", 0) == 0) ? "failed" : "aborted";
        const std::string result = reason.empty() ? message : reason;
        CAM_INFO("[demo] %s 结束（%s）：%s", args.name.c_str(), state.c_str(), result.c_str());
        std::lock_guard<std::mutex> lock(ctx.demo_mu);
        ctx.demo_state = state;
        ctx.demo_phase = state;
        ctx.demo_message = result;
        ctx.demo_running = false;
    }
};

bool arm_finished(AppContext& ctx, std::string& error) {
    std::lock_guard<std::mutex> lock(ctx.arm_mu);
    if (ctx.arm_busy) return false;
    error = ctx.arm_error;
    return true;
}

void publish(AppContext& ctx, const DemoMachine& machine, DemoCommand command) {
    std::lock_guard<std::mutex> lock(ctx.demo_mu);
    ctx.demo_phase = demo_phase_name(machine.phase());
    if (command != DemoCommand::None) {
        ++ctx.demo_calls;
        ctx.demo_action = demo_command_name(command);
    }
}

TargetObservation observe(const std::vector<csrc::Detection>& boxes, int frame_width) {
    TargetObservation target;
    if (frame_width <= 0) return target;
    double largest_area = 0;
    for (const auto& detection : boxes) {
        const auto& b = detection.box;
        const double width = static_cast<double>(b.x2) - b.x1;
        const double height = static_cast<double>(b.y2) - b.y1;
        const double offset = (static_cast<double>(b.x1) + b.x2) / 2 - frame_width / 2.0;
        const double area = width * height;
        if (width <= 0 || height <= 0 || !std::isfinite(area) || !std::isfinite(offset)) continue;
        if (area > largest_area) {
            largest_area = area;
            target = {true, width, offset};
        }
    }
    return target;
}

std::string pixel_string(double value) {
    // Observations can be finite yet exceed integer range; format without an integer cast.
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.10g", std::floor(value));
    return buffer;
}

void publish_target(AppContext& ctx, const DemoMachine& machine,
                    const TargetObservation& target, int64_t now_ms) {
    std::lock_guard<std::mutex> lock(ctx.demo_mu);
    ctx.demo_notes.clear();
    if (target.present) {
        ctx.demo_notes.emplace_back("box_w", pixel_string(target.width));
        ctx.demo_notes.emplace_back("offset", pixel_string(target.offset));
    } else {
        ctx.demo_notes.emplace_back("lost_ms", std::to_string(machine.lost_for(now_ms)));
    }
}

std::string result_message(const DemoMachine& machine) {
    if (machine.result() == DemoResult::Lost) return "目标丢失";
    if (machine.result() == DemoResult::Grabbed) return "已抓取（是否夹到请看实物：夹爪没有反馈）";
    return "已到位（未抓取）：框宽 " +
           pixel_string(machine.target().width) + "px，偏移 " + pixel_string(machine.target().offset);
}

void run_worker(RunArgs& args) {
    auto& ctx = *args.ctx;
    RunContext run(args);
    std::string message;
    try {
        int round = 0;
        while (!run.interrupted()) {
            {
                std::lock_guard<std::mutex> lock(ctx.demo_mu);
                ctx.demo_round = ++round;
            }
            CAM_INFO("[demo] %s 第 %d 轮，模型=%s，目标=%.0fpx，速度=%d/%d%%",
                     args.name.c_str(), round, args.model.c_str(), args.config.target_size,
                     std::min(args.config.speed, kDemoMaxSpeed),
                     std::min(args.config.turn_speed, kDemoMaxSpeed));
            DemoMachine machine(args.config, run.elapsed_ms());
            while (machine.result() == DemoResult::None && !run.interrupted()) {
                std::string arm_error;
                const bool arm_done = machine.phase() == DemoPhase::Grabbing && arm_finished(ctx, arm_error);
                if (!arm_error.empty()) {
                    run.reason = "failed: 抓取执行失败：" + arm_error;
                    break;
                }
                DemoCommand command = machine.tick(run.elapsed_ms(), nullptr, arm_done);
                if (command == DemoCommand::Detect) {
                    publish(ctx, machine, command);
                    std::vector<csrc::Detection> boxes;
                    int frame_width = 0;
                    std::string error;
                    const bool ok = detect_boxes(ctx, args.model,
                        decode_options(args.config.conf, args.config.iou), boxes, frame_width, error);
                    // Inference can block; recheck ownership before using its result or moving.
                    if (run.interrupted()) break;
                    if (!ok && error != "no frame") {
                        run.reason = "failed: 推理失败：" + error;
                        break;
                    }
                    const auto target = ok ? observe(boxes, frame_width) : TargetObservation{};
                    const int64_t now = run.elapsed_ms();
                    command = machine.tick(now, &target);
                    publish_target(ctx, machine, target, now);
                }
                bool applied = true;
                switch (command) {
                    case DemoCommand::Forward: applied = run.drive(args.config.speed, args.config.speed); break;
                    case DemoCommand::Back: applied = run.drive(-args.config.speed, -args.config.speed); break;
                    case DemoCommand::TurnLeft: applied = run.drive(-args.config.turn_speed, args.config.turn_speed); break;
                    case DemoCommand::TurnRight: applied = run.drive(args.config.turn_speed, -args.config.turn_speed); break;
                    case DemoCommand::Standby: applied = run.drive(0, 0); break;
                    case DemoCommand::Brake: applied = run.drive(0, 0, true); break;
                    case DemoCommand::Grab: {
                        std::lock_guard<std::mutex> lock(ctx.timer_mu);
                        applied = run.owns_motion_locked();
                        if (applied) {
                            const auto result = apply_arm_action(ctx, "grab");
                            if (result != ArmResult::Accepted) {
                                run.reason = result == ArmResult::Busy
                                    ? "failed: 夹爪正忙（本次没有抓取）" : "failed: 抓取动作不可用";
                                applied = false;
                            }
                        }
                        break;
                    }
                    case DemoCommand::None:
                    case DemoCommand::Detect:
                        break;
                }
                if (!applied) break;
                publish(ctx, machine, command);
                if (!run.keepalive()) break;
                if (machine.result() == DemoResult::None)
                    std::this_thread::sleep_for(std::chrono::milliseconds(kDemoTickMs));
            }
            if (!run.reason.empty() || run.interrupted()) break;
            message = result_message(machine);
            if (!args.repeat) break;
            const int64_t next_round = run.elapsed_ms() + args.config.round_gap_ms;
            while (run.elapsed_ms() < next_round && !run.interrupted())
                std::this_thread::sleep_for(std::chrono::milliseconds(kDemoTickMs));
        }
    } catch (const std::exception& e) {
        run.reason = std::string("error: ") + e.what();
    } catch (...) {
        run.reason = "error: Demo 执行异常";
    }
    run.finish(message);
}

void* worker_entry(void* pointer) {
    std::unique_ptr<RunArgs> args(static_cast<RunArgs*>(pointer));
    run_worker(*args);
    return nullptr;
}

}  // namespace

csrc::Json demo_run(AppContext& ctx, const std::string& name, const csrc::Json& params) {
    csrc::Json result;
    result["ok"] = false;
    TrackingConfig config;
    std::string error;
    if (!resolve_demo_config(ctx, name, params, config, error)) {
        result["error"] = error;
        return result;
    }
    const std::string model = params.gets("model");
    if (!valid_model_name(model)) {
        result["error"] = "model 必填且必须是合法模型名";
        return result;
    }
    const bool repeat = params.gets("mode", "once") == "loop";
    std::unique_ptr<RunArgs> args(new RunArgs{&ctx, name, model, config, repeat});
    std::lock_guard<std::mutex> lock(ctx.demo_mu);
    if (ctx.demo_running || ctx.shutdown) {
        result["error"] = ctx.shutdown ? "服务正在退出" : "已有 Demo 在运行（先 POST /api/demo/stop）";
        result["busy"] = ctx.demo_running;
        return result;
    }
    if (ctx.demo_tid_valid) {
        pthread_join(ctx.demo_tid, nullptr);
        ctx.demo_tid_valid = false;
    }
    ctx.demo_running = true;
    ctx.demo_abort = false;
    ctx.demo_state = "running";
    ctx.demo_phase = "detect";
    ctx.demo_message = "启动";
    ctx.demo_name = name;
    ctx.demo_model = model;
    ctx.demo_card = params.gets("card");
    ctx.demo_repeat = repeat;
    ctx.demo_round = 0;
    ctx.demo_calls = 0;
    ctx.demo_action.clear();
    ctx.demo_notes.clear();
    {
        std::lock_guard<std::mutex> motion_lock(ctx.timer_mu);
        args->own_seq = ctx.motion_seq;
        ctx.demo_motion_seq = args->own_seq;
    }
    pthread_attr_t attributes;
    int rc = pthread_attr_init(&attributes);
    if (rc == 0) {
        rc = pthread_attr_setstacksize(&attributes, kDemoStackBytes);
        if (rc == 0) rc = pthread_create(&ctx.demo_tid, &attributes, worker_entry, args.get());
        pthread_attr_destroy(&attributes);
    }
    if (rc != 0) {
        result["error"] = "创建 Demo 工作线程失败（rc=" + std::to_string(rc) + "）";
        ctx.demo_running = false;
        ctx.demo_state = "failed";
        ctx.demo_phase = "failed";
        ctx.demo_message = result.gets("error");
        return result;
    }
    args.release();
    ctx.demo_tid_valid = true;
    result["ok"] = true;
    result["state"] = "running";
    result["script"] = name;  // Compatibility field for existing clients.
    result["mode"] = repeat ? "loop" : "once";
    return result;
}

csrc::Json demo_stop(AppContext& ctx) {
    csrc::Json result;
    std::lock_guard<std::mutex> lock(ctx.demo_mu);
    result["ok"] = true;
    if (!ctx.demo_running) {
        result["state"] = "idle";
        result["message"] = "没有正在运行的 Demo";
        return result;
    }
    ctx.demo_abort = true;
    {
        std::lock_guard<std::mutex> motion_lock(ctx.timer_mu);
        if (ctx.motion_seq == ctx.demo_motion_seq) {
            ++ctx.motion_seq;  // Invalidate any in-flight decision before releasing the lock.
            ctx.motor_pair->brake();
            ctx.collector.set_target_speed(0, 0);
        }
    }
    result["state"] = "aborted";
    return result;
}

csrc::Json demo_status(AppContext& ctx) {
    std::lock_guard<std::mutex> lock(ctx.demo_mu);
    csrc::Json result;
    result["state"] = ctx.demo_state;
    result["script"] = ctx.demo_name;
    result["mode"] = ctx.demo_repeat ? "loop" : "once";
    result["round"] = ctx.demo_round;
    result["model"] = ctx.demo_model;
    result["card"] = ctx.demo_card;
    result["message"] = ctx.demo_message;
    result["calls"] = csrc::Json(static_cast<int64_t>(ctx.demo_calls));
    result["action"] = ctx.demo_action;
    result["phase"] = ctx.demo_phase;
    csrc::Json notes(csrc::Json::Type::Object);
    for (const auto& note : ctx.demo_notes) notes[note.first] = note.second;
    result["notes"] = notes;
    return result;
}

void join_demo_worker(AppContext& ctx) {
    pthread_t thread;
    {
        std::lock_guard<std::mutex> lock(ctx.demo_mu);
        if (!ctx.demo_tid_valid) return;
        thread = ctx.demo_tid;
        ctx.demo_tid_valid = false;
    }
    pthread_join(thread, nullptr);
}

}  // namespace capp
