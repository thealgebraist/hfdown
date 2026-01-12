cmake_minimum_required(VERSION 3.25)
project(hfdown VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(CURL REQUIRED)

# Main executable
add_executable(hfdown
    src/main.cpp
    src/http_client.cpp
    src/hf_client.cpp
)

target_include_directories(hfdown PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CURL_INCLUDE_DIRS}
)

target_link_libraries(hfdown PRIVATE
    ${CURL_LIBRARIES}
)

# Compiler warnings
if(MSVC)
    target_compile_options(hfdown PRIVATE /W4)
else()
    target_compile_options(hfdown PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Installation
install(TARGETS hfdown DESTINATION bin)
