cmake_minimum_required(VERSION 3.16)
project(TalkProject C CXX)   # C is needed only because llama's CMake package requests c_std_90

list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/ai-libs")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(llama REQUIRED)

add_library(tasks src/Tasks.cpp)
target_include_directories(tasks PUBLIC inc)
target_link_libraries(tasks PUBLIC Threads::Threads)

add_library(models src/Models.cpp)
target_include_directories(models PUBLIC inc)
target_link_libraries(models PUBLIC llama)

add_executable(talk_server src/Server.cpp)
target_link_libraries(talk_server PRIVATE tasks)

enable_testing()
add_subdirectory(tst)
