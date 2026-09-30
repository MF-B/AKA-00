#include "capp/demo_machine.hpp"

#include <cstdio>
#include <limits>

using namespace capp;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; \
} } while (0)

int main() {
    TrackingConfig config;
    TargetObservation far{true, 100, 0};
    DemoMachine approach(config);
    CHECK(approach.tick(0) == DemoCommand::Detect);
    CHECK(approach.tick(100, &far) == DemoCommand::Forward);
    CHECK(approach.tick(699) == DemoCommand::None);
    CHECK(approach.tick(700) == DemoCommand::Standby);
    CHECK(approach.tick(729) == DemoCommand::None);
    CHECK(approach.tick(730) == DemoCommand::Detect);
    TargetObservation reached{true, 300, 50};
    CHECK(approach.tick(800, &reached) == DemoCommand::Brake);
    CHECK(approach.tick(800) == DemoCommand::None);
    CHECK(approach.result() == DemoResult::Reached);
    CHECK(approach.tick(10000) == DemoCommand::None);

    config.on_arrival = ArrivalAction::Grab;
    DemoMachine grab(config);
    CHECK(grab.tick(0, &reached) == DemoCommand::Brake);
    CHECK(grab.tick(20) == DemoCommand::Grab);
    CHECK(grab.tick(4019, nullptr, true) == DemoCommand::None);
    CHECK(grab.result() == DemoResult::None);
    grab.tick(4020, nullptr, false);
    CHECK(grab.result() == DemoResult::None);
    grab.tick(4050, nullptr, true);
    CHECK(grab.result() == DemoResult::Grabbed);

    // Too-close recovery takes priority even when the target is already aligned.
    TargetObservation close{true, 650, 50};
    DemoMachine back(config);
    CHECK(back.tick(0, &close) == DemoCommand::Back);
    CHECK(back.tick(399) == DemoCommand::None);
    CHECK(back.tick(400) == DemoCommand::Standby);
    close.width = 100000;
    DemoMachine long_back(config);
    CHECK(long_back.tick(0, &close) == DemoCommand::Back);
    CHECK(long_back.tick(699) == DemoCommand::None);
    CHECK(long_back.tick(700) == DemoCommand::Standby);

    TargetObservation left{true, 100, -100};
    DemoMachine turn_left(config);
    CHECK(turn_left.tick(0, &left) == DemoCommand::TurnLeft);
    CHECK(turn_left.tick(299) == DemoCommand::None);
    CHECK(turn_left.tick(300) == DemoCommand::Standby);
    TargetObservation right{true, 100, 1000};
    DemoMachine turn_right(config);
    CHECK(turn_right.tick(0, &right) == DemoCommand::TurnRight);
    CHECK(turn_right.tick(399) == DemoCommand::None);
    CHECK(turn_right.tick(400) == DemoCommand::Standby);

    // Configured claw offset also changes the direction of fine alignment.
    config.grab_offset = -50;
    TargetObservation fine{true, 300, 20};
    DemoMachine fine_turn(config);
    CHECK(fine_turn.tick(0, &fine) == DemoCommand::TurnRight);
    CHECK(fine_turn.tick(300) == DemoCommand::Standby);
    TargetObservation shifted{true, 300, -50};
    DemoMachine shifted_grab(config);
    CHECK(shifted_grab.tick(0, &shifted) == DemoCommand::Brake);

    TargetObservation missing;
    DemoMachine lost(config, 1000);
    CHECK(lost.tick(1000, &missing) == DemoCommand::Standby);
    CHECK(lost.result() == DemoResult::None);
    CHECK(lost.tick(2500) == DemoCommand::Detect);
    CHECK(lost.tick(2500, &missing) == DemoCommand::Standby);
    CHECK(lost.result() == DemoResult::None);
    CHECK(lost.tick(2530) == DemoCommand::Detect);
    CHECK(lost.tick(2531, &missing) == DemoCommand::Standby);
    CHECK(lost.result() == DemoResult::Lost);

    TargetObservation invalid{true, std::numeric_limits<double>::quiet_NaN(), 0};
    DemoMachine bad_box(config);
    CHECK(bad_box.tick(1501, &invalid) == DemoCommand::Standby);
    CHECK(bad_box.result() == DemoResult::Lost);

    // Timings are supplied by configuration, without a sleep or clock in the core.
    config.forward_pulse_ms = 900;
    DemoMachine tuned(config, 2000);
    CHECK(tuned.tick(2000, &far) == DemoCommand::Forward);
    CHECK(tuned.tick(2899) == DemoCommand::None);
    CHECK(tuned.tick(2900) == DemoCommand::Standby);

    std::printf("Demo machine: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
