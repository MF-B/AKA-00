#include "capp/demo_config.hpp"

#include "capp/context.hpp"

#include <cmath>
#include <fstream>
#include <iterator>

namespace capp {

namespace {

struct NumberField {
    const char* name;
    double TrackingConfig::* member;
    double minimum;
    double maximum;
};

constexpr NumberField kNumberFields[] = {
    {"target_size", &TrackingConfig::target_size, 1, 100000},
    {"center_margin", &TrackingConfig::center_margin, 0, 100000},
    {"align_margin", &TrackingConfig::align_margin, 0, 100000},
    {"grab_offset", &TrackingConfig::grab_offset, -100000, 100000},
    {"turn_pulse_k", &TrackingConfig::turn_pulse_k, 0.000001, 1000},
    {"too_close_k", &TrackingConfig::too_close_k, 1.000001, 100},
    {"back_pulse_k", &TrackingConfig::back_pulse_k, 0.000001, 1000},
    {"conf", &TrackingConfig::conf, 0.000001, 0.999999},
    {"iou", &TrackingConfig::iou, 0.000001, 0.999999},
};

struct IntegerField {
    const char* name;
    int TrackingConfig::* member;
    int minimum;
    int maximum;
};

constexpr IntegerField kIntegerFields[] = {
    {"speed", &TrackingConfig::speed, 1, 100},
    {"turn_speed", &TrackingConfig::turn_speed, 1, 100},
    {"turn_pulse_min", &TrackingConfig::turn_pulse_min, 1, 60000},
    {"turn_pulse_max", &TrackingConfig::turn_pulse_max, 1, 60000},
    {"fine_pulse_max", &TrackingConfig::fine_pulse_max, 1, 60000},
    {"forward_pulse_ms", &TrackingConfig::forward_pulse_ms, 1, 60000},
    {"back_pulse_min", &TrackingConfig::back_pulse_min, 1, 60000},
    {"back_pulse_max", &TrackingConfig::back_pulse_max, 1, 60000},
    {"lost_ms", &TrackingConfig::lost_ms, 1, 60000},
    {"loop_gap_ms", &TrackingConfig::loop_gap_ms, 1, 60000},
    {"grab_wait_ms", &TrackingConfig::grab_wait_ms, 0, 60000},
    {"round_gap_ms", &TrackingConfig::round_gap_ms, 1, 60000},
};

bool read_number(const csrc::Json& params, const char* name, double minimum,
                 double maximum, double& value, std::string& error) {
    const auto* field = params.get(name);
    if (!field) return true;
    if (!field->is_number() || !std::isfinite(field->as_double()) ||
        field->as_double() < minimum || field->as_double() > maximum) {
        error = std::string(name) + " 必须是 " + csrc::Json(minimum).dump() + "~" +
                csrc::Json(maximum).dump() + " 之间的数字";
        return false;
    }
    value = field->as_double();
    return true;
}

}  // namespace

bool apply_tracking_params(const csrc::Json& params, TrackingConfig& config, std::string& error) {
    if (!params.is_object()) {
        error = "params 必须是 JSON 对象";
        return false;
    }
    TrackingConfig next = config;
    for (const auto& field : kNumberFields) {
        if (!read_number(params, field.name, field.minimum, field.maximum,
                          next.*(field.member), error)) return false;
    }
    for (const auto& field : kIntegerFields) {
        double value = next.*(field.member);
        if (!read_number(params, field.name, field.minimum, field.maximum, value, error)) return false;
        if (std::floor(value) != value) {
            error = std::string(field.name) + " 必须是整数";
            return false;
        }
        next.*(field.member) = static_cast<int>(value);
    }
    if (const auto* arrival = params.get("on_arrival")) {
        if (!arrival->is_string() || (arrival->as_string() != "stop" && arrival->as_string() != "grab")) {
            error = "on_arrival 必须是 stop 或 grab";
            return false;
        }
        next.on_arrival = arrival->as_string() == "grab" ? ArrivalAction::Grab : ArrivalAction::Stop;
    }
    if (const auto* mode = params.get("mode")) {
        if (!mode->is_string() || (mode->as_string() != "once" && mode->as_string() != "loop")) {
            error = "mode 必须是 once 或 loop";
            return false;
        }
    }
    if (next.turn_pulse_min > next.turn_pulse_max || next.turn_pulse_min > next.fine_pulse_max) {
        error = "turn_pulse_min 不能大于 turn_pulse_max 或 fine_pulse_max";
        return false;
    }
    if (next.back_pulse_min > next.back_pulse_max) {
        error = "back_pulse_min 不能大于 back_pulse_max";
        return false;
    }
    config = next;
    return true;
}

csrc::Json tracking_params_json(const TrackingConfig& config) {
    csrc::Json json(csrc::Json::Type::Object);
    for (const auto& field : kNumberFields) json[field.name] = config.*(field.member);
    for (const auto& field : kIntegerFields) json[field.name] = config.*(field.member);
    json["on_arrival"] = config.on_arrival == ArrivalAction::Grab ? "grab" : "stop";
    return json;
}

csrc::Json demo_params_only(const csrc::Json& json) {
    csrc::Json params(csrc::Json::Type::Object);
    for (const auto& field : kNumberFields) {
        if (const auto* value = json.get(field.name)) params[field.name] = *value;
    }
    for (const auto& field : kIntegerFields) {
        if (const auto* value = json.get(field.name)) params[field.name] = *value;
    }
    for (const char* name : {"on_arrival", "mode"}) {
        if (const auto* value = json.get(name)) params[name] = *value;
    }
    return params;
}

bool load_demo_action(AppContext& ctx, const std::string& action,
                      DemoActionConfig& config, std::string& error) {
    if (!valid_model_name(action)) {
        error = "动作名非法（只允许字母数字与 _ - .）：" + action;
        return false;
    }
    const std::string path = action_config_path(ctx, action);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "动作配置打不开：demo/" + action + ".json";
        return false;
    }
    if (file.tellg() <= 0 || file.tellg() > 64 * 1024) {
        error = "动作配置必须是 1~65536 字节的 JSON 文件：" + path;
        return false;
    }
    file.seekg(0);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    csrc::Json json;
    if (!csrc::Json::parse(text, json, &error) || !json.is_object()) {
        error = "动作配置不是有效的 JSON 对象：" + path + " " + error;
        return false;
    }
    if (json.gets("type") != "track") {
        error = "动作配置 type 必须是 track：" + path;
        return false;
    }
    const auto* arrival = json.get("on_arrival");
    const auto* defaults = json.get("params");
    if (!arrival || !defaults || !defaults->is_object()) {
        error = "动作配置必须包含 on_arrival 和 params 对象：" + path;
        return false;
    }
    csrc::Json params = *defaults;
    params["on_arrival"] = *arrival;
    DemoActionConfig next;
    next.name = json.gets("name", action);
    if (!apply_tracking_params(params, next.tracking, error)) {
        error = path + ": " + error;
        return false;
    }
    config = next;
    return true;
}

bool resolve_demo_config(AppContext& ctx, const std::string& action, const csrc::Json& params,
                         TrackingConfig& config, std::string& error) {
    DemoActionConfig defaults;
    if (!load_demo_action(ctx, action, defaults, error)) return false;
    config = defaults.tracking;
    return apply_tracking_params(params, config, error);
}

}  // namespace capp
