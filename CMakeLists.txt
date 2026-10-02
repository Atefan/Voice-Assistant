cmake_minimum_required(VERSION 3.16)
project(TalkProject CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_library(tasks src/Tasks.cpp)
target_include_directories(tasks PUBLIC inc)
target_link_libraries(tasks PUBLIC Threads::Threads)

enable_testing()
add_subdirectory(tst)
