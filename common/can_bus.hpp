// common/can_bus.hpp
//
// A small RAII wrapper around a SocketCAN raw socket.
//
// SocketCAN exposes the CAN bus through the ordinary BSD socket API: a CAN
// interface is a network interface, and a CAN frame is what you read/write.
// Every CAN program on Linux performs the same four steps, which this class
// hides behind a constructor:
//
//   1. socket(AF_CAN, SOCK_RAW, CAN_RAW)  ask for a raw CAN socket
//   2. ioctl(SIOCGIFINDEX)                resolve "vcan0" -> interface index
//   3. bind(sockaddr_can)                 attach the socket to that interface
//   4. read()/write()                     exchange struct can_frame
//
#pragma once

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>

#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <linux/can.h>
#include <linux/can/raw.h>

namespace canbus {

// Throws std::system_error carrying the current errno, so callers get a real
// message ("bind(vcan0): No such device") instead of a bare -1.
[[noreturn]] inline void throw_errno(const std::string& what) {
    throw std::system_error(errno, std::system_category(), what);
}

class Socket {
public:
    // Opens, resolves and binds in one shot. Throws on any failure, which
    // means a constructed Socket is always a usable Socket.
    explicit Socket(std::string iface) : iface_(std::move(iface)) {
        fd_ = ::socket(AF_CAN, SOCK_RAW, CAN_RAW);
        if (fd_ < 0) throw_errno("socket(AF_CAN, SOCK_RAW, CAN_RAW)");

        // Interfaces are named to humans but numbered to the kernel.
        struct ifreq ifr {};
        std::strncpy(ifr.ifr_name, iface_.c_str(), IFNAMSIZ - 1);
        if (::ioctl(fd_, SIOCGIFINDEX, &ifr) < 0) {
            int saved = errno;
            ::close(fd_);          // don't leak the fd on the error path
            fd_ = -1;
            errno = saved;
            throw_errno("ioctl(SIOCGIFINDEX, " + iface_ + ") - is the interface up?");
        }
        ifindex_ = ifr.ifr_ifindex;

        struct sockaddr_can addr {};
        addr.can_family  = AF_CAN;
        addr.can_ifindex = ifindex_;
        if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            int saved = errno;
            ::close(fd_);
            fd_ = -1;
            errno = saved;
            throw_errno("bind(" + iface_ + ")");
        }
    }

    // The destructor is the whole point: the fd closes on every exit path.
    ~Socket() {
        if (fd_ >= 0) ::close(fd_);
    }

    // Rule of five: this object owns a raw handle, so copying it would let two
    // objects close the same fd. Copy is deleted; move transfers ownership.
    Socket(const Socket&)            = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept
        : fd_(other.fd_), ifindex_(other.ifindex_), iface_(std::move(other.iface_)) {
        other.fd_ = -1;   // the moved-from object must not close it
    }

    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_       = other.fd_;
            ifindex_  = other.ifindex_;
            iface_    = std::move(other.iface_);
            other.fd_ = -1;
        }
        return *this;
    }

    // Always writes sizeof(struct can_frame) - the kernel uses frame.can_dlc
    // to decide how many payload bytes actually go on the wire.
    void send(const struct can_frame& frame) const {
        ssize_t n = ::write(fd_, &frame, sizeof(frame));
        if (n < 0) throw_errno("write(" + iface_ + ")");
        if (n != static_cast<ssize_t>(sizeof(frame)))
            throw std::runtime_error("short write on " + iface_ + ": CAN frames are atomic");
    }

    // Blocks until a frame arrives. Returns false only on a clean EOF.
    bool receive(struct can_frame& frame) const {
        ssize_t n = ::read(fd_, &frame, sizeof(frame));
        if (n < 0) throw_errno("read(" + iface_ + ")");
        if (n == 0) return false;
        return n == static_cast<ssize_t>(sizeof(frame));
    }

    int fd() const noexcept { return fd_; }
    int ifindex() const noexcept { return ifindex_; }
    const std::string& iface() const noexcept { return iface_; }

private:
    int         fd_      = -1;
    int         ifindex_ = -1;
    std::string iface_;
};

// Interface name resolution, in priority order:
//   argv[1]  ->  $CAN_IFACE  ->  "vcan0"
// Lets the same binary run bare on the host or inside a container without a
// rebuild, which is what the compose file relies on.
inline std::string iface_from(int argc, char** argv) {
    if (argc > 1) return argv[1];
    if (const char* env = std::getenv("CAN_IFACE")) return env;
    return "vcan0";
}

}  // namespace canbus
