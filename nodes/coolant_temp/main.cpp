// ECU node: engine coolant temperature.
// Publishes CAN ID 0x200 every 1000 ms. Payload is one byte, offset-encoded:
// raw = celsius + 40, so -40..215 C fits in an unsigned byte with no sign bit.
// (This is exactly how real OBD-II PID 0x05 encodes coolant temperature.)

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
        std::printf("[coolant_temp] attached to %s (ifindex %d), publishing 0x200 @ 1 Hz\n",
                    bus.iface().c_str(), bus.ifindex());

        timing::Periodic cycle(std::chrono::milliseconds(1000));
        int temp_c = 20;   // cold start

        while (g_running) {
            struct can_frame frame {};
            frame.can_id  = 0x200;
            frame.can_dlc = 1;
            frame.data[0] = static_cast<std::uint8_t>(temp_c + 40);

            bus.send(frame);
            std::printf("[coolant_temp] %d C (raw 0x%02X)\n", temp_c, frame.data[0]);

            if (temp_c < 90) temp_c += 2;   // warming toward operating temperature

            cycle.wait_next();
        }

        std::printf("[coolant_temp] shutting down cleanly\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[coolant_temp] fatal: %s\n", e.what());
        return 1;
    }
}
