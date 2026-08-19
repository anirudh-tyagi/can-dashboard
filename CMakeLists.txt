cmake_minimum_required(VERSION 3.16)
project(can_dashboard CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SocketCAN (<linux/can.h>, AF_CAN) exists only on Linux. Fail loudly rather
# than emitting a wall of missing-header errors on macOS or Windows.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR
    "This project uses SocketCAN and only builds on Linux. "
    "Current system: ${CMAKE_SYSTEM_NAME}")
endif()

find_package(Threads REQUIRED)

# Header-only shared code. An INTERFACE library compiles nothing itself; it
# just propagates the include path to anything that links it.
add_library(can_common INTERFACE)
target_include_directories(can_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(can_common INTERFACE Threads::Threads)

foreach(node engine_rpm coolant_temp wheel_speed)
  add_executable(${node} nodes/${node}/main.cpp)
  target_link_libraries(${node} PRIVATE can_common)
  target_compile_options(${node} PRIVATE -Wall -Wextra)
endforeach()
