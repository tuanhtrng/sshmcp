cmake_minimum_required(VERSION 3.28)
project(sshmcp VERSION 0.1.0 LANGUAGES CXX)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

configure_file(cmake/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/sshmcp/version.hpp @ONLY)

add_library(sshmcp INTERFACE)
target_compile_features(sshmcp INTERFACE cxx_std_23)
target_include_directories(sshmcp INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sshmcp/platform/${SSHMCP_PLATFORM})

add_executable(sshmcp_app app/main.cpp)
target_link_libraries(sshmcp_app PRIVATE sshmcp)
set_target_properties(sshmcp_app PROPERTIES OUTPUT_NAME sshmcp)

enable_testing()

add_executable(sshmcp_unit
    regress/unit/main.cpp
    regress/unit/test_harness.cpp
    regress/unit/test_ssh.cpp
    regress/unit/test_util.cpp
    regress/unit/test_config.cpp)
target_link_libraries(sshmcp_unit PRIVATE sshmcp)
add_test(NAME unit COMMAND sshmcp_unit)
