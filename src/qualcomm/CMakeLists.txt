add_library(sakura_qualcomm STATIC
    # Protocol layer
    protocol/sahara_protocol.cpp
    protocol/firehose_client.cpp
    protocol/diag_client.cpp

    # Services
    services/qualcomm_service.cpp
    services/cloud_loader_service.cpp
    services/provision_service.cpp

    # Auth strategies
    auth/oneplus_auth.cpp
    auth/xiaomi_auth.cpp
    auth/vip_auth.cpp

    # Database
    database/qualcomm_chip_db.cpp

    # Parsers
    parsers/rawprogram_parser.cpp
    parsers/loader_feature_detector.cpp
    parsers/lp_metadata_parser.cpp
    parsers/motorola_support.cpp

    # Exploit
    exploit/pbl_exploit.cpp
)

target_include_directories(sakura_qualcomm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(sakura_qualcomm PUBLIC
    sakura_common
    sakura_transport
    Qt6::Core
    Qt6::Network
    Qt6::Xml
    OpenSSL::Crypto
)
