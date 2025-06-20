cmake_minimum_required(VERSION 3.10)
project(blake3 C)

add_library(blake3 STATIC
    blake3.c
    blake3_dispatch.c
    blake3_portable.c
)

target_include_directories(blake3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(blake3 PUBLIC
    BLAKE3_NO_AVX2=1
    BLAKE3_NO_AVX512=1
    BLAKE3_NO_SSE2=1
    BLAKE3_NO_SSE41=1
    BLAKE3_USE_NEON=0
)
