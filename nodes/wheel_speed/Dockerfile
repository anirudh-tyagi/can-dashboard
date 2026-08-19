# Build context is the REPO ROOT (see docker-compose.yml): a Dockerfile cannot
# COPY ../common, so the context must contain both common/ and the node.
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y g++ && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY common/ ./common/
COPY nodes/wheel_speed/main.cpp .

RUN g++ -std=c++17 -O2 -Wall -Wextra -I. -o node main.cpp

# Interface name is overridable: CAN_IFACE env var, or a command: override.
ENV CAN_IFACE=vcan0
CMD ["./node"]
