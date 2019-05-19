cmake_minimum_required(VERSION 3.13)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(Sender sender.c common.c common.h)
add_executable(Receiver receiver.c common.c common.h)

target_link_libraries(Sender Threads::Threads)
