// ECU node: coolant temperature sensor.
//
// Publishes COOLANT (CAN ID 0x200) every 1000 ms, encoded to match
// dbc/vehicle.dbc exactly:
//
//   byte 0     CoolantTemp   u8, 1 degC per bit, offset -40
//
// No counter and no checksum, unlike the other two messages. That is a
// deliberate design decision, not laziness: E2E fields cost payload bytes and
// CPU on both ends, so they go on signals that can hurt someone if they lie.
// A wrong temperature gauge is annoying; a wrong wheel speed reaching a
// stability-control module is not. Real buses are full of both kinds.
//
// The -40 offset is the OBD-II standard encoding (PID 0x05): storing
// (celsius + 40) as an unsigned byte covers -40..215 degC with no sign bit and
// no wasted range.

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cmath>

#include <linux/can.h>

#include "common/can_bus.hpp"
#include "common/periodic.hpp"
#include "common/vehicle_model.hpp"

namespace {
volatile std::sig_atomic_t g_running = 1;
void on_signal(int) { g_running = 0; }

constexpr canid_t kMsgId  = 0x200;
constexpr int     kDlc    = 1;
constexpr int     kOffset = -40;   // physical = raw + offset
}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    try {
        const std::string iface = canbus::iface_from(argc, argv);
        canbus::Socket bus(iface);
        std::printf("[coolant_temp] attached to %s (ifindex %d), publishing 0x200 @ 1 Hz\n",
                    bus.iface().c_str(), bus.ifindex());

        timing::Periodic cycle(std::chrono::milliseconds(1000));

        while (g_running) {
            const vehicle::State st = vehicle::sample();

            // Encoding is the inverse of decoding: raw = (physical - offset) / scale.
            // Scale is 1 here, so this is just the offset being undone.
            long raw = std::lround(st.coolant_c) - kOffset;
            if (raw < 0)   raw = 0;
            if (raw > 255) raw = 255;

            struct can_frame frame {};
            frame.can_id  = kMsgId;
            frame.can_dlc = kDlc;
            frame.data[0] = static_cast<std::uint8_t>(raw);

            bus.send(frame);
            std::printf("[coolant_temp] %5.1f degC (raw 0x%02X)\n", st.coolant_c, frame.data[0]);

            cycle.wait_next();
        }

        std::printf("[coolant_temp] shutting down cleanly\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[coolant_temp] fatal: %s\n", e.what());
        return 1;
    }
}
