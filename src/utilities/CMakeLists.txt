cmake_minimum_required(VERSION 3.10)

# Library for common utilities (SimpliDFS_Utils, now includes BlockIO functionality)
project(SimpliDFSUtils CXX) # Specify language for project command
add_library(SimpliDFS_Utils
    logger.cpp
    filesystem.cpp
    client.cpp
    server.cpp
    errorcodes.cpp
    message.cpp
    cid_utils.cpp  # Added cid_utils.cpp
    blockio.cpp    # Moved from BlockIOLib
)
target_include_directories(SimpliDFS_Utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include # For all public headers in include/utilities
        ${cppcodec_SOURCE_DIR} # For cppcodec
        ${SODIUM_INCLUDE_DIRS} # For libsodium
)

# Add ZSTD include directories for SimpliDFS_Utils
if(ZSTD_INCLUDE_DIRS) # If ZSTD was found by find_package
    target_include_directories(SimpliDFS_Utils PUBLIC ${ZSTD_INCLUDE_DIRS})
endif()
if(zstd_SOURCE_DIR) # If zstd was fetched by FetchContent (zstd_SOURCE_DIR is defined)
    target_include_directories(SimpliDFS_Utils PUBLIC ${zstd_SOURCE_DIR}/lib)
endif()

# This allows consumers of SimpliDFS_Utils to find all necessary headers.

# Link dependencies to SimpliDFS_Utils
target_link_libraries(SimpliDFS_Utils
    PUBLIC
        cppcodec
        ${SODIUM_LIBRARIES}
)

# Link ZSTD to SimpliDFS_Utils (similar to how it was done for BlockIOLib)
if(TARGET ZSTD::zstd)
    target_link_libraries(SimpliDFS_Utils PUBLIC ZSTD::zstd)
elseif(TARGET zstd::zstd_shared)
    target_link_libraries(SimpliDFS_Utils PUBLIC zstd::zstd_shared)
elseif(TARGET libzstd_shared)
    target_link_libraries(SimpliDFS_Utils PUBLIC libzstd_shared)
elseif(TARGET zstd_shared)
    target_link_libraries(SimpliDFS_Utils PUBLIC zstd_shared)
elseif(TARGET zstd::zstd_static)
    target_link_libraries(SimpliDFS_Utils PUBLIC zstd::zstd_static)
elseif(TARGET libzstd_static)
    target_link_libraries(SimpliDFS_Utils PUBLIC libzstd_static)
elseif(TARGET zstd_static)
    target_link_libraries(SimpliDFS_Utils PUBLIC zstd_static)
elseif(TARGET zstd)
    target_link_libraries(SimpliDFS_Utils PUBLIC zstd)
else()
    message(WARNING "zstd target not found for linking to SimpliDFS_Utils. ZSTD_FOUND=${ZSTD_FOUND}")
endif()
