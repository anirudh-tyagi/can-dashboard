// ECU node: engine RPM.
// Publishes CAN ID 0x100 every 200 ms. Payload is a big-endian uint16 raw RPM.

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
    std::signal(SIGTERM, on_signal);   // docker compose stop sends SIGTERM

    try {
        const std::string iface = canbus::iface_from(argc, argv);
        canbus::Socket bus(iface);
        std::printf("[engine_rpm] attached to %s (ifindex %d), publishing 0x100 @ 5 Hz\n",
                    bus.iface().c_str(), bus.ifindex());

        timing::Periodic cycle(std::chrono::milliseconds(200));
        std::uint16_t rpm_raw = 0;
        std::uint64_t ticks   = 0;

        while (g_running) {
            struct can_frame frame {};
            frame.can_id  = 0x100;
            frame.can_dlc = 2;
            frame.data[0] = (rpm_raw >> 8) & 0xFF;   // big-endian: high byte first
            frame.data[1] = rpm_raw & 0xFF;

            bus.send(frame);

            if (++ticks % 5 == 0)   // ~1 line per second instead of 5
                std::printf("[engine_rpm] rpm=%u  missed=%llu\n", rpm_raw,
                            static_cast<unsigned long long>(cycle.missed()));

            rpm_raw += 40;
            if (rpm_raw > 8000) rpm_raw = 0;

            cycle.wait_next();
        }

        std::printf("[engine_rpm] shutting down cleanly\n");
        return 0;   // ~Socket() closes the fd here
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[engine_rpm] fatal: %s\n", e.what());
        return 1;
    }
}
