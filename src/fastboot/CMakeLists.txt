add_library(sakura_fastboot STATIC
    # Protocol layer
    protocol/fastboot_protocol.cpp
    protocol/fastboot_client.cpp

    # Services
    services/fastboot_service.cpp

    # Parsers
    parsers/sparse_image.cpp
    parsers/payload_parser.cpp

    # Vendor-specific
    vendor/huawei_honor.cpp
)

target_include_directories(sakura_fastboot PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(sakura_fastboot PUBLIC
    sakura_common
    sakura_transport
    Qt6::Core
    Qt6::Network
)
