// ECU node: engine control unit.
//
// Publishes ENGINE_STATUS (CAN ID 0x100) every 20 ms, encoded to match
// dbc/vehicle.dbc exactly:
//
//   byte 0-1   EngineRPM     u16 big-endian, 0.25 rpm/bit
//   byte 2     low nibble    EngCounter (0-15, rolls over)
//              high nibble   Gear (0 = neutral, 1-5)
//   byte 3     EngChecksum   XOR over the rest, see common/e2e.hpp
//
// Every bit is packed by hand here rather than by a library, because the whole
// point of this phase is seeing where each bit lands.

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

constexpr canid_t kMsgId       = 0x100;
constexpr int     kDlc         = 4;
constexpr int     kChecksumIdx = 3;
constexpr double  kRpmScale    = 0.25;   // rpm per bit, from the DBC
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
        std::printf("[engine_rpm] attached to %s (ifindex %d), publishing 0x100 @ 50 Hz\n",
                    bus.iface().c_str(), bus.ifindex());

        timing::Periodic cycle(std::chrono::milliseconds(20));
        e2e::Counter     counter;
        std::uint64_t    ticks = 0;

        while (g_running) {
            const vehicle::State st = vehicle::sample();

            // Physical value -> raw value. Divide by the DBC scale, round to
            // the nearest integer (never truncate: that biases every reading
            // downward), then clamp so an out-of-range model value cannot
            // silently wrap around the top of a uint16.
            long raw = std::lround(st.rpm / kRpmScale);
            if (raw < 0)     raw = 0;
            if (raw > 65535) raw = 65535;
            const auto rpm_raw = static_cast<std::uint16_t>(raw);

            struct can_frame frame {};
            frame.can_id  = kMsgId;
            frame.can_dlc = kDlc;

            // Big-endian (Motorola): most significant byte goes out first.
            frame.data[0] = static_cast<std::uint8_t>((rpm_raw >> 8) & 0xFF);
            frame.data[1] = static_cast<std::uint8_t>(rpm_raw & 0xFF);

            // Two signals sharing one byte. Mask each to its own nibble before
            // OR-ing, so a bad gear value can never corrupt the counter.
            const std::uint8_t counter_nib = counter.next() & 0x0F;
            const std::uint8_t gear_nib    = static_cast<std::uint8_t>(st.gear) & 0x0F;
            frame.data[2] = static_cast<std::uint8_t>(counter_nib | (gear_nib << 4));

            // Computed last, over the bytes above. data[3] is still zero and
            // is skipped by checksum() anyway.
            frame.data[kChecksumIdx] = e2e::checksum(frame, kChecksumIdx);

            bus.send(frame);

            if (++ticks % 50 == 0)   // one line per second instead of fifty
                std::printf("[engine_rpm] %6.0f rpm  gear %d  cnt %2u  csum 0x%02X  missed=%llu\n",
                            st.rpm, st.gear,
                            static_cast<unsigned>(counter_nib),
                            static_cast<unsigned>(frame.data[kChecksumIdx]),
                            static_cast<unsigned long long>(cycle.missed()));

            cycle.wait_next();
        }

        std::printf("[engine_rpm] shutting down cleanly\n");
        return 0;   // ~Socket() closes the fd here
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[engine_rpm] fatal: %s\n", e.what());
        return 1;
    }
}
