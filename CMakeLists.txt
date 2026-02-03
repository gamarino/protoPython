cmake_minimum_required(VERSION 3.20)
project(protoPython VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include protoCore (assuming it's in the same parent directory)
if(NOT TARGET protoCore)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../protoCore ${CMAKE_CURRENT_BINARY_DIR}/protoCore)
endif()

include_directories(include)

# Subdirectories
add_subdirectory(src/library)

enable_testing()
add_subdirectory(test/library)
# add_subdirectory(src/runtime) # Not yet ready
# add_subdirectory(src/compiler) # Not yet ready
