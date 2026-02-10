# FindLibUSB.cmake — Locate libusb-1.0 static library
#
# This module defines:
#   LibUSB_FOUND        - True if libusb was found
#   LibUSB_INCLUDE_DIRS - Include directories
#   LibUSB_LIBRARIES    - Library files
#
# Search paths:
#   LIBUSB_ROOT environment variable or cmake variable
#   third_party/libusb in project root

find_path(LibUSB_INCLUDE_DIR
    NAMES libusb.h libusb-1.0/libusb.h
    PATHS
        ${LIBUSB_ROOT}
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libusb
        $ENV{LIBUSB_ROOT}
    PATH_SUFFIXES include include/libusb-1.0
)

find_library(LibUSB_LIBRARY
    NAMES usb-1.0 libusb-1.0 usb
    PATHS
        ${LIBUSB_ROOT}
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libusb
        $ENV{LIBUSB_ROOT}
    PATH_SUFFIXES lib lib/mingw64 lib/x64
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibUSB
    REQUIRED_VARS LibUSB_LIBRARY LibUSB_INCLUDE_DIR
)

if(LibUSB_FOUND)
    set(LibUSB_INCLUDE_DIRS ${LibUSB_INCLUDE_DIR})
    set(LibUSB_LIBRARIES ${LibUSB_LIBRARY})

    if(NOT TARGET LibUSB::LibUSB)
        add_library(LibUSB::LibUSB UNKNOWN IMPORTED)
        set_target_properties(LibUSB::LibUSB PROPERTIES
            IMPORTED_LOCATION "${LibUSB_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LibUSB_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(LibUSB_INCLUDE_DIR LibUSB_LIBRARY)
