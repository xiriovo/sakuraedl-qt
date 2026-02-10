# ---------------------------------------------------------------------------
# lwext4 – Lightweight ext2/3/4 filesystem library
#
# This wraps the lwext4 C sources as a static library for use by SakuraEDL.
#
# If the full lwext4 source tree is available at the path below, it will be
# compiled directly.  Otherwise a minimal stub is provided so the CMake
# target exists and dependents can link without error.
# ---------------------------------------------------------------------------

set(LWEXT4_SOURCE_DIR "D:/code/qt/edl2/Libs/lwext4")

# Check whether the real source tree is available
if(EXISTS "${LWEXT4_SOURCE_DIR}/src/ext4.c")
    message(STATUS "lwext4: using source tree at ${LWEXT4_SOURCE_DIR}")

    file(GLOB LWEXT4_SOURCES
        "${LWEXT4_SOURCE_DIR}/src/*.c"
    )

    add_library(sakura_lwext4 STATIC ${LWEXT4_SOURCES})

    target_include_directories(sakura_lwext4 PUBLIC
        "${LWEXT4_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}"     # for local config overrides
    )

    # lwext4 config header – provide a default if none exists
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ext4_config.h")
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ext4_config.h"
            "#pragma once\n"
            "#define CONFIG_USE_DEFAULT_CFG 1\n"
            "#define CONFIG_BLOCK_DEV_CACHE_SIZE 16\n"
        )
        target_include_directories(sakura_lwext4 PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    # Suppress common C warnings in third-party code
    if(MSVC)
        target_compile_options(sakura_lwext4 PRIVATE /W0)
    else()
        target_compile_options(sakura_lwext4 PRIVATE -w)
    endif()

else()
    message(STATUS "lwext4: source tree not found – building stub target")

    # Build from local stub so the CMake target exists
    add_library(sakura_lwext4 STATIC
        lwext4_stub.c
    )

    target_include_directories(sakura_lwext4 PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
