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
    printf("Socket created successfully, fd = %d\n", sock);

    struct ifreq ifr{};
    strncpy(ifr.ifr_name, "vcan0", IFNAMSIZ - 1);

    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl (is vcan0 up?)");
        return 1;
    }
    printf("vcan0 resolved to interface index %d\n", ifr.ifr_ifindex);

    struct sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    printf("Attached to vcan0 — ready to speak\n");

    uint16_t rpm_raw = 0;

    while (true) {
        struct can_frame frame{};
        frame.can_id = 0x100;
        frame.can_dlc = 2;
        frame.data[0] = (rpm_raw >> 8) & 0xFF;
        frame.data[1] = rpm_raw & 0xFF;

        ssize_t nbytes = write(sock, &frame, sizeof(frame));
        if (nbytes != sizeof(frame)) {
            perror("write");
            break;
        }

        printf("Sent frame, raw value = %u\n", rpm_raw);

        rpm_raw += 40;
        if (rpm_raw > 8000) rpm_raw = 0;

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    return 0;
}