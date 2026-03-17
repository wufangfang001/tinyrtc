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

# Add mbedTLS subdirectory - use the submodule version instead of system
set(ENABLE_TESTING OFF CACHE INTERNAL "Disable mbedtls testing")
set(ENABLE_PROGRAMS OFF CACHE INTERNAL "Disable mbedtls programs")
set(MBEDTLS_FATAL_WARNINGS OFF CACHE INTERNAL "Disable fatal warnings")
add_subdirectory(third_party/mbedtls)

# Build AOSL directly here for compatibility with older CMake
set(AOSL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/aosl")
set(AOSL_ADD_INCLUDES_PUBLIC
    "${AOSL_DIR}/include"
    "${AOSL_DIR}/platform/include"
    "${AOSL_DIR}/platform/src/${CONFIG_PLATFORM}/config"
)

# Platform sources
file(GLOB AOSL_PLATFORM_SRCS "${AOSL_DIR}/platform/src/${CONFIG_PLATFORM}/*.c")
set(AOSL_ADD_SOURCES ${AOSL_PLATFORM_SRCS})

# Core sources
list(APPEND AOSL_ADD_SOURCES
    "${AOSL_DIR}/kernel/fileobj.c"
    "${AOSL_DIR}/kernel/iofd.c"
    "${AOSL_DIR}/kernel/timer.c"
    "${AOSL_DIR}/kernel/osmp.c"
    "${AOSL_DIR}/kernel/mpq.c"
    "${AOSL_DIR}/kernel/mpqp.c"
    "${AOSL_DIR}/kernel/refobj.c"
    "${AOSL_DIR}/kernel/file.c"
    "${AOSL_DIR}/kernel/time.c"
    "${AOSL_DIR}/kernel/log.c"
    "${AOSL_DIR}/kernel/panic.c"
    "${AOSL_DIR}/kernel/version.c"
    "${AOSL_DIR}/kernel/thread.c"
    "${AOSL_DIR}/kernel/select_mp.c"
    "${AOSL_DIR}/kernel/poll_mp.c"
    "${AOSL_DIR}/kernel/et_mp.c"
    "${AOSL_DIR}/kernel/rt_monitor.c"

    "${AOSL_DIR}/lib/thread.c"
    "${AOSL_DIR}/lib/atomic.c"
    "${AOSL_DIR}/lib/bitmap.c"
    "${AOSL_DIR}/lib/rbtree.c"
    "${AOSL_DIR}/lib/marshalling.c"
    "${AOSL_DIR}/lib/marshalling-base-obj.c"
    "${AOSL_DIR}/lib/psbuff.c"
    "${AOSL_DIR}/lib/tls.c"
    "${AOSL_DIR}/lib/utils.c"
    "${AOSL_DIR}/lib/cond.c"
    "${AOSL_DIR}/lib/rwlockimpl.c"
    "${AOSL_DIR}/lib/rwlock.c"
    "${AOSL_DIR}/lib/byteswap.c"
    "${AOSL_DIR}/lib/errno.c"

    "${AOSL_DIR}/mm/mm.c"

    "${AOSL_DIR}/net/if.c"
    "${AOSL_DIR}/net/route.c"
    "${AOSL_DIR}/net/sk_utils.c"
    "${AOSL_DIR}/net/inet_ntop.c"
    "${AOSL_DIR}/net/inet_pton.c"
    "${AOSL_DIR}/net/dns.c"

    "${AOSL_DIR}/test/aosl_test.c"
)

# AOSL config
set(CONFIG_AOSL_IPV6 y)
set(CONFIG_AOSL_MEM_STAT n)
set(CONFIG_AOSL_MEM_DUMP n)

if(CONFIG_AOSL_IPV6)
    list(APPEND AOSL_ADD_SOURCES "${AOSL_DIR}/net/ipv6.c")
    add_definitions(-DCONFIG_AOSL_IPV6=y)
endif()

if(NOT CONFIG_AOSL_MEM_STAT STREQUAL n)
    add_definitions(-DCONFIG_AOSL_MEM_STAT=${CONFIG_AOSL_MEM_STAT})
endif()

if(NOT CONFIG_AOSL_MEM_DUMP STREQUAL n)
    add_definitions(-DCONFIG_AOSL_MEM_DUMP=${CONFIG_AOSL_MEM_DUMP})
endif()

add_library(aosl STATIC ${AOSL_ADD_SOURCES})
target_include_directories(aosl PUBLIC ${AOSL_ADD_INCLUDES_PUBLIC})
target_compile_options(aosl PRIVATE -Wno-unused-variable -Wno-unused-function)

# Add link directories for subproject libraries on older CMake/GCC
link_directories(
    ${CMAKE_CURRENT_BINARY_DIR}/third_party/mbedtls/library
)

# Collect source files
file(GLOB_RECURSE TINYRTC_SOURCES
    "src/*.c"
)

# Create library
add_library(tinyrtc STATIC ${TINYRTC_SOURCES})
# AOSL is built as subproject, directly link to the target
target_link_libraries(tinyrtc PUBLIC aosl)

# DTLS/SRTP requires mbedTLS for crypto operations (lightweight, portable)
# Use the submodule version now instead of system libraries
target_link_libraries(tinyrtc PUBLIC mbedtls mbedx509 mbedcrypto)

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
