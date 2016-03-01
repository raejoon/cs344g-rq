cmake_minimum_required(VERSION 3.2)
project(project)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread -Wall -pedantic -Wextra -Weffc++ -Werror")
set(CMAKE_BUILD_TYPE Debug)

include_directories(SYSTEM /usr/local/include/RaptorQ)
include_directories(SYSTEM /usr/local/include/eigen3)
find_library(RAPTORQ_LIBRARY RaptorQ /usr/local/lib/)

set(SOURCE_FILES
    src/address.cc
    src/address.hh
    src/tub.hh
    src/common.hh
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
    src/progress.hh
    src/wire_format.hh)

add_executable(sender src/sender.cc src/address.cc src/socket.cc src/file_descriptor.cc src/timestamp.cc
        src/poller.cc)
target_link_libraries(sender ${RAPTORQ_LIBRARY})

add_executable(receiver src/receiver.cc src/address.cc src/socket.cc src/file_descriptor.cc
        src/timestamp.cc src/poller.cc)
target_link_libraries(receiver ${RAPTORQ_LIBRARY})
