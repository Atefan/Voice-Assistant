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

# Registers each TEST() as its own ctest entry, so one crashing test does not
# take the rest of the run down with it.
gtest_discover_tests(test_tasks)

# Adding a second test file later (e.g. the threading tests):
#
#   add_executable(test_tasks_threading test_tasks_threading.cpp)
#   target_link_libraries(test_tasks_threading PRIVATE tasks GTest::gtest_main)
#   gtest_discover_tests(test_tasks_threading)
#
# A separate binary is worth it for concurrency tests specifically: a data
# race can abort the process, and you do not want that killing your other
# tests mid-run.
