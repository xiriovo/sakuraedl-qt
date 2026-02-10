# StaticBuild.cmake — Configure static linking for single-exe deployment

option(SAKURA_STATIC_BUILD "Build as a fully static executable" ON)

if(SAKURA_STATIC_BUILD)
    message(STATUS "SakuraEDL: Static build enabled")

    if(MINGW)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")

        # Size optimization: -Os for smaller code, gc-sections to remove dead code
        string(REPLACE "-O2" "-Os" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
        string(REPLACE "-O3" "-Os" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Os -ffunction-sections -fdata-sections" CACHE STRING "" FORCE)
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Os -ffunction-sections -fdata-sections" CACHE STRING "" FORCE)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections -Wl,--strip-all -Wl,-s")
    endif()

    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".lib")
    set(BUILD_SHARED_LIBS OFF)
endif()

if(WIN32)
    set(SAKURA_WIN32_LIBS
        ws2_32 winmm setupapi hid
        iphlpapi userenv advapi32 shell32 ole32 oleaut32
        uuid comdlg32 gdi32 user32 kernel32
    )
endif()
