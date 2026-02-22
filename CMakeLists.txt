cmake_minimum_required(VERSION 3.16)
project(cynk_device_sdk_c LANGUAGES C)

add_library(cynk_device STATIC
  src/cynk_device.c
  third_party/jsmn/jsmn.c
)

target_include_directories(cynk_device
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/jsmn
)

target_compile_features(cynk_device PUBLIC c_std_99)

add_executable(cynk_device_example
  examples/basic_device.c
)
target_link_libraries(cynk_device_example PRIVATE cynk_device)

add_executable(cynk_device_tests
  tests/test_device.c
)
target_link_libraries(cynk_device_tests PRIVATE cynk_device)

enable_testing()
add_test(NAME cynk_device_tests COMMAND cynk_device_tests)
