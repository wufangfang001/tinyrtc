# Demo applications CMakeLists

# Common utilities
add_library(demo_common STATIC common.c)
target_link_libraries(demo_common tinyrtc aosl)

# Simple send demo (sends media to browser)
add_executable(tinyrtc_send demo_sender.c)
target_link_libraries(tinyrtc_send demo_common tinyrtc aosl)

# Simple receive demo (receives media from browser)
add_executable(tinyrtc_recv demo_receiver.c)
target_link_libraries(tinyrtc_recv demo_common tinyrtc aosl)
