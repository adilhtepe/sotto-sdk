# JNI library for the Kotlin API (vban.sdk.*). Builds the SDK from source.
# Used by the Android Gradle module and, for the JVM unit tests, on the host.
cmake_minimum_required(VERSION 3.13)
project(vban_jni C)

set(VBAN_SDK_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../..)
set(VBAN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(VBAN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VBAN_BUILD_BENCH OFF CACHE BOOL "" FORCE)
add_subdirectory(${VBAN_SDK_ROOT} vban_sdk)

add_library(vban_jni SHARED vban_jni.c)
target_link_libraries(vban_jni PRIVATE vban)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vban_jni PRIVATE -Wall -Wextra -fvisibility=hidden)
    # export only the JNI entry points, not the embedded SDK (no clashes with
    # another library that embeds it, smaller .so)
    target_link_options(vban_jni PRIVATE -Wl,--exclude-libs,ALL -Wl,--gc-sections)
endif()

if(ANDROID)
    # 16 KB page alignment: required for apps targeting Android 15+ devices
    target_link_options(vban_jni PRIVATE -Wl,-z,max-page-size=16384)
else()
    find_package(JNI REQUIRED COMPONENTS JVM)  # headers only; no AWT
    target_include_directories(vban_jni PRIVATE ${JNI_INCLUDE_DIRS})
endif()
