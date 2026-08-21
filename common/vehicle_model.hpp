// common/vehicle_model.hpp
//
// A toy physics model of one car, shared by all three ECU nodes.
//
// WHY THIS EXISTS
// Until now each node made up its own number with its own sawtooth, so the
// dashboard would show 6000 rpm next to 3 km/h. Nothing on the bus tied them
// together. In a real car there is exactly one crankshaft and one set of
// wheels, and every ECU is measuring the same physical object - which is why
// RPM and road speed move together through the gearbox.
//
// HOW THE NODES AGREE WITHOUT TALKING TO EACH OTHER
// They don't share state and they don't send each other anything. Instead,
// every node computes the car's state as a pure function of wall-clock time:
//
//     state = f(seconds since the Unix epoch)
//
// Wall-clock time is the one thing all processes on a host already agree on,
// so three independently started programs, sampling at three different rates,
// all read the same car. That is genuinely how a simulation rig is wired: one
// shared model, many samplers.
//
// Note this uses system_clock (wall clock), NOT steady_clock. That is the
// opposite of common/periodic.hpp, which uses steady_clock precisely because
// it must not jump when the system clock is adjusted. Different jobs:
// scheduling wants a monotonic clock, a shared model wants a common one.
//
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>

namespace vehicle {

// --- Drivetrain constants -------------------------------------------------
inline constexpr double kDriveCycleSeconds = 60.0;    // one accel/cruise/brake lap
inline constexpr double kKeyOnCycleSeconds = 600.0;   // engine "restarts" every 10 min
inline constexpr double kWheelCircumferenceM = 1.8;   // ~205/55R16
inline constexpr double kFinalDrive = 4.2;
inline constexpr double kIdleRpm = 800.0;

// Index 0 is unused (gear 0 = neutral). A five-speed, tall at the top.
inline constexpr double kGearRatio[6] = {0.0, 3.50, 2.10, 1.40, 1.00, 0.80};

// Everything the three nodes between them need to transmit.
struct State {
    double t;          // seconds since this key-on cycle began
    double speed_kmh;
    double rpm;
    int    gear;       // 0 = neutral, 1..5
    bool   brake;
    double coolant_c;
};

// Seconds since the Unix epoch, as a double. ~1.8e9 today, and a double holds
// that to well under a microsecond, which is far finer than a 20 ms cycle.
inline double now_seconds() {
    const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration<double>(since_epoch).count();
}

// The drive cycle: a 60-second lap of idle, accelerate, cruise, brake, idle.
// Piecewise linear on purpose - you can read the profile straight off the code
// and predict what the gauge should do at any second.
inline double speed_at(double phase) {
    if (phase < 5.0)  return 0.0;                            // idling at a light
    if (phase < 25.0) return (phase - 5.0) * 5.0;            // 0 -> 100 km/h
    if (phase < 40.0) return 100.0;                          // cruise
    if (phase < 50.0) return 100.0 - (phase - 40.0) * 10.0;  // 100 -> 0, braking
    return 0.0;                                              // stopped
}

// A crude automatic gearbox: shift purely on road speed.
inline int gear_for(double kmh) {
    if (kmh <  1.0) return 0;
    if (kmh < 20.0) return 1;
    if (kmh < 40.0) return 2;
    if (kmh < 60.0) return 3;
    if (kmh < 90.0) return 4;
    return 5;
}

// The actual gearbox maths, and the reason the two gauges now agree:
//
//   road speed  ->  wheel revs/sec  ->  (x final drive x gear ratio)  ->  engine
//
// so every upshift drops the needle and every acceleration raises it, exactly
// as it would in the car.
inline double rpm_for(double kmh, int gear) {
    if (gear <= 0) return kIdleRpm;
    const double v_ms         = kmh / 3.6;
    const double wheel_rev_s  = v_ms / kWheelCircumferenceM;
    const double engine_rev_s = wheel_rev_s * kFinalDrive * kGearRatio[gear];
    return std::max(kIdleRpm, engine_rev_s * 60.0);
}

// Newton's law of cooling, run backwards: the block warms toward 90 C with a
// 120-second time constant, then the thermostat cycles it a couple of degrees
// either side. Exponential, not linear, because that is what heat does.
inline double coolant_at(double key_on) {
    const double warm = 20.0 + 70.0 * (1.0 - std::exp(-key_on / 120.0));
    if (warm < 85.0) return warm;
    return warm + 2.0 * std::sin(key_on / 20.0);
}

// The one entry point the nodes call. No arguments: the clock is the input.
inline State sample() {
    const double t     = std::fmod(now_seconds(), kKeyOnCycleSeconds);
    const double phase = std::fmod(t, kDriveCycleSeconds);

    State s{};
    s.t         = t;
    s.speed_kmh = speed_at(phase);
    s.gear      = gear_for(s.speed_kmh);
    s.rpm       = rpm_for(s.speed_kmh, s.gear);
    s.brake     = (phase >= 40.0 && phase < 50.0);
    s.coolant_c = coolant_at(t);
    return s;
}

}  // namespace vehicle
