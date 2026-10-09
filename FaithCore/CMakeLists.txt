cmake_minimum_required(VERSION 3.24)
project(FaithCore CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(FaithCore STATIC src/BoxWorld.cpp)
target_include_directories(FaithCore PUBLIC include)
if(MSVC)
  target_compile_options(FaithCore PRIVATE /W4 /fp:strict)
endif()

include(FetchContent)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.5.3)   # 2.4.x declares cmake_minimum_required(3.0), which CMake 4 rejects
FetchContent_MakeAvailable(doctest)

add_executable(FaithCoreTests
  tests/test_main.cpp tests/vec3_tests.cpp tests/world_tests.cpp tests/golden_tests.cpp)
target_link_libraries(FaithCoreTests PRIVATE FaithCore doctest::doctest)
target_compile_definitions(FaithCoreTests PRIVATE
  FAITH_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
if(MSVC)
  target_compile_options(FaithCoreTests PRIVATE /fp:strict)
endif()
enable_testing()
add_test(NAME FaithCoreTests COMMAND FaithCoreTests)
