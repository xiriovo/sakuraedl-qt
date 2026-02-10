add_library(zlib_shim STATIC zlib_shim.c)
target_include_directories(zlib_shim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
