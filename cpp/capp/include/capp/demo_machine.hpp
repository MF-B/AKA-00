// Tracking decisions and deadlines, independent of threads, devices and JSON.
#pragma once

#include <cstdint>

namespace capp {

enum class ArrivalAction { Stop, Grab };

struct TrackingConfig {
    ArrivalAction on_arrival = ArrivalAction::Stop;
    double target_size = 300;
    int speed = 25;
    int turn_speed = 25;
    double center_margin = 80;
    double align_margin = 25;
    double grab_offset = 50;
    double turn_pulse_k = 0.5;
    int turn_pulse_min = 300;
    int turn_pulse_max = 400;
    int fine_pulse_max = 500;
    int forward_pulse_ms = 600;
    double too_close_k = 1.5;
    double back_pulse_k = 2;
    int back_pulse_min = 300;
    int back_pulse_max = 700;
    int lost_ms = 1500;
    int loop_gap_ms = 30;
    int grab_wait_ms = 4000;
    int round_gap_ms = 200;
    double conf = 0.25;
    double iou = 0.45;
};

struct TargetObservation {
    bool present = false;
    double width = 0;
    double offset = 0;  // Target center minus image center, in source pixels.
};

enum class DemoPhase { Detect, Forward, Back, TurnLeft, TurnRight, Gap, Arrived, Grabbing, Done };
enum class DemoCommand { None, Detect, Forward, Back, TurnLeft, TurnRight, Standby, Brake, Grab };
enum class DemoResult { None, Reached, Grabbed, Lost };

const char* demo_phase_name(DemoPhase phase);
const char* demo_command_name(DemoCommand command);

// The caller supplies monotonic milliseconds, observations and arm completion.
// tick() never sleeps or performs I/O; a platform adapter executes its command.
class DemoMachine {
public:
    explicit DemoMachine(const TrackingConfig& config, int64_t now_ms = 0);
    DemoCommand tick(int64_t now_ms, const TargetObservation* target = nullptr,
                     bool arm_done = false);

    DemoPhase phase() const { return phase_; }
    DemoResult result() const { return result_; }
    const TargetObservation& target() const { return target_; }
    int64_t lost_for(int64_t now_ms) const { return now_ms - last_seen_ms_; }

private:
    DemoCommand move(DemoPhase phase, DemoCommand command, int duration_ms, int64_t now_ms);

    TrackingConfig config_;
    DemoPhase phase_ = DemoPhase::Detect;
    DemoResult result_ = DemoResult::None;
    TargetObservation target_;
    int64_t deadline_ms_ = 0;
    int64_t last_seen_ms_ = 0;
};

}  // namespace capp
