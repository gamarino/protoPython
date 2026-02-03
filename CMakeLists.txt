cmake_minimum_required(VERSION 3.20)
project(protoPython VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find protoCore (assuming it's in the same parent directory)
find_path(PROTOCORE_INCLUDE_DIR NAMES protoCore/ProtoObject.h PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../protoCore/include)

if(NOT PROTOCORE_INCLUDE_DIR)
    message(FATAL_ERROR "protoCore not found. Please ensure it is located at ../protoCore")
endif()

include_directories(include ${PROTOCORE_INCLUDE_DIR})

# Subdirectories
add_subdirectory(src/library)
# add_subdirectory(src/runtime) # Not yet ready
# add_subdirectory(src/compiler) # Not yet ready
