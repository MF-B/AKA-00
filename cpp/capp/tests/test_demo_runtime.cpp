// Exercise the real Demo routes/runner with deterministic device boundaries.
#include "capp/context.hpp"
#include "capp/demo_config.hpp"
#include "capp/http_server.hpp"
#include "routes_internal.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <vector>

using namespace capp;
using csrc::Json;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; \
} } while (0)

namespace {

struct MotorEvent { int left; int right; bool brake; };

class TestMotor : public csrc::MotorPair {
public:
    void set_speed(int left, int right) override { record({left, right, false}); }
    void get_speeds(int& left, int& right) override { left = right = 0; }
    void brake() override { record({0, 0, true}); }
    void sleep() override { record({0, 0, false}); }
    void close() override {}
    bool reinitialize() override { return true; }
    void get_encoder(int& left, int& right) override { left = right = 0; }

    std::vector<MotorEvent> snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        return events;
    }
    bool wait_moves(size_t count) {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(2), [&] {
            size_t moves = 0;
            for (const auto& e : events) if (e.left || e.right) ++moves;
            return moves >= count;
        });
    }

private:
    void record(MotorEvent event) {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back(event);
        changed.notify_all();
    }
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<MotorEvent> events;
};

std::mutex scene_mu;
std::condition_variable scene_changed;
std::vector<csrc::Detection> scene_boxes;
std::string scene_error;
bool block_detection = false;
bool detection_entered = false;
bool detection_released = false;
std::atomic<bool> connected{true};
std::atomic<int> grabs{0};
ArmResult arm_result = ArmResult::Accepted;
std::string arm_execution_error;

csrc::Detection box(float width, float offset, float height = 100) {
    csrc::Detection detection;
    detection.box.x1 = 320 + offset - width / 2;
    detection.box.x2 = detection.box.x1 + width;
    detection.box.y1 = 0;
    detection.box.y2 = height;
    return detection;
}

void scene(std::vector<csrc::Detection> boxes, std::string error = "", bool block = false) {
    std::lock_guard<std::mutex> lock(scene_mu);
    scene_boxes = std::move(boxes);
    scene_error = std::move(error);
    block_detection = block;
    detection_entered = false;
    detection_released = false;
    connected = true;
    arm_result = ArmResult::Accepted;
    arm_execution_error.clear();
    grabs = 0;
}

Json object(const std::string& text) {
    Json json;
    CHECK(Json::parse(text, json));
    return json;
}

Json read_json(const fs::path& path) {
    std::ifstream file(path);
    return object(std::string(std::istreambuf_iterator<char>(file), {}));
}

void write_json(const fs::path& path, const Json& json) {
    std::ofstream(path) << json.dump(true);
}

struct Fixture {
    AppContext ctx;
    TestMotor* motor;
    fs::path root;

    explicit Fixture(const fs::path& board) {
        char directory[] = "/tmp/aka-demo-tests-XXXXXX";
        char* created = mkdtemp(directory);
        if (!created) std::abort();
        root = created;
        fs::create_directories(root / "demo/models");
        fs::copy(board / "demo/configs", root / "demo/configs", fs::copy_options::recursive);
        for (const char* action : {"grab.json", "approach.json"})
            fs::copy_file(board / "demo" / action, root / "demo" / action);
        for (const char* model : {"tennis.cvimodel", "orange.cvimodel"})
            std::ofstream(root / "demo/models" / model) << "test model";
        ctx.app_dir = root.string();
        motor = new TestMotor;
        ctx.motor_pair.reset(motor);
    }
    ~Fixture() {
        {
            std::lock_guard<std::mutex> lock(scene_mu);
            detection_released = true;
            scene_changed.notify_all();
        }
        demo_stop(ctx);
        ctx.shutdown = true;
        join_demo_worker(ctx);
        fs::remove_all(root);
    }
};

struct Response { int status; Json json; };

Response request(AppContext& ctx, const std::string& method, const std::string& path,
                 const Json& body = Json(), const std::string& query = "") {
    Router router;
    routes::register_demo_routes(router, ctx);
    HttpRequest req;
    req.method = method;
    req.path = path;
    req.query = query;
    req.body = body.dump();
    HttpResponse response;
    ClientConn connection;
    CHECK(router.dispatch(method, path, req, response, connection, ctx));
    return {response.status, object(response.body)};
}

void wait_done(AppContext& ctx) {
    CHECK(wait_demo_done(ctx, 2));
    join_demo_worker(ctx);
}

void test_configuration(const fs::path& board) {
    Fixture f(board);
    auto list = request(f.ctx, "GET", "/api/demo/list");
    CHECK(list.status == 200);
    CHECK(list.json.get("actions")->size() == 2);
    CHECK(list.json.get("demos")->size() == 3);
    for (const auto& card : list.json.get("demos")->array()) CHECK(card.getb("ready"));
    auto legacy = request(f.ctx, "GET", "/api/demo/config", Json(), "name=tennis-approach");
    CHECK(legacy.json.geti("target_size") == 280);
    CHECK(legacy.json.geti("speed") == 20);
    CHECK(legacy.json.geti("grab_offset") == 50);

    Json action = read_json(f.root / "demo/approach.json");
    action["params"]["grab_offset"] = 75;
    write_json(f.root / "demo/approach.json", action);
    auto reloaded = request(f.ctx, "GET", "/api/demo/config", Json(), "name=tennis-approach");
    CHECK(reloaded.json.geti("grab_offset") == 75);
    auto saved = request(f.ctx, "POST", "/api/demo/config", object(
        R"({"name":"tuned","action":"approach","model":"tennis","speed":35,"grab_offset":90,"forward_pulse_ms":850})"));
    CHECK(saved.status == 200 && saved.json.getb("ok"));
    CHECK(saved.json.geti("grab_offset") == 90);
    auto basic_edit = request(f.ctx, "POST", "/api/demo/config", object(
        R"({"name":"tuned","action":"approach","model":"tennis","speed":40,"target_size":320,"turn_speed":25,"mode":"once"})"));
    CHECK(basic_edit.json.geti("grab_offset") == 90);
    CHECK(basic_edit.json.geti("forward_pulse_ms") == 850);
    CHECK(read_json(f.root / "demo/configs/tuned.json").geti("grab_offset") == 90);

    auto invalid = request(f.ctx, "POST", "/api/demo/config", object(
        R"({"name":"tuned","action":"approach","model":"tennis","turn_pulse_min":1000})"));
    CHECK(invalid.status == 400);
    CHECK(read_json(f.root / "demo/configs/tuned.json").geti("speed") == 40);
    auto bad_run = request(f.ctx, "POST", "/api/demo/init", object(
        R"({"action":"approach","model":"tennis","speed":"fast","wait":false})"));
    CHECK(bad_run.status == 400);
    CHECK(f.motor->snapshot().empty());
    auto missing = request(f.ctx, "POST", "/api/demo/init", object(
        R"({"action":"unknown","model":"tennis","wait":false})"));
    CHECK(missing.status == 400);
    CHECK(!demo_run(f.ctx, "../approach", object(R"({"model":"tennis"})")).getb("ok"));
    CHECK(!demo_run(f.ctx, "approach", object(R"({"model":"../tennis"})")).getb("ok"));

    TrackingConfig config;
    std::string error;
    CHECK(!apply_tracking_params(object(R"({"speed":1.5})"), config, error));
    CHECK(!apply_tracking_params(object(R"({"conf":1})"), config, error));
    CHECK(!apply_tracking_params(object(R"({"mode":"forever"})"), config, error));
    Json huge;
    huge["target_size"] = std::numeric_limits<double>::infinity();
    CHECK(!apply_tracking_params(huge, config, error));
    CHECK(config.speed == 25 && config.target_size == 300);
}

void test_arrival(const fs::path& board) {
    Fixture f(board);
    // Selecting the largest target must ignore the smaller, distant box.
    scene({box(100, -150, 10), box(300, 50)});
    auto done = request(f.ctx, "POST", "/api/demo/init", object(R"({"name":"tennis-approach"})"));
    CHECK(done.status == 200 && done.json.getb("completed"));
    CHECK(demo_status(f.ctx).gets("state") == "done");
    CHECK(grabs == 0);
    for (const auto& event : f.motor->snapshot()) CHECK(event.left == 0 && event.right == 0);

    auto grab = request(f.ctx, "POST", "/api/demo/run", object(
        R"({"script":"grab","params":{"model":"tennis","grab_wait_ms":0}})"));
    CHECK(grab.status == 200 && grab.json.getb("completed"));
    CHECK(grabs == 1);
    arm_result = ArmResult::Busy;
    auto busy = request(f.ctx, "POST", "/api/demo/run", object(
        R"({"action":"grab","params":{"model":"tennis","grab_wait_ms":0}})"));
    CHECK(!busy.json.getb("completed"));
    CHECK(demo_status(f.ctx).gets("state") == "failed");
    CHECK(grabs == 1);
    arm_result = ArmResult::Accepted;
    arm_execution_error = "test grab failure";
    auto failed_arm = request(f.ctx, "POST", "/api/demo/run", object(
        R"({"action":"grab","params":{"model":"tennis","grab_wait_ms":0}})"));
    CHECK(!failed_arm.json.getb("completed"));
    CHECK(demo_status(f.ctx).gets("message").find("test grab failure") != std::string::npos);
}

void test_stop_and_keepalive(const fs::path& board) {
    Fixture f(board);
    scene({box(100, 0)});
    Json params = object(R"({"model":"tennis","speed":100})");
    CHECK(demo_run(f.ctx, "approach", params).getb("ok"));
    CHECK(f.motor->wait_moves(2));  // Initial drive and a keepalive while waiting.
    for (const auto& event : f.motor->snapshot()) CHECK(event.left <= 70 && event.right <= 70);
    CHECK(demo_run(f.ctx, "approach", params).getb("busy"));
    auto busy = request(f.ctx, "POST", "/api/demo/run", object(
        R"({"action":"approach","params":{"model":"tennis"},"wait":false})"));
    CHECK(busy.status == 409 && busy.json.getb("busy"));
    CHECK(demo_stop(f.ctx).gets("state") == "aborted");
    const size_t after_stop = f.motor->snapshot().size();
    wait_done(f.ctx);
    CHECK(demo_status(f.ctx).gets("state") == "aborted");
    CHECK(f.motor->snapshot().size() == after_stop);
    // A previous stop flag must not abort the next invocation.
    scene({box(300, 50)});
    CHECK(demo_run(f.ctx, "approach", params).getb("ok"));
    wait_done(f.ctx);
    CHECK(demo_status(f.ctx).gets("state") == "done");
}

void test_large_observation(const fs::path& board) {
    Fixture f(board);
    scene({box(1e30f, 0)});
    CHECK(demo_run(f.ctx, "approach", object(R"({"model":"tennis"})")).getb("ok"));
    CHECK(f.motor->wait_moves(1));
    // Telemetry must also handle finite coordinates outside the integer range.
    CHECK(demo_status(f.ctx).get("notes")->gets("box_w") != "");
    CHECK(f.motor->snapshot().front().left < 0);
    demo_stop(f.ctx);
    wait_done(f.ctx);
}

void test_human_takeover(const fs::path& board, bool during_detection) {
    Fixture f(board);
    scene({box(100, 0)}, "", during_detection);
    CHECK(demo_run(f.ctx, "approach", object(R"({"model":"tennis"})")).getb("ok"));
    if (during_detection) {
        std::unique_lock<std::mutex> lock(scene_mu);
        CHECK(scene_changed.wait_for(lock, std::chrono::seconds(2), [] { return detection_entered; }));
    } else CHECK(f.motor->wait_moves(1));
    {
        std::lock_guard<std::mutex> lock(f.ctx.timer_mu);
        ++f.ctx.motion_seq;
        f.motor->set_speed(37, 38);
    }
    const size_t after_human = f.motor->snapshot().size();
    if (during_detection) {
        // Stop must also leave the new human command untouched.
        demo_stop(f.ctx);
        std::lock_guard<std::mutex> lock(scene_mu);
        detection_released = true;
        scene_changed.notify_all();
    }
    wait_done(f.ctx);
    CHECK(demo_status(f.ctx).gets("state") == "aborted");
    CHECK(f.motor->snapshot().size() == after_human);
}

void test_failure_and_loop(const fs::path& board) {
    Fixture f(board);
    scene({}, "no frame");
    CHECK(demo_run(f.ctx, "approach", object(R"({"model":"tennis","lost_ms":40,"loop_gap_ms":1})")).getb("ok"));
    wait_done(f.ctx);
    CHECK(demo_status(f.ctx).gets("state") == "done");
    CHECK(demo_status(f.ctx).gets("message") == "目标丢失");

    scene({}, "broken model");
    CHECK(demo_run(f.ctx, "approach", object(R"({"model":"tennis","mode":"loop"})")).getb("ok"));
    wait_done(f.ctx);
    CHECK(demo_status(f.ctx).gets("state") == "failed");
    CHECK(demo_status(f.ctx).geti("round") == 1);

    scene({box(100, 0)});
    CHECK(demo_run(f.ctx, "approach", object(R"({"model":"tennis"})")).getb("ok"));
    CHECK(f.motor->wait_moves(1));
    connected = false;
    wait_done(f.ctx);
    CHECK(demo_status(f.ctx).gets("state") == "failed");
    CHECK(f.motor->snapshot().back().brake);

    scene({box(300, 50)});
    auto loop = request(f.ctx, "POST", "/api/demo/init", object(
        R"({"action":"approach","model":"tennis","mode":"loop","round_gap_ms":1})"));
    CHECK(loop.status == 200 && loop.json.gets("status") == "started");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (demo_status(f.ctx).geti("round") < 2 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(demo_status(f.ctx).geti("round") >= 2);
    demo_stop(f.ctx);
    wait_done(f.ctx);
    CHECK(demo_status(f.ctx).gets("state") == "aborted");
}

}  // namespace

// Only device boundaries are replaced; routes, JSON handling and worker code are production code.
namespace csrc {
Camera& Camera::get_instance() { static Camera camera; return camera; }
Camera::~Camera() = default;
ScreenDisplay::~ScreenDisplay() = default;
YoloDetector::~YoloDetector() = default;
}

namespace capp {
bool cancel_pending_stop_if_owned(AppContext& ctx, int64_t expected_seq) {
    std::lock_guard<std::mutex> lock(ctx.timer_mu);
    return ctx.motion_seq == expected_seq;
}
Json motor_status_json(AppContext&) { Json result; result["connected"] = connected.load(); return result; }
ArmResult apply_arm_action(AppContext& ctx, const std::string&) {
    if (arm_result == ArmResult::Accepted) {
        ++grabs;
        std::lock_guard<std::mutex> lock(ctx.arm_mu);
        ctx.arm_error = arm_execution_error;
    }
    return arm_result;
}
csrc::DecodeOptions decode_options(double conf, double iou) {
    csrc::DecodeOptions options;
    options.conf = static_cast<float>(conf);
    options.iou = static_cast<float>(iou);
    return options;
}
bool detect_boxes(AppContext&, const std::string&, const csrc::DecodeOptions&,
                  std::vector<csrc::Detection>& out, int& frame_width, std::string& error) {
    std::unique_lock<std::mutex> lock(scene_mu);
    detection_entered = true;
    scene_changed.notify_all();
    if (block_detection) scene_changed.wait(lock, [] { return detection_released; });
    out = scene_boxes;
    frame_width = 640;
    error = scene_error;
    return error.empty();
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    test_configuration(argv[1]);
    test_arrival(argv[1]);
    test_stop_and_keepalive(argv[1]);
    test_large_observation(argv[1]);
    test_human_takeover(argv[1], false);
    test_human_takeover(argv[1], true);
    test_failure_and_loop(argv[1]);
    std::printf("Demo routes/runtime: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
