cmake_minimum_required(VERSION 3.25)
project(hfdown VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization and Specialization Flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -flto -ffunction-sections -fdata-sections)
    if(APPLE)
        add_link_options("-Wl,-dead_strip" -flto)
    else()
        add_link_options("-Wl,--gc-sections" -flto)
    endif()
endif()

# Find required packages
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)

# Try to detect libuv (prefer pkg-config, fall back to Homebrew Cellar paths)
pkg_check_modules(LIBUV QUIET libuv)
if(NOT LIBUV_FOUND)
    if(NOT DEFINED LIBUV_ROOT)
        if(EXISTS "/opt/homebrew/Cellar/libuv")
            file(GLOB _uv_versions "/opt/homebrew/Cellar/libuv/*")
            list(SORT _uv_versions)
            list(REVERSE _uv_versions)
            list(GET _uv_versions 0 LIBUV_ROOT)
        elseif(EXISTS "/opt/homebrew")
            set(LIBUV_ROOT "/opt/homebrew")
        endif()
    endif()

    if(DEFINED LIBUV_ROOT)
        if(EXISTS ${LIBUV_ROOT}/include)
            set(LIBUV_INCLUDE_DIRS ${LIBUV_ROOT}/include)
        endif()
        if(EXISTS ${LIBUV_ROOT}/lib/libuv.a)
            set(LIBUV_LIBRARIES ${LIBUV_ROOT}/lib/libuv.a)
        elseif(EXISTS ${LIBUV_ROOT}/lib/libuv.dylib)
            set(LIBUV_LIBRARIES ${LIBUV_ROOT}/lib/libuv.dylib)
        elseif(EXISTS ${LIBUV_ROOT}/lib)
            # As a last resort use the bare -luv token; prefer absolute paths above
            set(LIBUV_LIBRARIES -luv)
        endif()
    endif()
endif()

if(LIBUV_FOUND OR DEFINED LIBUV_LIBRARIES)
    message(STATUS "Found libuv: ${LIBUV_LIBRARIES}")
else()
    message(STATUS "libuv not found; some test targets may fail to link.")
endif()

# If pkg-config returned bare lib tokens (e.g. 'uv') prefer explicit Homebrew library paths
if(LIBUV_FOUND AND (NOT LIBUV_LIBRARIES MATCHES "/"))
    if(EXISTS "/opt/homebrew/lib/libuv.dylib")
        set(LIBUV_LIBRARIES "/opt/homebrew/lib/libuv.dylib")
        message(STATUS "Overriding LIBUV_LIBRARIES with Homebrew dylib: ${LIBUV_LIBRARIES}")
    elseif(EXISTS "/opt/homebrew/lib/libuv.a")
        set(LIBUV_LIBRARIES "/opt/homebrew/lib/libuv.a")
        message(STATUS "Overriding LIBUV_LIBRARIES with Homebrew static: ${LIBUV_LIBRARIES}")
    else()
        # Add Homebrew lib dir to link flags as a fallback
        set(LIBUV_LIBRARIES "-L/opt/homebrew/lib -luv")
        message(STATUS "Using -L/opt/homebrew/lib -luv as fallback for libuv")
    endif()
endif()

# Optional NGTCP2 support
option(USE_NGTCP2 "Enable QUIC (ngtcp2 + nghttp3) support" OFF)
if(USE_NGTCP2)
    # Try pkg-config first
    pkg_check_modules(NGTCP2 libngtcp2)
    pkg_check_modules(NGHTTP3 libnghttp3)
    pkg_check_modules(NGTCP2_CRYPTO_OSSL libngtcp2_crypto_ossl)
    pkg_check_modules(NGTCP2_CRYPTO_GNUTLS libngtcp2_crypto_gnutls)

    if(NGTCP2_CRYPTO_OSSL_FOUND)
        set(NGTCP2_CRYPTO_FOUND TRUE)
        set(NGTCP2_CRYPTO_INCLUDE_DIRS ${NGTCP2_CRYPTO_OSSL_INCLUDE_DIRS})
        set(NGTCP2_CRYPTO_LIBRARIES ${NGTCP2_CRYPTO_OSSL_LIBRARIES})
        set(NGTCP2_LIBRARIES ${NGTCP2_LIBRARIES})
        add_definitions(-DUSE_NGTCP2_CRYPTO_OSSL)
        link_directories(${NGTCP2_LIBRARY_DIRS} ${NGTCP2_CRYPTO_OSSL_LIBRARY_DIRS})
    elseif(NGTCP2_CRYPTO_GNUTLS_FOUND)
        set(NGTCP2_CRYPTO_FOUND TRUE)
        set(NGTCP2_CRYPTO_INCLUDE_DIRS ${NGTCP2_CRYPTO_GNUTLS_INCLUDE_DIRS})
        set(NGTCP2_CRYPTO_LIBRARIES ${NGTCP2_CRYPTO_GNUTLS_LIBRARIES})
        add_definitions(-DUSE_NGTCP2_CRYPTO_GNUTLS)
        find_package(GnuTLS REQUIRED)
        list(APPEND NGTCP2_CRYPTO_INCLUDE_DIRS ${GNUTLS_INCLUDE_DIR})
        list(APPEND NGTCP2_CRYPTO_LIBRARIES ${GNUTLS_LIBRARIES})
    endif()

    # Fallback search paths
    set(NGTCP2_SEARCH_PATHS
        ${NGTCP2_ROOT}
        ${NGTCP2_INCLUDE_DIR}/..
        /opt/homebrew
        /usr/local
        /usr
    )

    if(NOT NGTCP2_FOUND)
        find_path(NGTCP2_INCLUDE_DIRS NAMES ngtcp2/ngtcp2.h PATHS ${NGTCP2_SEARCH_PATHS} PATH_SUFFIXES include)
        find_library(NGTCP2_LIBRARIES NAMES ngtcp2 PATHS ${NGTCP2_SEARCH_PATHS} PATH_SUFFIXES lib)
    endif()

    if(NOT NGHTTP3_FOUND)
        find_path(NGHTTP3_INCLUDE_DIRS NAMES nghttp3/nghttp3.h PATHS ${NGTCP2_SEARCH_PATHS} PATH_SUFFIXES include)
        find_library(NGHTTP3_LIBRARIES NAMES nghttp3 PATHS ${NGTCP2_SEARCH_PATHS} PATH_SUFFIXES lib)
    endif()

    if(NOT NGTCP2_CRYPTO_FOUND)
        find_path(NGTCP2_CRYPTO_INCLUDE_DIRS NAMES ngtcp2/ngtcp2_crypto.h PATHS ${NGTCP2_SEARCH_PATHS} PATH_SUFFIXES include)
        find_library(NGTCP2_CRYPTO_LIBRARIES NAMES ngtcp2_crypto_ossl PATHS ${NGTCP2_SEARCH_PATHS} PATH_SUFFIXES lib)
    endif()

    # Consolidate include directories - very important for Linux if headers are split
    set(NGTCP2_ALL_INCLUDE_DIRS 
        ${NGTCP2_INCLUDE_DIRS} 
        ${NGHTTP3_INCLUDE_DIRS} 
        ${NGTCP2_CRYPTO_INCLUDE_DIRS}
        ${NGTCP2_ROOT}/include
    )
    if(NGTCP2_ALL_INCLUDE_DIRS)
        list(REMOVE_DUPLICATES NGTCP2_ALL_INCLUDE_DIRS)
    endif()

    # Consolidate libraries
    set(NGTCP2_ALL_LIBRARIES ${NGTCP2_LIBRARIES} ${NGHTTP3_LIBRARIES} ${NGTCP2_CRYPTO_LIBRARIES})
    link_directories(${NGTCP2_LIBRARY_DIRS} ${NGHTTP3_LIBRARY_DIRS} ${NGTCP2_CRYPTO_LIBRARY_DIRS})

    if(NOT NGTCP2_INCLUDE_DIRS AND NOT NGTCP2_FOUND)
        message(FATAL_ERROR "ngtcp2 headers not found. Set NGTCP2_ROOT or install libngtcp2-dev.")
    endif()

    set(NGTCP2_CONFIGURED TRUE)
    message(STATUS "QUIC (ngtcp2/nghttp3) available")
    message(STATUS "NGTCP2 Include Dirs: ${NGTCP2_ALL_INCLUDE_DIRS}")
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
    src/async_file_writer.cpp
    src/http3_client.cpp
    src/rsync_client.cpp
)

target_include_directories(hfdown PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(hfdown PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
)

# Agent Implementation Controller
add_executable(agentimpl 
    src/agent_impl.cpp 
    src/http_client.cpp 
    src/async_file_writer.cpp
    src/quic_socket.cpp
    src/http3_client.cpp
    src/lisp.cpp
)
target_include_directories(agentimpl PRIVATE include ${OPENSSL_INCLUDE_DIR})
target_link_libraries(agentimpl PRIVATE 
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
)
if(USE_NGTCP2 AND NGTCP2_CONFIGURED)
    target_include_directories(agentimpl PRIVATE ${NGTCP2_ALL_INCLUDE_DIRS})
    target_link_libraries(agentimpl PRIVATE ${NGTCP2_ALL_LIBRARIES})
    target_compile_definitions(agentimpl PRIVATE USE_NGTCP2)
endif()

# Apply ngtcp2/nghttp3 include/libs to the hfdown target if configured
if(USE_NGTCP2 AND NGTCP2_CONFIGURED)
    target_include_directories(hfdown PRIVATE ${NGTCP2_ALL_INCLUDE_DIRS})
    target_link_libraries(hfdown PRIVATE ${NGTCP2_ALL_LIBRARIES})
    target_compile_definitions(hfdown PRIVATE USE_NGTCP2)
    message(STATUS "QUIC (ngtcp2/nghttp3) enabled for target hfdown")
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(hfdown PRIVATE /W4)
else()
    target_compile_options(hfdown PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Test executables
if(USE_NGTCP2 AND NGTCP2_CONFIGURED)
    add_executable(test_http3 src/tests/test_http3.cpp
        src/http3_client.cpp
        src/quic_socket.cpp
        src/async_file_writer.cpp
        src/http_client.cpp
    )
    target_include_directories(test_http3 PRIVATE ${CMAKE_SOURCE_DIR}/include ${NGTCP2_ALL_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${LIBUV_INCLUDE_DIRS})
    target_link_libraries(test_http3 PRIVATE ${NGTCP2_ALL_LIBRARIES} ${OPENSSL_LIBRARIES} CURL::libcurl ${LIBUV_LIBRARIES} ${LIBURING_LIBRARIES})
    target_compile_definitions(test_http3 PRIVATE USE_NGTCP2)

    add_executable(test_http3_range src/tests/test_http3_range.cpp
        src/http3_client.cpp
        src/quic_socket.cpp
        src/async_file_writer.cpp
        src/http_client.cpp
    )
    target_include_directories(test_http3_range PRIVATE ${CMAKE_SOURCE_DIR}/include ${NGTCP2_ALL_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${LIBUV_INCLUDE_DIRS})
    target_link_libraries(test_http3_range PRIVATE ${NGTCP2_ALL_LIBRARIES} ${OPENSSL_LIBRARIES} CURL::libcurl ${LIBUV_LIBRARIES} ${LIBURING_LIBRARIES})
    target_compile_definitions(test_http3_range PRIVATE USE_NGTCP2)

    add_executable(benchmark_h3_h2 src/tests/benchmark_h3_h2.cpp
        src/http3_client.cpp
        src/quic_socket.cpp
        src/async_file_writer.cpp
        src/http_client.cpp
    )
    target_include_directories(benchmark_h3_h2 PRIVATE ${CMAKE_SOURCE_DIR}/include ${NGTCP2_ALL_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${LIBUV_INCLUDE_DIRS})
    target_link_libraries(benchmark_h3_h2 PRIVATE ${NGTCP2_ALL_LIBRARIES} ${OPENSSL_LIBRARIES} CURL::libcurl ${LIBUV_LIBRARIES} ${LIBURING_LIBRARIES})
    target_compile_definitions(benchmark_h3_h2 PRIVATE USE_NGTCP2)

    add_executable(test_providers_h3 src/tests/test_providers_h3.cpp
        src/http3_client.cpp
        src/quic_socket.cpp
        src/async_file_writer.cpp
        src/http_client.cpp
    )
    target_include_directories(test_providers_h3 PRIVATE ${CMAKE_SOURCE_DIR}/include ${NGTCP2_ALL_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${LIBUV_INCLUDE_DIRS})
    target_link_libraries(test_providers_h3 PRIVATE ${NGTCP2_ALL_LIBRARIES} ${OPENSSL_LIBRARIES} CURL::libcurl ${LIBUV_LIBRARIES} ${LIBURING_LIBRARIES})
    target_compile_definitions(test_providers_h3 PRIVATE USE_NGTCP2)
endif()