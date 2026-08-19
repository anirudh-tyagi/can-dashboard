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
* a PyQt dashboard that subscribes to the bus and drives live gauges
* a fault injector that puts malformed and out-of-range frames on the wire, so you can watch the
  dashboard notice

The point is the whole loop: how frames are packed, how nodes are scheduled, how a receiver
decodes them, and how it tells good data from bad.

## Architecture

```
  engine_rpm    (0x100, 5 Hz)  ─┐
  coolant_temp  (0x200, 1 Hz)  ─┼──►  vcan0  ──►  PyQt dashboard
  wheel_speed   (0x300, 20 Hz) ─┘       ▲
                                        │
                                 fault injector
```

Each node is a separate process (and a separate container) because that is what a car actually
looks like. They do not talk to each other or to the dashboard directly. They only put frames on
the bus, and anything bound to the bus gets a copy.

## Status

Working now:

* `vcan0` setup scripts
* three C++ ECU nodes on SocketCAN, each with its own cycle time
* shared socket and scheduling code under `common/`
* CMake build for running natively, plus one container per node

Not built yet:

* the DBC file and the counter/checksum fields
* the PyQt gauge cluster
* fault injection and the detectors that go with it
* CSV logging, drive-cycle replay, message-rate anomaly detection

## Requirements

Linux. SocketCAN and the `vcan` module are Linux-only, so macOS and Windows are out unless you
use WSL2 or a VM.

```
sudo apt install can-utils build-essential cmake
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
  vcan0  300   [2]  00 C8
  vcan0  100   [2]  01 90
  vcan0  300   [2]  00 CC
  vcan0  200   [1]  3C
```

`cansniffer vcan0` is nicer for watching over a long period, since it collapses everything into
one line per ID.

Tear the bus down with `sudo ./scripts/vcan-down.sh`.

The interface name is not hardcoded. Pass it as an argument (`./build/engine_rpm vcan1`) or set
`CAN_IFACE`.

## Layout

```
common/          shared C++ headers: socket wrapper, periodic scheduler
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
