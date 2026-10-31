cmake_minimum_required(VERSION 3.16)
project(LbnCoopSA C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(LBN_BUILD_TESTS "Build unit tests" ON)
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(_lbn_plugin_default ON)
else()
  set(_lbn_plugin_default OFF)
endif()
option(LBN_BUILD_PLUGIN "Build the GTA SA ASI plugin (Windows x86 only)" ${_lbn_plugin_default})

# ---------------- ENet (MIT, vendored in third_party/enet) ----------------
set(ENET_DIR ${CMAKE_SOURCE_DIR}/third_party/enet)
set(ENET_SOURCES
  ${ENET_DIR}/callbacks.c ${ENET_DIR}/compress.c ${ENET_DIR}/host.c ${ENET_DIR}/list.c
  ${ENET_DIR}/packet.c ${ENET_DIR}/peer.c ${ENET_DIR}/protocol.c)
if(WIN32)
  list(APPEND ENET_SOURCES ${ENET_DIR}/win32.c)
else()
  list(APPEND ENET_SOURCES ${ENET_DIR}/unix.c)
endif()
add_library(enet STATIC ${ENET_SOURCES})
target_include_directories(enet PUBLIC ${ENET_DIR}/include)
if(WIN32)
  target_link_libraries(enet PUBLIC ws2_32 winmm)
else()
  target_compile_definitions(enet PRIVATE HAS_SOCKLEN_T=1 HAS_FCNTL=1 HAS_POLL=1 HAS_GETADDRINFO=1
    HAS_GETNAMEINFO=1 HAS_INET_PTON=1 HAS_INET_NTOP=1 HAS_MSGHDR_FLAGS=1)
endif()
set_target_properties(enet PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(enet PRIVATE -w)
endif()

# ---------------- Common (platform independent, unit tested) ----------------
add_library(lbn_common STATIC
  src/common/protocol.cpp src/common/interp.cpp src/common/config.cpp src/common/log.cpp
  src/common/net_common.cpp src/common/net_server.cpp src/common/net_client.cpp)
target_include_directories(lbn_common PUBLIC src/common)
target_link_libraries(lbn_common PUBLIC enet)
set_target_properties(lbn_common PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(lbn_common PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
find_package(Threads REQUIRED)
target_link_libraries(lbn_common PUBLIC Threads::Threads)

set(LBN_STATIC_FLAGS "")
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  set(LBN_STATIC_FLAGS -static -s)
endif()

add_executable(lbn_server src/server/main.cpp)
target_link_libraries(lbn_server PRIVATE lbn_common ${LBN_STATIC_FLAGS})
set_target_properties(lbn_server PROPERTIES OUTPUT_NAME "LbnCoopSA-Server")

add_executable(lbn_testclient src/tools/testclient.cpp)
target_link_libraries(lbn_testclient PRIVATE lbn_common ${LBN_STATIC_FLAGS})

if(LBN_BUILD_TESTS)
  enable_testing()
  add_executable(lbn_tests tests/test_main.cpp tests/test_protocol.cpp tests/test_interp.cpp
    tests/test_config.cpp tests/test_loopback.cpp)
  target_link_libraries(lbn_tests PRIVATE lbn_common ${LBN_STATIC_FLAGS})
  if(NOT WIN32)
    target_compile_definitions(lbn_tests PRIVATE LBN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  endif()
  add_test(NAME lbn_tests COMMAND lbn_tests)
endif()

# ---------------- ASI plugin (Windows x86) ----------------
if(LBN_BUILD_PLUGIN)
  add_library(LbnCoopSA SHARED
    src/plugin/dllmain.cpp src/plugin/game.cpp src/plugin/hooks.cpp src/plugin/script.cpp
    src/plugin/coop.cpp src/plugin/remote_player.cpp src/plugin/overlay.cpp src/plugin/input.cpp)
  target_link_libraries(LbnCoopSA PRIVATE lbn_common user32 kernel32 ${LBN_STATIC_FLAGS})
  set_target_properties(LbnCoopSA PROPERTIES PREFIX "" SUFFIX ".asi" OUTPUT_NAME "LbnCoopSA")
  target_compile_options(LbnCoopSA PRIVATE -Wall -Wno-unused-parameter -fno-strict-aliasing)
  target_link_options(LbnCoopSA PRIVATE -Wl,--kill-at)

  # Fake-game harness (runs under Wine): loads the ASI into a simulated SA 1.0 US address space.
  add_executable(fake_game_harness tests/fake_game_harness.cpp)
  target_link_libraries(fake_game_harness PRIVATE lbn_common ${LBN_STATIC_FLAGS})
  target_link_options(fake_game_harness PRIVATE -Wl,--image-base,0x30000000 -Wl,--disable-dynamicbase)
endif()
