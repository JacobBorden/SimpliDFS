cmake_minimum_required(VERSION 3.10)

# Library for BlockIO
project(BlockIOLib CXX) # Specify language for project command
add_library(BlockIOLib
    blockio.cpp
)
target_include_directories(BlockIOLib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${SODIUM_INCLUDE_DIRS}
)
# This allows consumers of BlockIOLib to find utilities/blockio.hpp

# Link libsodium to BlockIOLib
# target_link_libraries(BlockIOLib PUBLIC Sodium::libsodium) # Replaced by PkgConfig method
target_link_libraries(BlockIOLib PUBLIC ${SODIUM_LIBRARIES})

# Library for other common utilities
project(SimpliDFSUtils CXX) # Specify language for project command
add_library(SimpliDFS_Utils
    logger.cpp
    filesystem.cpp
    client.cpp
    server.cpp
    errorcodes.cpp
    message.cpp
)
target_include_directories(SimpliDFS_Utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
# This allows consumers of SimpliDFS_Utils to find, e.g., utilities/logger.h, utilities/message.h etc.

# Optional: If SimpliDFS_Utils uses BlockIOLib, you might add:
# target_link_libraries(SimpliDFS_Utils PRIVATE BlockIOLib)
# For now, assume they are independent utility libraries.
