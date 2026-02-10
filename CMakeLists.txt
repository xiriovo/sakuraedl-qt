cmake_minimum_required(VERSION 3.24)
project(SakuraEDL VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# --- Static build configuration ---
include(cmake/StaticBuild.cmake)

# --- Find Qt6 ---
find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Quick QuickControls2 Network Xml Svg
    SerialPort Concurrent QuickDialogs2
)

# --- Find third-party libraries ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# libusb – local copy in third_party/
set(LIBUSB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libusb")
find_package(LibUSB REQUIRED)

# OpenSSL – statically link MinGW bundled OpenSSL 1.1
# We avoid find_package(OpenSSL) because it may pick up the .dll.a import
# library. Instead, we create IMPORTED STATIC targets directly.
set(OPENSSL_OPT "D:/QT/Tools/mingw1310_64/opt")

add_library(OpenSSL::Crypto STATIC IMPORTED)
set_target_properties(OpenSSL::Crypto PROPERTIES
    IMPORTED_LOCATION "${OPENSSL_OPT}/lib/libcrypto.a"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_OPT}/include"
)

add_library(OpenSSL::SSL STATIC IMPORTED)
set_target_properties(OpenSSL::SSL PROPERTIES
    IMPORTED_LOCATION "${OPENSSL_OPT}/lib/libssl.a"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_OPT}/include"
    INTERFACE_LINK_LIBRARIES OpenSSL::Crypto
)

# zlib shim — provides the 7 zlib symbols OpenSSL static needs
add_subdirectory(third_party/zlib_shim)

# OpenSSL static on Windows needs ws2_32, crypt32, gdi32, and zlib
if(WIN32)
    set_property(TARGET OpenSSL::Crypto APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES zlib_shim ws2_32 crypt32 gdi32)
endif()

# liblzma – statically link MinGW bundled XZ/LZMA
add_library(LZMA::LZMA STATIC IMPORTED)
set_target_properties(LZMA::LZMA PROPERTIES
    IMPORTED_LOCATION "${OPENSSL_OPT}/lib/liblzma.a"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_OPT}/include"
)

# --- Global include directories ---
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- Sub-projects (all static libraries) ---
add_subdirectory(src/core)
add_subdirectory(src/transport)
add_subdirectory(src/common)
add_subdirectory(src/qualcomm)
add_subdirectory(src/mediatek)
add_subdirectory(src/spreadtrum)
add_subdirectory(src/fastboot)
add_subdirectory(third_party/lwext4)

# --- Main application ---
add_subdirectory(src/app)
