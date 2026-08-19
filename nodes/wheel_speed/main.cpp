#include <cstdio>
#include <cstring>
#include <cstdint>

#include <chrono>
#include <thread>

#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <linux/can.h>
#include <linux/can/raw.h>

int main() {
    int sock = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct ifreq ifr{};
    strncpy(ifr.ifr_name, "vcan0", IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl (is vcan0 up?)");
        return 1;
    }

    struct sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    printf("Wheel Speed node attached to vcan0\n");

    double speed_kmh = 0.0;
    double direction = 1.0;   // +1 = accelerating, -1 = braking

    while (true) {
        uint16_t raw = static_cast<uint16_t>(speed_kmh * 100.0);

        struct can_frame frame{};
        frame.can_id = 0x300;
        frame.can_dlc = 2;
        frame.data[0] = (raw >> 8) & 0xFF;
        frame.data[1] = raw & 0xFF;

        ssize_t nbytes = write(sock, &frame, sizeof(frame));
        if (nbytes != sizeof(frame)) {
            perror("write");
            break;
        }

        printf("Wheel speed = %.1f km/h\n", speed_kmh);

        speed_kmh += direction * 2.0;
        if (speed_kmh >= 120.0) direction = -1.0;
        if (speed_kmh <= 0.0)   direction = 1.0;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return 0;
}