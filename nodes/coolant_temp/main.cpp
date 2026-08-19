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

    printf("Coolant Temp node attached to vcan0\n");

    int temp_c = 20;   // cold start

    while (true) {
        struct can_frame frame{};
        frame.can_id = 0x200;
        frame.can_dlc = 1;
        frame.data[0] = static_cast<uint8_t>(temp_c + 40);

        ssize_t nbytes = write(sock, &frame, sizeof(frame));
        if (nbytes != sizeof(frame)) {
            perror("write");
            break;
        }

        printf("Coolant temp = %d C\n", temp_c);

        if (temp_c < 90) temp_c += 2;   // warming up toward operating temp

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    return 0;
}