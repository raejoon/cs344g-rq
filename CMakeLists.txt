cmake_minimum_required(VERSION 3.2)
project(project)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -pedantic -Wextra -Weffc++ -Werror")

include_directories(SYSTEM /usr/local/include/RaptorQ)
include_directories(SYSTEM /usr/include/eigen3)
link_directories(/usr/local/lib/)

set(SOURCE_FILES
    src/address.cc
    src/address.hh
    src/buffer.hh
    src/file_descriptor.cc
    src/file_descriptor.hh
    src/poller.cc
    src/poller.hh
    src/receiver.cc
    src/sender.cc
    src/socket.cc
    src/socket.hh
    src/timestamp.cc
    src/timestamp.hh
    src/util.hh
    src/wire_format.hh)

add_executable(sender src/sender.cc src/address.cc src/socket.cc src/file_descriptor.cc src/timestamp.cc
        src/poller.cc)
add_executable(receiver src/receiver.cc src/address.cc src/socket.cc src/file_descriptor.cc
        src/timestamp.cc src/poller.cc)