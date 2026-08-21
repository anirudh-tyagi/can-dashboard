// common/e2e.hpp
//
// End-to-end protection (E2E) for safety-relevant CAN messages.
//
// CAN already has a 15-bit CRC in the frame itself, checked by the controller
// hardware. So why add our own? Because the hardware CRC only proves the bits
// survived the wire. It cannot detect:
//
//   * a sender that has crashed or hung, still leaving its last frame cached
//     somewhere downstream ("the value looks plausible, it just never changes")
//   * a frame that was corrupted *before* it was handed to the CAN controller,
//     e.g. by a bug in the sending software
//   * a frame replayed onto the bus by something that isn't the real ECU
//
// The automotive answer, standardised in AUTOSAR as "E2E protection", is to add
// two fields inside the payload, produced and checked by the application:
//
//   * a ROLLING COUNTER that increments on every transmission, so a receiver
//     can tell "sender is alive and this is new data" from "sender is stuck"
//   * a CHECKSUM over the rest of the payload, so a receiver can tell whether
//     the bytes it is about to trust were assembled correctly
//
// Real E2E profiles use a CRC-8 polynomial. We use XOR, which is weaker but
// fits on one screen and detects every single-bit error, which is all Phase 5
// needs to demonstrate.
//
#pragma once

#include <cstdint>

#include <linux/can.h>

namespace e2e {

// A non-zero seed. Without it, an all-zero payload would produce checksum 0,
// which means a frame of nothing but zeros - the exact thing a dead or
// uninitialised sender emits - would pass validation.
inline constexpr std::uint8_t kSeed = 0xA5;

// XOR of the seed, the message ID, and every payload byte EXCEPT the checksum
// byte itself (which is what we're computing, so it cannot be an input).
//
// Mixing in can_id means a valid ENGINE_STATUS payload replayed under a
// different ID fails the check. That is a real attack, and it costs two XORs.
//
// Note the ID is FOLDED (low byte XOR high byte) rather than truncated. Our
// three IDs are 0x100, 0x200 and 0x300 - take the low byte alone and all three
// give 0x00, so the ID would contribute nothing at all. Folding gives 0x01,
// 0x02, 0x03. A worked example of why "mix in the identifier" is not enough of
// a spec on its own.
inline std::uint8_t id_tag(canid_t id) noexcept {
    const canid_t bits = id & CAN_EFF_MASK;   // strip the EFF/RTR/ERR flags
    return static_cast<std::uint8_t>((bits & 0xFF) ^ ((bits >> 8) & 0xFF));
}

inline std::uint8_t checksum(const struct can_frame& frame, int checksum_index) {
    std::uint8_t sum = kSeed ^ id_tag(frame.can_id);
    for (int i = 0; i < static_cast<int>(frame.can_dlc); ++i) {
        if (i == checksum_index) continue;
        sum ^= frame.data[i];
    }
    return sum;
}

// Receiver side. Phase 5's validators call these; the nodes only need
// checksum() and Counter.
inline bool checksum_ok(const struct can_frame& frame, int checksum_index) {
    return frame.data[checksum_index] == checksum(frame, checksum_index);
}

// A 4-bit counter: 0,1,2,...,14,15,0,1,... It fits in one nibble, so it costs
// half a byte rather than a whole one.
class Counter {
public:
    // Returns the value to put in THIS frame, then advances.
    std::uint8_t next() noexcept {
        const std::uint8_t current = value_;
        value_ = static_cast<std::uint8_t>((value_ + 1) & 0x0F);
        return current;
    }

    std::uint8_t peek() const noexcept { return value_; }

private:
    std::uint8_t value_ = 0;
};

// True if `now` is exactly one step after `prev`, wrapping 15 -> 0.
// A receiver seeing prev == now has a frozen sender; seeing a jump has lost
// frames (or someone is injecting).
inline bool counter_ok(std::uint8_t prev, std::uint8_t now) noexcept {
    return now == ((prev + 1) & 0x0F);
}

}  // namespace e2e
