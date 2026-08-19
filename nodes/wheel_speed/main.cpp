// ECU node: wheel speed.
// Publishes CAN ID 0x300 every 50 ms. Payload is a big-endian uint16 at
// 0.01 km/h per bit, so 120.00 km/h is transmitted as 12000.

#include <csignal>
#include <cstdint>
#include <cstdio>

#include <linux/can.h>

#include "common/can_bus.hpp"
#include "common/periodic.hpp"

namespace {
volatile std::sig_atomic_t g_running = 1;
void on_signal(int) { g_running = 0; }
}  // namespace

int main(int argc, char** argv) {
    // Docker captures stdout through a pipe, which makes it block-buffered.
    // Line-buffer it so `docker compose logs -f` streams in real time.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    try {
        const std::string iface = canbus::iface_from(argc, argv);
        canbus::Socket bus(iface);
        std::printf("[wheel_speed] attached to %s (ifindex %d), publishing 0x300 @ 20 Hz\n",
                    bus.iface().c_str(), bus.ifindex());

        timing::Periodic cycle(std::chrono::milliseconds(50));
        double speed_kmh = 0.0;
        double direction = 1.0;   // +1 accelerating, -1 braking
        std::uint64_t ticks = 0;

        while (g_running) {
            const auto raw = static_cast<std::uint16_t>(speed_kmh * 100.0);

            struct can_frame frame {};
            frame.can_id  = 0x300;
            frame.can_dlc = 2;
            frame.data[0] = (raw >> 8) & 0xFF;
            frame.data[1] = raw & 0xFF;

            bus.send(frame);

            if (++ticks % 20 == 0)   // ~1 line per second instead of 20
                std::printf("[wheel_speed] %.1f km/h (raw %u)  missed=%llu\n",
                            speed_kmh, raw,
                            static_cast<unsigned long long>(cycle.missed()));

            speed_kmh += direction * 2.0;
            if (speed_kmh >= 120.0) direction = -1.0;
            if (speed_kmh <= 0.0)   direction =  1.0;

            cycle.wait_next();
        }

        std::printf("[wheel_speed] shutting down cleanly\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[wheel_speed] fatal: %s\n", e.what());
        return 1;
    }
}
