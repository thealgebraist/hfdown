cmake_minimum_required(VERSION 3.25)
project(hfdown VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Main executable
add_executable(hfdown
    src/main.cpp
    src/http_client.cpp
    src/hf_client.cpp
    src/socket_wrapper.cpp
    src/tls_socket.cpp
    src/http_protocol.cpp
)

target_include_directories(hfdown PRIVATE
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(hfdown PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Cryptos(hfdown PRIVATE
    Threads::Threads
)

# Compiler warnings
if(MSVC)
    target_compile_options(hfdown PRIVATE /W4)
else()
    target_compile_options(hfdown PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Installation
install(TARGETS hfdown DESTINATION bin)
