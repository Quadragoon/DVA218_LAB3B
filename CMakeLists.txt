cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 99)

add_executable(Sender sender.c common.c common.h)
add_executable(Receiver receiver.c common.c common.h)