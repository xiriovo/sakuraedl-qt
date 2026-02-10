add_library(sakura_spreadtrum STATIC
    protocol/fdl_client.cpp
    protocol/hdlc_protocol.cpp
    protocol/sprd_diag_client.cpp
    services/spreadtrum_service.cpp
    parsers/pac_parser.cpp
    parsers/boot_parser.cpp
    exploit/sprd_exploit.cpp
    database/sprd_fdl_database.cpp
)

target_include_directories(sakura_spreadtrum PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(sakura_spreadtrum PUBLIC
    sakura_common
    sakura_transport
    Qt6::Core
    Qt6::Network
)
