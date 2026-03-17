# Demo applications CMakeLists

# Common utilities
add_library(demo_common STATIC common.c)
target_link_libraries(demo_common PUBLIC tinyrtc)

# Simple send demo (sends media to browser)
add_executable(tinyrtc_send demo_sender.c)
target_link_libraries(tinyrtc_send PRIVATE demo_common tinyrtc)

# Simple receive demo (receives media from browser)
add_executable(tinyrtc_recv demo_receiver.c)
target_link_libraries(tinyrtc_recv PRIVATE demo_common tinyrtc)

# Add pthread for Linux threading
if(CONFIG_PLATFORM STREQUAL "linux")
    target_link_libraries(tinyrtc_send PRIVATE pthread dl rt m)
    target_link_libraries(tinyrtc_recv PRIVATE pthread dl rt m)
endif()
