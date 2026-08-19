FROM ubuntu:24.04

RUN apt-get update && apt-get install -y g++ && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY main.cpp .
RUN g++ -std=c++17 -O2 -o main main.cpp

CMD ["./main"]
