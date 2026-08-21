# Virtual CAN Bus Dashboard

A simulated automotive CAN bus and instrument cluster, built entirely in software. No car, no
CAN transceiver, no hardware of any kind.

## What this is

Cars run on CAN. A handful of small computers (ECUs) share a two-wire bus, broadcasting things
like engine RPM and wheel speed, and whoever cares picks them up. I wanted to actually understand
that stack rather than read about it, so this project builds a working version of it on a Linux
box:

* a few C++ programs that each pretend to be one ECU, publishing a signal at a realistic rate
* a virtual CAN interface (`vcan0`) standing in for the physical bus
* a `.dbc` file, the industry-standard schema that says what every bit on the bus means
* a C++/Qt Quick dashboard that subscribes to the bus and drives live gauges
* a fault injector that puts malformed and out-of-range frames on the wire, so you can watch the
  dashboard notice

The point is the whole loop: how frames are packed, how nodes are scheduled, how a receiver
decodes them, and how it tells good data from bad.

## Architecture

```
  engine_rpm    ENGINE_STATUS  0x100  50 Hz  ─┐
  coolant_temp  COOLANT        0x200   1 Hz  ─┼──►  vcan0  ──►  Qt/QML dashboard
  wheel_speed   WHEEL_SPEED    0x300  50 Hz  ─┘       ▲
                                                      │
                                               fault injector

                     dbc/vehicle.dbc
        (read by both sides, so they cannot disagree)
```

Each node is a separate process (and a separate container) because that is what a car actually
looks like. They do not talk to each other or to the dashboard directly. They only put frames on
the bus, and anything bound to the bus gets a copy.

## Status

Working now:

* `vcan0` setup scripts
* three C++ ECU nodes on SocketCAN, each with its own cycle time
* `dbc/vehicle.dbc` — the bus schema, readable by `cantools`, SavvyCAN or any DBC tool
* a shared vehicle model, so RPM and road speed are physically consistent
* rolling counters and checksums on the two safety-relevant messages
* shared socket and scheduling code under `common/`
* CMake build for running natively, plus one container per node

Not built yet:

* the Qt/QML gauge cluster, and the DBC parser it decodes with
* fault injection and the detectors that go with it
* CSV logging, drive-cycle replay, message-rate anomaly detection

## Requirements

Linux. SocketCAN and the `vcan` module are Linux-only, so macOS and Windows are out unless you
use WSL2 or a VM.

```
sudo apt install can-utils build-essential cmake
```

The dashboard (not built yet) will add Qt 6:

```
sudo apt install qt6-base-dev qt6-declarative-dev
```

Docker is optional. The nodes build and run fine without it.

## Running it

Bring up the virtual bus (once per boot):

```
sudo ./scripts/vcan-up.sh
```

Build and run the nodes:

```
cmake -B build && cmake --build build
./build/engine_rpm &
./build/coolant_temp &
./build/wheel_speed &
```

Or with containers:

```
docker compose up --build
```

Either way, watch the bus from another terminal:

```
candump vcan0
```

You should see three IDs interleaved at three different rates:

```
  vcan0  300   [4]  09 C4 03 68
  vcan0  100   [4]  1F E7 23 7F
  vcan0  300   [4]  09 C4 04 6F
  vcan0  200   [1]  7D
```

Reading those against `dbc/vehicle.dbc`:

* `300`: `09 C4` is 2500 at 0.01 km/h per bit, so **25.00 km/h**. `03` is the rolling counter in
  the low nibble, brake bit clear. `68` is the checksum. Next frame, the counter is `04`.
* `100`: `1F E7` is 8167 at 0.25 rpm per bit, so **2041.75 rpm**. `23` is counter 3 in the low
  nibble and **gear 2** in the high nibble.
* `200`: `7D` is 125, and the signal has a -40 offset, so **85 degC**.

25 km/h in second gear at 2040 rpm is a consistent picture of one car, which is the point of this
phase — those three frames came from three separate processes that never spoke to each other.

`cansniffer vcan0` is nicer for watching over a long period, since it collapses everything into
one line per ID.

Tear the bus down with `sudo ./scripts/vcan-down.sh`.

The interface name is not hardcoded. Pass it as an argument (`./build/engine_rpm vcan1`) or set
`CAN_IFACE`.

## Layout

```
dbc/             vehicle.dbc, the schema every side of the bus is written against
common/          shared C++ headers: socket wrapper, scheduler, E2E, vehicle model
nodes/           one directory per ECU, each with a main.cpp and a Dockerfile
scripts/         vcan setup and teardown
CMakeLists.txt   native build
docker-compose.yml
```

Note that the Docker build context is the repository root, not the node directory, since the
nodes need `common/`.

## A couple of things that caught me out

Containers get their own network namespace by default, which means `vcan0` does not exist inside
them and every node dies on `SIOCGIFINDEX`. `network_mode: host` fixes it. You do not need
`privileged`, since `CAP_NET_RAW` is already in Docker's default set.

`sleep_for` in a send loop drifts, because it sleeps for at least the requested time and the work
happens on top of that. The nodes schedule against absolute deadlines instead and count any
cycles they miss. That matters later, when the dashboard starts judging node health by message
rate.

The three nodes never talk to each other, yet the tachometer and the speedometer agree. They
manage that by computing the car's state as a pure function of wall-clock time
(`common/vehicle_model.hpp`), so three separately started processes sampling at different rates
all read the same imaginary car.

The two safety-relevant messages carry a rolling counter and a checksum inside the payload, on
top of the CRC the CAN controller already does. The hardware CRC proves the bits survived the
wire; it says nothing about a sender that has hung with a plausible-looking value stuck in its
output buffer. That is what the counter is for, and it is the whole basis of the fault detection
coming later.
