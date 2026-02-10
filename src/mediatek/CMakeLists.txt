add_library(sakura_mediatek STATIC
    protocol/brom_client.cpp
    protocol/xflash_client.cpp
    protocol/xml_da_client.cpp
    protocol/da_loader.cpp
    services/mediatek_service.cpp
    # exploit/brom_exploit_framework.cpp   # DISABLED — exploit not ready
    # exploit/carbonara_exploit.cpp        # DISABLED — exploit not ready
    # exploit/kamakiri2_exploit.cpp        # DISABLED — exploit not ready (requires libusb)
    auth/mtk_sla_auth.cpp
    auth/cloud_signing_service.cpp
    database/mtk_chip_database.cpp
)

target_include_directories(sakura_mediatek PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(sakura_mediatek PUBLIC
    sakura_common
    sakura_transport
    Qt6::Core
    Qt6::Network
    Qt6::Xml
    OpenSSL::Crypto
)
