qt_policy(SET QTP0001 NEW)

qt_add_executable(SakuraEDL
    main.cpp
    app_controller.h app_controller.cpp
    qualcomm_controller.h qualcomm_controller.cpp
    mediatek_controller.h mediatek_controller.cpp
    spreadtrum_controller.h spreadtrum_controller.cpp
    fastboot_controller.h fastboot_controller.cpp
)

qt_add_qml_module(SakuraEDL
    URI SakuraEDL
    VERSION 1.0
    QML_FILES
        Main.qml
)

target_link_libraries(SakuraEDL PRIVATE
    # Qt modules
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2
    Qt6::Network Qt6::Xml Qt6::Svg Qt6::Concurrent
    Qt6::SerialPort Qt6::QuickDialogs2

    # Our static libraries
    sakura_core
    sakura_transport
    sakura_common
    sakura_qualcomm
    sakura_mediatek
    sakura_spreadtrum
    sakura_fastboot
    sakura_lwext4

    # Third-party
    LibUSB::LibUSB
    OpenSSL::SSL OpenSSL::Crypto
)

# Windows: link system libraries for static build
if(WIN32)
    target_link_libraries(SakuraEDL PRIVATE ${SAKURA_WIN32_LIBS})
endif()

# Windows: GUI application (no console window)
set_target_properties(SakuraEDL PROPERTIES
    WIN32_EXECUTABLE TRUE
)
