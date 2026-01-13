cmake_minimum_required(VERSION 3.25)
project(hfdown VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Optional QUIC (quiche) support
option(USE_QUIC "Enable QUIC (quiche) support" OFF)
if(USE_QUIC)
    if(NOT DEFINED QUICHE_ROOT)
        message(FATAL_ERROR "USE_QUIC=ON but QUICHE_ROOT not set. Pass -DQUICHE_ROOT=/path/to/quiche/build")
    endif()
    set(QUICHE_INCLUDE_DIR ${QUICHE_ROOT}/include)
    set(QUICHE_LIB ${QUICHE_ROOT}/lib/libquiche.a)
    if(NOT EXISTS ${QUICHE_INCLUDE_DIR})
        message(FATAL_ERROR "quiche include dir not found: ${QUICHE_INCLUDE_DIR}")
    endif()
    if(NOT EXISTS ${QUICHE_LIB})
        message(FATAL_ERROR "quiche lib not found: ${QUICHE_LIB}")
    endif()
endif()
# On macOS with Homebrew, CMake may not find OpenSSL automatically.
if(NOT OPENSSL_FOUND)
    set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl@3")
    find_package(OpenSSL REQUIRED)
endif()

# Main executable
add_executable(hfdown
    src/main.cpp
    src/http_client.cpp
    src/hf_client.cpp
    src/kaggle_client.cpp
    src/cache_manager.cpp
    src/github_client.cpp
    src/file_monitor.cpp
    src/git_uploader.cpp
    src/secret_scanner.cpp
    src/quic_socket.cpp
    src/http3_client.cpp
    src/socket_wrapper.cpp
    src/tls_socket.cpp
    src/http_protocol.cpp
)

target_include_directories(hfdown PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(hfdown PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

if(USE_QUIC)
    target_include_directories(hfdown PRIVATE ${QUICHE_INCLUDE_DIR})
    target_link_libraries(hfdown PRIVATE ${QUICHE_LIB})
    target_compile_definitions(hfdown PRIVATE USE_QUIC)
    message(STATUS "QUIC (quiche) enabled. Include: ${QUICHE_INCLUDE_DIR}, Lib: ${QUICHE_LIB}")
else()
    message(STATUS "QUIC (quiche) disabled. Build with -DUSE_QUIC=ON -DQUICHE_ROOT=... to enable")
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(hfdown PRIVATE /W4)
else()
    target_compile_options(hfdown PRIVATE -Wall -Wextra -Wpedantic)
endif()
