include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(test_tasks test_tasks.cpp)
target_link_libraries(test_tasks PRIVATE tasks GTest::gtest_main)
gtest_discover_tests(test_tasks)

# Kept in its own binary: TaskQueue is not actually thread-safe (the mutex in
# Tasks.cpp is a local), so these tests can corrupt memory and abort the
# process. Isolating them keeps a crash from taking the rest of the suite down.
add_executable(test_tasks_threading test_tasks_threading.cpp)
target_link_libraries(test_tasks_threading PRIVATE tasks GTest::gtest_main)
gtest_discover_tests(test_tasks_threading)
