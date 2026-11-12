# Cross-compile toolchain for MinGW-w64 on Linux (used by ci/run-local.sh).
# Pass -DMINGW_TRIPLE=i686-w64-mingw32 for 32-bit; default is x86_64.
set(CMAKE_SYSTEM_NAME Windows)
if(NOT MINGW_TRIPLE)
    set(MINGW_TRIPLE x86_64-w64-mingw32)
endif()
set(CMAKE_C_COMPILER   ${MINGW_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${MINGW_TRIPLE}-g++)
set(CMAKE_RC_COMPILER  ${MINGW_TRIPLE}-windres)
set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# Static runtime so the test .exe runs under Wine without DLL hunting.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
