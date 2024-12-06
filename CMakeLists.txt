cmake_minimum_required(VERSION 3.28)

project(peernet VERSION 0.1.0)

# Set C++ standards and other project-wide settings
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_DEBUG_POSTFIX "-d")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable testing
include(CTest)
enable_testing()

# Fetch and include GoogleTest using FetchContent
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
FetchContent_MakeAvailable(googletest)

# Add main application source files
add_executable(peernet src/main.cpp)

# Set up tests as a separate executable for test organization
add_executable(peernet_tests tests/dht_test.cpp)

# Link GoogleTest to the test executable only (not the main application)
target_link_libraries(peernet_tests PRIVATE gtest gtest_main)

# Discover tests automatically
include(GoogleTest)
gtest_discover_tests(peernet_tests)

# CPack settings for packaging (optional)
set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
