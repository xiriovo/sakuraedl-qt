set(TRANSPORT_SOURCES
    usb_transport.cpp
    serial_transport.cpp
    port_detector.cpp
)

# Win32 native serial transport (CreateFileA-based, lower overhead than QSerialPort)
if(WIN32)
    list(APPEND TRANSPORT_SOURCES win32_serial_transport.cpp)
endif()

add_library(sakura_transport STATIC ${TRANSPORT_SOURCES})

target_include_directories(sakura_transport PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(sakura_transport PUBLIC
    sakura_core
    Qt6::Core
    Qt6::SerialPort
    LibUSB::LibUSB
)

if(WIN32)
    target_link_libraries(sakura_transport PUBLIC setupapi hid cfgmgr32)
endif()
