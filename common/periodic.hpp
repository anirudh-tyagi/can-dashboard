// common/periodic.hpp
//
// A drift-free periodic scheduler.
//
// The naive loop is:
//
//     while (true) { do_work(); sleep_for(20ms); }
//
// which is wrong in two ways. sleep_for() guarantees *at least* the requested
// duration (the OS may wake you later), and the time spent in do_work() is
// added on top. The period becomes 20ms + work + jitter, and the error is
// cumulative: a "50 Hz" node quietly runs at 47 Hz forever.
//
// The fix is to sleep until an ABSOLUTE deadline computed from a fixed start
// point. Work time then happens *inside* the period instead of extending it,
// and a late wakeup is corrected by the next one rather than compounding.
//
#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

namespace timing {

class Periodic {
public:
    explicit Periodic(std::chrono::milliseconds period)
        : period_(period), next_(std::chrono::steady_clock::now() + period) {}

    // Sleeps until the next deadline. If we already blew past it (the work
    // took longer than one period), skip the missed slots instead of trying
    // to catch up with a burst of back-to-back frames.
    void wait_next() {
        std::this_thread::sleep_until(next_);

        const auto now = std::chrono::steady_clock::now();
        next_ += period_;

        if (next_ < now) {
            // Overran. Advance to the first deadline still in the future.
            const auto behind  = now - next_;
            const auto skipped = behind / period_ + 1;
            missed_ += static_cast<std::uint64_t>(skipped);
            next_   += skipped * period_;
        }
    }

    // How many cycles were dropped because the node couldn't keep up. Worth
    // printing: if this climbs, the node is overloaded, not the bus.
    std::uint64_t missed() const noexcept { return missed_; }

private:
    std::chrono::milliseconds                          period_;
    std::chrono::steady_clock::time_point              next_;
    std::uint64_t                                      missed_ = 0;
};

}  // namespace timing
