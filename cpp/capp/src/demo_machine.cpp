#include "capp/demo_machine.hpp"

#include <algorithm>
#include <cmath>

namespace capp {

namespace {

int pulse_ms(double duration, int minimum, int maximum) {
    // Clamp before conversion so a large observation cannot overflow an int.
    return static_cast<int>(std::clamp(std::floor(duration),
                                      static_cast<double>(minimum),
                                      static_cast<double>(maximum)));
}

}  // namespace

const char* demo_phase_name(DemoPhase phase) {
    switch (phase) {
        case DemoPhase::Detect: return "detect";
        case DemoPhase::Forward: return "forward";
        case DemoPhase::Back: return "back";
        case DemoPhase::TurnLeft: return "turn_left";
        case DemoPhase::TurnRight: return "turn_right";
        case DemoPhase::Gap: return "gap";
        case DemoPhase::Arrived: return "arrived";
        case DemoPhase::Grabbing: return "grabbing";
        case DemoPhase::Done: return "done";
    }
    return "done";
}

const char* demo_command_name(DemoCommand command) {
    switch (command) {
        case DemoCommand::None: return "";
        case DemoCommand::Detect: return "detect";
        case DemoCommand::Forward: return "forward";
        case DemoCommand::Back: return "back";
        case DemoCommand::TurnLeft: return "turn_left";
        case DemoCommand::TurnRight: return "turn_right";
        case DemoCommand::Standby: return "standby";
        case DemoCommand::Brake: return "brake";
        case DemoCommand::Grab: return "grab";
    }
    return "";
}

DemoMachine::DemoMachine(const TrackingConfig& config, int64_t now_ms)
    : config_(config), last_seen_ms_(now_ms) {}

DemoCommand DemoMachine::move(DemoPhase phase, DemoCommand command,
                              int duration_ms, int64_t now_ms) {
    phase_ = phase;
    deadline_ms_ = now_ms + duration_ms;
    return command;
}

DemoCommand DemoMachine::tick(int64_t now_ms, const TargetObservation* target, bool arm_done) {
    switch (phase_) {
        case DemoPhase::Forward:
        case DemoPhase::Back:
        case DemoPhase::TurnLeft:
        case DemoPhase::TurnRight:
            if (now_ms < deadline_ms_) return DemoCommand::None;
            phase_ = DemoPhase::Gap;
            deadline_ms_ = now_ms + config_.loop_gap_ms;
            return DemoCommand::Standby;
        case DemoPhase::Gap:
            if (now_ms < deadline_ms_) return DemoCommand::None;
            phase_ = DemoPhase::Detect;
            return DemoCommand::Detect;
        case DemoPhase::Arrived:
            if (config_.on_arrival == ArrivalAction::Stop) {
                phase_ = DemoPhase::Done;
                result_ = DemoResult::Reached;
                return DemoCommand::None;
            }
            phase_ = DemoPhase::Grabbing;
            deadline_ms_ = now_ms + config_.grab_wait_ms;
            return DemoCommand::Grab;
        case DemoPhase::Grabbing:
            // A configured delay cannot claim completion before the actual arm finishes.
            if (now_ms >= deadline_ms_ && arm_done) {
                phase_ = DemoPhase::Done;
                result_ = DemoResult::Grabbed;
            }
            return DemoCommand::None;
        case DemoPhase::Done:
            return DemoCommand::None;
        case DemoPhase::Detect:
            break;
    }

    if (!target) return DemoCommand::Detect;
    if (!target->present || !std::isfinite(target->width) || target->width <= 0 ||
        !std::isfinite(target->offset)) {
        if (lost_for(now_ms) > config_.lost_ms) {
            phase_ = DemoPhase::Done;
            result_ = DemoResult::Lost;
        } else {
            phase_ = DemoPhase::Gap;
            deadline_ms_ = now_ms + config_.loop_gap_ms;
        }
        return DemoCommand::Standby;
    }

    target_ = *target;
    last_seen_ms_ = now_ms;
    const double align_error = target->offset - config_.grab_offset;
    if (target->width > config_.target_size * config_.too_close_k) {
        const double over = target->width - config_.target_size * config_.too_close_k;
        return move(DemoPhase::Back, DemoCommand::Back,
                    pulse_ms(config_.back_pulse_k * over,
                             config_.back_pulse_min, config_.back_pulse_max), now_ms);
    }
    if (target->width >= config_.target_size && std::abs(align_error) <= config_.align_margin) {
        phase_ = DemoPhase::Arrived;
        return DemoCommand::Brake;
    }
    if (std::abs(target->offset) > config_.center_margin || target->width >= config_.target_size) {
        const int maximum = std::abs(target->offset) > config_.center_margin
                                ? config_.turn_pulse_max : config_.fine_pulse_max;
        const int duration = pulse_ms(config_.turn_pulse_k * std::abs(align_error),
                                      config_.turn_pulse_min, maximum);
        return align_error < 0
                   ? move(DemoPhase::TurnLeft, DemoCommand::TurnLeft, duration, now_ms)
                   : move(DemoPhase::TurnRight, DemoCommand::TurnRight, duration, now_ms);
    }
    return move(DemoPhase::Forward, DemoCommand::Forward, config_.forward_pulse_ms, now_ms);
}

}  // namespace capp
