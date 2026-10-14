cmake_minimum_required(VERSION 3.16)
project(TalkProject C CXX)   # C is needed only because llama's CMake package requests c_std_90

list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/ai-libs")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(llama REQUIRED)
find_package(whisper REQUIRED)

add_library(tasks src/Tasks.cpp)
target_include_directories(tasks PUBLIC inc)
target_link_libraries(tasks PUBLIC Threads::Threads)

add_library(models src/Models.cpp)
target_include_directories(models PUBLIC inc)
target_link_libraries(models PUBLIC llama whisper)

add_library(wav src/Wav.cpp)
target_include_directories(wav PUBLIC inc)

add_library(chatml src/ChatML.cpp)
target_include_directories(chatml PUBLIC inc)

# Plain POSIX sockets: no third-party dependency, so this one builds and links
# in a fraction of a second even when llama/whisper are not touched.
add_library(sockets src/Sockets.cpp)
target_include_directories(sockets PUBLIC inc)
# no link deps on purpose: this layer is bytes on an fd and nothing else

# Frame <-> Task, the seam between the wire and the work
add_library(protocol src/Protocol.cpp)
target_include_directories(protocol PUBLIC inc)
target_link_libraries(protocol PUBLIC tasks sockets)

add_executable(talk_server src/Server.cpp src/TaskHandler.cpp)
target_link_libraries(talk_server PRIVATE tasks models wav chatml sockets protocol)

enable_testing()
add_subdirectory(tst)
