// ECU node: ABS module.
//
// Publishes WHEEL_SPEED (CAN ID 0x300) every 20 ms, encoded to match
// dbc/vehicle.dbc exactly:
//
//   byte 0-1   VehicleSpeed  u16 big-endian, 0.01 km/h per bit
//   byte 2     bits 0-3      WhlCounter (0-15, rolls over)
//              bit  4        BrakePressed
//              bits 5-7      reserved, transmitted as zero
//   byte 3     WhlChecksum   XOR over the rest, see common/e2e.hpp
//
// 0.01 km/h per bit looks absurdly fine-grained for a speedometer, and it is -
// but this signal also feeds ABS and traction control, which need to see tiny
// differences between wheels. Resolution is chosen by the hungriest consumer,
// not by the prettiest display.

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cmath>

#include <linux/can.h>

#include "common/can_bus.hpp"
#include "common/e2e.hpp"
#include "common/periodic.hpp"
#include "common/vehicle_model.hpp"

namespace {
volatile std::sig_atomic_t g_running = 1;
void on_signal(int) { g_running = 0; }

constexpr canid_t kMsgId       = 0x300;
constexpr int     kDlc         = 4;
constexpr int     kChecksumIdx = 3;
constexpr double  kSpeedScale  = 0.01;   // km/h per bit, from the DBC
constexpr std::uint8_t kBrakeBit = 0x10; // bit 4 of byte 2
}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    try {
        const std::string iface = canbus::iface_from(argc, argv);
        canbus::Socket bus(iface);
        std::printf("[wheel_speed] attached to %s (ifindex %d), publishing 0x300 @ 50 Hz\n",
                    bus.iface().c_str(), bus.ifindex());

        timing::Periodic cycle(std::chrono::milliseconds(20));
        e2e::Counter     counter;
        std::uint64_t    ticks = 0;

        while (g_running) {
            const vehicle::State st = vehicle::sample();

            long raw = std::lround(st.speed_kmh / kSpeedScale);
            if (raw < 0)     raw = 0;
            if (raw > 65535) raw = 65535;
            const auto speed_raw = static_cast<std::uint16_t>(raw);

            struct can_frame frame {};
            frame.can_id  = kMsgId;
            frame.can_dlc = kDlc;

            frame.data[0] = static_cast<std::uint8_t>((speed_raw >> 8) & 0xFF);
            frame.data[1] = static_cast<std::uint8_t>(speed_raw & 0xFF);

            const std::uint8_t counter_nib = counter.next() & 0x0F;
            frame.data[2] = static_cast<std::uint8_t>(counter_nib | (st.brake ? kBrakeBit : 0));

            frame.data[kChecksumIdx] = e2e::checksum(frame, kChecksumIdx);

            bus.send(frame);

            if (++ticks % 50 == 0)
                std::printf("[wheel_speed] %6.2f km/h  brake %d  cnt %2u  csum 0x%02X  missed=%llu\n",
                            st.speed_kmh, st.brake ? 1 : 0,
                            static_cast<unsigned>(counter_nib),
                            static_cast<unsigned>(frame.data[kChecksumIdx]),
                            static_cast<unsigned long long>(cycle.missed()));

            cycle.wait_next();
        }

        std::printf("[wheel_speed] shutting down cleanly\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[wheel_speed] fatal: %s\n", e.what());
        return 1;
    }
}
