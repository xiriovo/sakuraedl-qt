add_library(sakura_core STATIC
    logger.cpp
    language_manager.cpp
    watchdog.cpp
    performance_config.cpp
)

target_include_directories(sakura_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(sakura_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Concurrent
)
