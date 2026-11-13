cmake_minimum_required(VERSION 3.13)

# ESP-IDF component mode
if(ESP_PLATFORM)
    idf_component_register(
        SRCS src/vban_proto.c src/vban_pcm.c src/vban_jitter.c src/vban_mixer.c
             src/vban_rx.c src/vban_tx.c src/vban_net.c
             src/vban_noise.c src/vban_secure.c third_party/monocypher/monocypher.c
        INCLUDE_DIRS src third_party/monocypher)
    target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -fno-math-errno -fno-trapping-math)
    return()
endif()

project(vban_sdk VERSION 1.0.0 LANGUAGES C CXX)

option(VBAN_BUILD_TESTS     "Build unit / integration tests" ON)
option(VBAN_BUILD_EXAMPLES  "Build example tools"            ON)
option(VBAN_BUILD_BENCH     "Build benchmarks"               ON)
option(VBAN_ENABLE_NET      "Build the socket layer"         ON)
option(VBAN_SANITIZE        "Build with ASan + UBSan"        OFF)
option(VBAN_WERROR          "Treat compiler warnings as errors (CI)" OFF)
option(VBAN_ENABLE_SECURE   "Secure VBAN (certificates, Noise XX, ChaCha20-Poly1305; Monocypher)" ON)

set(VBAN_SOURCES
    src/vban_proto.c src/vban_pcm.c src/vban_jitter.c src/vban_mixer.c
    src/vban_rx.c src/vban_tx.c src/vban_net.c)

if(VBAN_ENABLE_SECURE)
    list(APPEND VBAN_SOURCES src/vban_noise.c src/vban_secure.c)
    # Vendored, unmodified (third_party/monocypher/VERSION): built with its own flags, no -Wpedantic noise.
    add_library(vban_monocypher OBJECT third_party/monocypher/monocypher.c)
    set_target_properties(vban_monocypher PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
    target_include_directories(vban_monocypher PUBLIC third_party/monocypher)
    list(APPEND VBAN_SOURCES $<TARGET_OBJECTS:vban_monocypher>)
endif()

add_library(vban STATIC ${VBAN_SOURCES})
if(WIN32 AND VBAN_ENABLE_SECURE)
    target_link_libraries(vban PUBLIC bcrypt)   # BCryptGenRandom
endif()
add_library(vban::vban ALIAS vban)
target_include_directories(vban PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/monocypher>
    $<INSTALL_INTERFACE:include>)
set_target_properties(vban PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
if(NOT VBAN_ENABLE_NET)
    target_compile_definitions(vban PUBLIC VBAN_ENABLE_NET=0)
endif()

# Warning flags shared by the library, tests, examples and bench.
if(MSVC)
    set(VBAN_WARN /W4)
    if(VBAN_WERROR)
        list(APPEND VBAN_WARN /WX)
    endif()
else()
    set(VBAN_WARN -Wall -Wextra -Wpedantic)
    if(VBAN_WERROR)
        list(APPEND VBAN_WARN -Werror)
    endif()
endif()

if(MSVC)
    # Default /fp:precise, not /fp:fast: fast-math folds `v == v` to true and
    # lets hostile NaN samples past the float sanitizer in vban_pcm.c (clang-cl
    # maps /fp:fast to -ffast-math). MSVC never traps on FP by default.
    target_compile_options(vban PRIVATE ${VBAN_WARN})
    target_link_libraries(vban PUBLIC ws2_32)
else()
    target_compile_options(vban PRIVATE ${VBAN_WARN} -fno-math-errno -fno-trapping-math)
    if(WIN32)
        target_link_libraries(vban PUBLIC ws2_32)
    else()
        # Probe by linking rather than find_library(): the latter misses libm
        # in cross toolchains without CMAKE_FIND_ROOT_PATH and silently drops -lm.
        include(CheckLibraryExists)
        check_library_exists(m lrintf "" VBAN_HAVE_LIBM)
        if(VBAN_HAVE_LIBM)
            target_link_libraries(vban PUBLIC m)
        endif()
        find_package(Threads)
        if(Threads_FOUND)
            target_link_libraries(vban PUBLIC Threads::Threads)
        endif()
    endif()
endif()

if(VBAN_SANITIZE AND NOT MSVC)
    target_compile_options(vban PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(vban PUBLIC -fsanitize=address,undefined)
endif()

if(VBAN_BUILD_TESTS)
    enable_testing()
    add_executable(vban_tests tests/test_main.c)
    target_link_libraries(vban_tests PRIVATE vban)
    target_compile_options(vban_tests PRIVATE ${VBAN_WARN})
    add_test(NAME vban_tests COMMAND vban_tests)
    add_executable(vban_cpp_test tests/test_cpp.cpp)
    set_target_properties(vban_cpp_test PROPERTIES CXX_STANDARD 11)
    target_link_libraries(vban_cpp_test PRIVATE vban)
    target_compile_options(vban_cpp_test PRIVATE ${VBAN_WARN})
    add_test(NAME vban_cpp_test COMMAND vban_cpp_test)
    # kernel ring (shared/vban_kring.h, used by sotto-linux) vs SDK jitter buffer on simulated networks
    add_executable(vban_parity tests/test_parity.c)
    target_link_libraries(vban_parity PRIVATE vban)
    target_compile_options(vban_parity PRIVATE ${VBAN_WARN})
    add_test(NAME vban_parity COMMAND vban_parity)
    if(VBAN_ENABLE_SECURE)
        add_executable(vban_secure_test tests/test_secure.c)
        target_link_libraries(vban_secure_test PRIVATE vban)
        target_compile_options(vban_secure_test PRIVATE ${VBAN_WARN})
        add_test(NAME vban_secure_test COMMAND vban_secure_test)
    endif()
endif()

if(VBAN_BUILD_EXAMPLES AND VBAN_ENABLE_NET)
    set(VBAN_EXAMPLES vban_send vban_recv vban_ping vban_text vban_mixer_demo vban_monitor)
    if(VBAN_ENABLE_SECURE)
        list(APPEND VBAN_EXAMPLES vban_cert)
    endif()
    foreach(ex ${VBAN_EXAMPLES})
        add_executable(${ex} examples/${ex}.c)
        target_link_libraries(${ex} PRIVATE vban)
        target_compile_options(${ex} PRIVATE ${VBAN_WARN})
    endforeach()
endif()

if(VBAN_BUILD_BENCH AND VBAN_ENABLE_NET)
    add_executable(vban_bench bench/bench.c)
    target_link_libraries(vban_bench PRIVATE vban)
    target_compile_options(vban_bench PRIVATE ${VBAN_WARN})
    add_executable(vban_netsim bench/netsim.c)
    target_link_libraries(vban_netsim PRIVATE vban)
    target_compile_options(vban_netsim PRIVATE ${VBAN_WARN})
endif()

install(TARGETS vban EXPORT vban-targets ARCHIVE DESTINATION lib)
install(DIRECTORY src/ DESTINATION include FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
install(EXPORT vban-targets NAMESPACE vban:: DESTINATION lib/cmake/vban)
