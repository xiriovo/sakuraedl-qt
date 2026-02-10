add_library(sakura_common STATIC
    gpt_parser.cpp
    sparse_stream.cpp
    hdlc_codec.cpp
    crc_utils.cpp
    lz4_decoder.cpp
    lzma_decoder.cpp
    partition_info.cpp
    ext4_parser.cpp
    erofs_parser.cpp
)

target_include_directories(sakura_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(sakura_common PUBLIC
    sakura_core
    Qt6::Core
    LZMA::LZMA
)
