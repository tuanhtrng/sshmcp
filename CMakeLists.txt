cmake_minimum_required(VERSION 3.28)
project(sshmcp VERSION 0.9.0 LANGUAGES CXX)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

include(CheckPIESupported)
check_pie_supported(LANGUAGES CXX)

configure_file(cmake/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/sshmcp/version.hpp @ONLY)

add_library(sshmcp INTERFACE)
target_compile_features(sshmcp INTERFACE cxx_std_23)
target_include_directories(sshmcp SYSTEM INTERFACE
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
    regress/unit/test_config.cpp
    regress/unit/test_jsonrpc.cpp
    regress/unit/test_tools.cpp
    regress/unit/test_registry.cpp
    regress/unit/test_mcp.cpp
    regress/unit/test_base64.cpp
    regress/unit/test_session_protocol.cpp
    regress/unit/test_session_manager.cpp
    regress/unit/test_allow.cpp
    regress/unit/test_forward.cpp
    regress/unit/platform/${SSHMCP_PLATFORM}/test_impl.cpp)
target_include_directories(sshmcp_unit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/unit)
target_link_libraries(sshmcp_unit PRIVATE sshmcp)
add_test(NAME unit COMMAND sshmcp_unit)

add_test(NAME e2e_basic COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/driver.py
    $<TARGET_FILE:sshmcp_app>
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/cases/basic)
add_test(NAME e2e_errors COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/driver.py
    $<TARGET_FILE:sshmcp_app>
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/cases/errors)
add_test(NAME e2e_timeout COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/driver.py
    $<TARGET_FILE:sshmcp_app>
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/cases/timeout)
add_test(NAME e2e_session_basic COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/driver.py
    $<TARGET_FILE:sshmcp_app>
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/cases/session_basic)
add_test(NAME e2e_session_timeout COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/driver.py
    $<TARGET_FILE:sshmcp_app>
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/cases/session_timeout)

add_test(NAME lint_header_only COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/share/linter/header_only.py
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME lint_banned COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/share/linter/banned_tokens.py
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME lint_include COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/share/linter/include_hygiene.py
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME lint_format COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/share/linter/check_format.py
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME lint_style COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/share/linter/style.py
    ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(lint_format lint_style PROPERTIES
    SKIP_RETURN_CODE 77)
