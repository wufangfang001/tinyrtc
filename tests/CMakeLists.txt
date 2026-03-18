# CMakeLists.txt for unit tests

# minunit is just a header, no sources needed

set(TEST_SOURCES
    tests_main.c
    test_rtp.c
    test_sdp.c
    test_jitter_buffer.c
    test_stun.c
)

add_executable(tinyrtc_tests ${TEST_SOURCES})
target_link_libraries(tinyrtc_tests PRIVATE tinyrtc aosl mbedtls mbedx509 mbedcrypto)

# Include directories for minunit and internal headers
target_include_directories(tinyrtc_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/aosl/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/mbedtls/include
)

# Register with CTest
add_test(NAME tinyrtc_tests COMMAND tinyrtc_tests)
