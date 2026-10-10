cmake_minimum_required(VERSION 3.24)
project(RunlineCore CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(RunlineCore STATIC src/BoxWorld.cpp src/Move.cpp src/Probes.cpp src/MeshWorld.cpp src/Locomotion.cpp src/Tuning.cpp src/Controller.cpp src/ControllerStubs.cpp src/Vault.cpp)
target_include_directories(RunlineCore PUBLIC include)
if(MSVC)
  target_compile_options(RunlineCore PRIVATE /W4 /fp:strict)
endif()

option(RUNLINE_TESTS "Build RunlineCore's doctest suite" ON)
if(RUNLINE_TESTS)
include(FetchContent)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.5.3)   # 2.4.x declares cmake_minimum_required(3.0), which CMake 4 rejects
FetchContent_MakeAvailable(doctest)

add_executable(RunlineCoreTests
  tests/test_main.cpp tests/vec3_tests.cpp tests/world_tests.cpp tests/golden_tests.cpp tests/move_tests.cpp tests/probes_tests.cpp tests/mesh_world_tests.cpp tests/source_tests.cpp tests/trace_tests.cpp tests/locomotion_tests.cpp tests/tuning_tests.cpp tests/controller_tests.cpp tests/replay_tests.cpp)
target_link_libraries(RunlineCoreTests PRIVATE RunlineCore doctest::doctest)
target_compile_definitions(RunlineCoreTests PRIVATE
  RUNLINE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden"
  RUNLINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
  target_compile_options(RunlineCoreTests PRIVATE /fp:strict)
endif()
enable_testing()
add_test(NAME RunlineCoreTests COMMAND RunlineCoreTests)
endif()
