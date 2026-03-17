# Demo applications CMakeLists

# Add link directories for subproject libraries on older CMake/GCC
link_directories(
    ${CMAKE_CURRENT_BINARY_DIR}/../third_party/aosl
    ${CMAKE_CURRENT_BINARY_DIR}/../third_party/mbedtls/library
)

# Common utilities
add_library(demo_common STATIC common.c)
target_link_libraries(demo_common PUBLIC tinyrtc aosl)

# Simple send demo (sends media to browser)
add_executable(tinyrtc_send demo_sender.c)
target_link_libraries(tinyrtc_send PRIVATE demo_common tinyrtc aosl)

# Simple receive demo (receives media from browser)
add_executable(tinyrtc_recv demo_receiver.c)
target_link_libraries(tinyrtc_recv PRIVATE demo_common tinyrtc aosl)

# Add pthread for Linux threading
if(CONFIG_PLATFORM STREQUAL "linux")
    target_link_libraries(tinyrtc_send PRIVATE pthread dl rt m)
    target_link_libraries(tinyrtc_recv PRIVATE pthread dl rt m)
endif()
