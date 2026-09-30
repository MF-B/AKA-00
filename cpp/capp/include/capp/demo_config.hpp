#pragma once

#include <string>

#include "capp/demo_machine.hpp"
#include "csrc/json.hpp"

namespace capp {

struct AppContext;

struct DemoActionConfig {
    std::string name;
    TrackingConfig tracking;
};

// Action defaults < card overrides < request overrides. Invalid values fail before motion.
bool apply_tracking_params(const csrc::Json& params, TrackingConfig& config, std::string& error);
csrc::Json tracking_params_json(const TrackingConfig& config);
csrc::Json demo_params_only(const csrc::Json& json);

bool load_demo_action(AppContext& ctx, const std::string& action,
                      DemoActionConfig& config, std::string& error);
bool resolve_demo_config(AppContext& ctx, const std::string& action, const csrc::Json& params,
                         TrackingConfig& config, std::string& error);

}  // namespace capp
