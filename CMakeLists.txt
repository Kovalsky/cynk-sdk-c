cmake_minimum_required(VERSION 3.16)
project(cynk_sdk_c LANGUAGES C)

# ---------------------------------------------------------------------------
# Protocol library (internal — used by tests, examples, and the public API)
# ---------------------------------------------------------------------------
add_library(cynk_protocol STATIC
  src/cynk_protocol.c
  third_party/jsmn/jsmn.c
)

target_include_directories(cynk_protocol
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/jsmn
)

target_compile_features(cynk_protocol PUBLIC c_std_99)

# ---------------------------------------------------------------------------
# Public API library (requires a platform MQTT adapter to link)
# ---------------------------------------------------------------------------
add_library(cynk STATIC
  src/cynk.c
)

target_include_directories(cynk
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(cynk PUBLIC cynk_protocol)
target_compile_features(cynk PUBLIC c_std_99)

# ---------------------------------------------------------------------------
# Mosquitto platform adapter (desktop / Linux / Mac)
# ---------------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(MOSQUITTO QUIET libmosquitto)
endif()

if(MOSQUITTO_FOUND)
  target_sources(cynk PRIVATE src/platform/mqtt_mosquitto.c)
  target_include_directories(cynk PRIVATE ${MOSQUITTO_INCLUDE_DIRS})
  target_link_libraries(cynk PUBLIC ${MOSQUITTO_LIBRARIES})

  # Quick-start example using the public API (requires Mosquitto)
  add_executable(cynk_quick_start examples/quick_start.c)
  target_link_libraries(cynk_quick_start PRIVATE cynk)

  message(STATUS "libmosquitto found -- building full SDK with Mosquitto adapter")
else()
  message(STATUS "libmosquitto not found -- cynk library needs a platform adapter at link time")
endif()

# ---------------------------------------------------------------------------
# Protocol-level example (no platform adapter needed)
# ---------------------------------------------------------------------------
add_executable(cynk_device_example
  examples/basic_device.c
)
target_link_libraries(cynk_device_example PRIVATE cynk_protocol)

# ---------------------------------------------------------------------------
# Protocol-level tests (no platform adapter needed)
# ---------------------------------------------------------------------------
add_executable(cynk_device_tests
  tests/test_device.c
)
target_link_libraries(cynk_device_tests PRIVATE cynk_protocol)

enable_testing()
add_test(NAME cynk_device_tests COMMAND cynk_device_tests)
