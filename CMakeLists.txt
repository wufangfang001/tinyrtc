cmake_minimum_required(VERSION 3.10)

# Set platform default to linux
if(NOT DEFINED CONFIG_PLATFORM)
    set(CONFIG_PLATFORM "linux")
endif()

# Handle toolchain: when AOSL declares project, it needs full compiler path
set(CMAKE_C_COMPILER /usr/bin/gcc CACHE PATH "C compiler")
set(CMAKE_CXX_COMPILER /usr/bin/g++ CACHE PATH "C++ compiler")
set(CC /usr/bin/gcc)
set(CXX /usr/bin/g++)

project(tinyrtc C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON) # GNU extensions

# Options
option(TINYRTC_BUILD_EXAMPLES "Build example applications" ON)
option(TINYRTC_BUILD_TESTS "Build unit tests" OFF)
option(TINYRTC_ENABLE_DEBUG "Enable debug logging" ON)
option(TINYRTC_ENABLE_SCTP "Enable SCTP for data channels" OFF)
option(TINYRTC_ENABLE_TURN "Enable TURN support" ON)

# Compiler flags
if(CMAKE_C_COMPILER_ID MATCHES "GCC|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(TINYRTC_ENABLE_DEBUG)
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O2)
    endif()
endif()

# Configuration
if(TINYRTC_ENABLE_DEBUG)
    add_definitions(-DTINYRTC_DEBUG=1)
else()
    add_definitions(-DTINYRTC_DEBUG=0)
endif()
add_definitions(-DTINYRTC_MAX_PEERS=16)
add_definitions(-DTINYRTC_MAX_CANDIDATES=32)

if(TINYRTC_ENABLE_SCTP)
    add_definitions(-DTINYRTC_HAVE_SCTP=1)
endif()

if(TINYRTC_ENABLE_TURN)
    add_definitions(-DTINYRTC_HAVE_TURN=1)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/aosl/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/aosl/platform/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/aosl/platform/src/${CONFIG_PLATFORM}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/aosl/platform/src/${CONFIG_PLATFORM}/config/hal
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mbedtls/include
)

# Add AOSL subdirectory - we are the main project, AOSL doesn't need to declare itself
# but we still need it to create the library, which is handled in build.cmake now
set(AOSL_DECLARE_PROJECT OFF CACHE INTERNAL "AOSL doesn't declare project when built as subdirectory")
add_subdirectory(third_party/aosl)

# Add mbedTLS subdirectory - use the submodule version instead of system
set(ENABLE_TESTING OFF CACHE INTERNAL "Disable mbedtls testing")
set(ENABLE_PROGRAMS OFF CACHE INTERNAL "Disable mbedtls programs")
set(MBEDTLS_FATAL_WARNINGS OFF CACHE INTERNAL "Disable fatal warnings")
add_subdirectory(third_party/mbedtls)

# Collect source files
file(GLOB_RECURSE TINYRTC_SOURCES
    "src/*.c"
)

# Create library
add_library(tinyrtc STATIC ${TINYRTC_SOURCES})
target_link_libraries(tinyrtc aosl)

# DTLS/SRTP requires mbedTLS for crypto operations (lightweight, portable)
# Use the submodule version now instead of system libraries
target_link_libraries(tinyrtc mbedtls mbedx509 mbedcrypto)

# Build examples if enabled
if(TINYRTC_BUILD_EXAMPLES)
    add_subdirectory(demo)
endif()

# Build tests if enabled
if(TINYRTC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install
install(TARGETS tinyrtc DESTINATION lib)
install(DIRECTORY include/tinyrtc DESTINATION include)
