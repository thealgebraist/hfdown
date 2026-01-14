cmake_minimum_required(VERSION 3.25)
project(hfdown VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUV REQUIRED libuv)
set(LIBUV_INCLUDE_DIRS "/opt/homebrew/Cellar/libuv/1.51.0/include")
set(LIBUV_LIBRARIES "/opt/homebrew/Cellar/libuv/1.51.0/lib/libuv.a")

pkg_check_modules(LIBURING QUIET liburing)
if(LIBURING_FOUND)
    add_definitions(-DHAS_IO_URING)
    message(STATUS "io_uring support enabled")
endif()

# Optional QUIC (quiche) support
option(USE_QUIC "Enable QUIC (quiche) support" OFF)
if(USE_QUIC)
    if(NOT DEFINED QUICHE_ROOT)
        # Default to Homebrew quiche if available
        set(QUICHE_ROOT "/opt/homebrew/Cellar/cloudflare-quiche/0.24.7")
    endif()
    if(NOT DEFINED QUICHE_INCLUDE_DIR)
        set(QUICHE_INCLUDE_DIR ${QUICHE_ROOT}/include)
    endif()
    if(NOT DEFINED QUICHE_LIB)
        set(QUICHE_LIB ${QUICHE_ROOT}/lib/libquiche.a)
    endif()
    if(NOT EXISTS ${QUICHE_INCLUDE_DIR})
        message(FATAL_ERROR "quiche include dir not found: ${QUICHE_INCLUDE_DIR}")
    endif()
    if(NOT EXISTS ${QUICHE_LIB})
        message(FATAL_ERROR "quiche lib not found: ${QUICHE_LIB}")
    endif()
endif()

# Optional NGTCP2 support
option(USE_NGTCP2 "Enable QUIC (ngtcp2 + nghttp3) support" OFF)
if(USE_NGTCP2)
    # Homebrew pkg-config names are libngtcp2 / libngtcp2_crypto_ossl / libnghttp3
    if(NOT DEFINED NGTCP2_ROOT)
        set(NGTCP2_ROOT "/opt/homebrew/Cellar/libngtcp2/1.19.0")
    endif()
    if(NOT DEFINED NGHTTP3_ROOT)
        set(NGHTTP3_ROOT "/opt/homebrew/Cellar/libnghttp3/1.14.0")
    endif()
    pkg_check_modules(NGTCP2 QUIET libngtcp2)
    pkg_check_modules(NGHTTP3 QUIET libnghttp3)
    pkg_check_modules(NGTCP2_CRYPTO QUIET libngtcp2_crypto_ossl)

    # If pkg-config didn't find them, try Homebrew Cellar defaults
    if(NOT NGTCP2_FOUND)
        if(NOT DEFINED NGTCP2_ROOT)
            set(NGTCP2_ROOT "/opt/homebrew/Cellar/libngtcp2/1.19.0")
        endif()
        set(NGTCP2_INCLUDE_DIRS ${NGTCP2_ROOT}/include)
        if(EXISTS ${NGTCP2_ROOT}/lib/libngtcp2.a)
            set(NGTCP2_LIBRARIES ${NGTCP2_ROOT}/lib/libngtcp2.a)
        elseif(EXISTS ${NGTCP2_ROOT}/lib/libngtcp2.dylib)
            set(NGTCP2_LIBRARIES ${NGTCP2_ROOT}/lib/libngtcp2.dylib)
        endif()
    endif()

    if(NOT NGHTTP3_FOUND)
        if(NOT DEFINED NGHTTP3_ROOT)
            set(NGHTTP3_ROOT "/opt/homebrew/Cellar/libnghttp3/1.14.0")
        endif()
        set(NGHTTP3_INCLUDE_DIRS ${NGHTTP3_ROOT}/include)
        if(EXISTS ${NGHTTP3_ROOT}/lib/libnghttp3.a)
            set(NGHTTP3_LIBRARIES ${NGHTTP3_ROOT}/lib/libnghttp3.a)
        elseif(EXISTS ${NGHTTP3_ROOT}/lib/libnghttp3.dylib)
            set(NGHTTP3_LIBRARIES ${NGHTTP3_ROOT}/lib/libnghttp3.dylib)
        endif()
    endif()

    # Prefer the crypto helper pkg-config if present (provides openssl glue)
    if(NGTCP2_CRYPTO_FOUND)
        set(NGTCP2_INCLUDE_DIRS ${NGTCP2_CRYPTO_INCLUDE_DIRS})
        # Prepend crypto helper libs (pkg-config provides -L/path -l...) and keep core ngtcp2 libs
        set(NGTCP2_LIBRARIES "${NGTCP2_CRYPTO_LIBRARIES} ${NGTCP2_LIBRARIES}")
        message(STATUS "Using libngtcp2_crypto_ossl for TLS helper")
    else()
        # Try to find a prebuilt helper in the Cellar
        if(EXISTS ${NGTCP2_ROOT}/lib/libngtcp2_crypto_ossl.a)
            list(APPEND NGTCP2_LIBRARIES ${NGTCP2_ROOT}/lib/libngtcp2_crypto_ossl.a)
        elseif(EXISTS ${NGTCP2_ROOT}/lib/libngtcp2_crypto_ossl.dylib)
            list(APPEND NGTCP2_LIBRARIES ${NGTCP2_ROOT}/lib/libngtcp2_crypto_ossl.dylib)
        endif()
    endif()

    # Also append explicit Cellar library paths if present to avoid linker -l resolution issues
    if(EXISTS ${NGTCP2_ROOT}/lib/libngtcp2.a)
        set(NGTCP2_LIBRARIES ${NGTCP2_ROOT}/lib/libngtcp2.a)
    elseif(EXISTS ${NGTCP2_ROOT}/lib/libngtcp2.dylib)
        set(NGTCP2_LIBRARIES ${NGTCP2_ROOT}/lib/libngtcp2.dylib)
    endif()
    if(EXISTS ${NGTCP2_ROOT}/lib/libngtcp2_crypto_ossl.a)
        set(NGTCP2_CRYPTO_LIB ${NGTCP2_ROOT}/lib/libngtcp2_crypto_ossl.a)
        list(APPEND NGTCP2_LIBRARIES ${NGTCP2_CRYPTO_LIB})
    elseif(EXISTS ${NGTCP2_ROOT}/lib/libngtcp2_crypto_ossl.dylib)
        set(NGTCP2_CRYPTO_LIB ${NGTCP2_ROOT}/lib/libngtcp2_crypto_ossl.dylib)
        list(APPEND NGTCP2_LIBRARIES ${NGTCP2_CRYPTO_LIB})
    endif()
    if(EXISTS ${NGHTTP3_ROOT}/lib/libnghttp3.a)
        set(NGHTTP3_LIBRARIES ${NGHTTP3_ROOT}/lib/libnghttp3.a)
    elseif(EXISTS ${NGHTTP3_ROOT}/lib/libnghttp3.dylib)
        set(NGHTTP3_LIBRARIES ${NGHTTP3_ROOT}/lib/libnghttp3.dylib)
    endif()
    message(STATUS "NGTCP2_LIBRARIES='${NGTCP2_LIBRARIES}'")
    message(STATUS "NGHTTP3_LIBRARIES='${NGHTTP3_LIBRARIES}'")

    # Filter out bare library names from pkg-config output (e.g. 'ngtcp2')
    set(_filtered_ngtcp2)
    foreach(_lib ${NGTCP2_LIBRARIES})
        if(_lib MATCHES "^-" OR _lib MATCHES "/")
            list(APPEND _filtered_ngtcp2 ${_lib})
        else()
            message(STATUS "Dropping bare lib token from NGTCP2_LIBRARIES: ${_lib}")
        endif()
    endforeach()
    set(NGTCP2_LIBRARIES ${_filtered_ngtcp2})

    set(_filtered_nghttp3)
    foreach(_lib ${NGHTTP3_LIBRARIES})
        if(_lib MATCHES "^-" OR _lib MATCHES "/")
            list(APPEND _filtered_nghttp3 ${_lib})
        else()
            message(STATUS "Dropping bare lib token from NGHTTP3_LIBRARIES: ${_lib}")
        endif()
    endforeach()
    set(NGHTTP3_LIBRARIES ${_filtered_nghttp3})

    if( (NOT NGTCP2_FOUND AND (NOT DEFINED NGTCP2_LIBRARIES OR NGTCP2_LIBRARIES STREQUAL "")) OR (NOT NGHTTP3_FOUND AND (NOT DEFINED NGHTTP3_LIBRARIES OR NGHTTP3_LIBRARIES STREQUAL "")) )
        message(FATAL_ERROR "ngtcp2/nghttp3 not found. Install libngtcp2 and libnghttp3 or set NGTCP2_ROOT/NGHTTP3_ROOT.")
    endif()

    set(NGTCP2_CONFIGURED TRUE)
    message(STATUS "QUIC (ngtcp2/nghttp3) available")
else()
    message(STATUS "QUIC (ngtcp2) disabled. Build with -DUSE_NGTCP2=ON after installing ngtcp2/nghttp3")
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
    ${LIBUV_INCLUDE_DIRS}
)

target_link_libraries(hfdown PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    ${LIBUV_LIBRARIES}
    ${LIBURING_LIBRARIES}
)

# Apply ngtcp2/nghttp3 include/libs to the hfdown target if configured
if(USE_NGTCP2 AND NGTCP2_CONFIGURED)
    target_include_directories(hfdown PRIVATE ${NGTCP2_INCLUDE_DIRS} ${NGHTTP3_INCLUDE_DIRS})
    target_link_libraries(hfdown PRIVATE ${NGTCP2_LIBRARIES} ${NGHTTP3_LIBRARIES})
    target_compile_definitions(hfdown PRIVATE USE_NGTCP2)
    message(STATUS "QUIC (ngtcp2/nghttp3) enabled for target hfdown")
endif()

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

# Test executables
if(USE_NGTCP2 AND NGTCP2_CONFIGURED)
    add_executable(test_http3 src/tests/test_http3.cpp
        src/http3_client.cpp
        src/quic_socket.cpp
        src/async_file_writer.cpp
        src/http_client.cpp
    )
    target_include_directories(test_http3 PRIVATE ${CMAKE_SOURCE_DIR}/include ${NGTCP2_INCLUDE_DIRS} ${NGHTTP3_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${LIBUV_INCLUDE_DIRS})
    target_link_libraries(test_http3 PRIVATE ${NGTCP2_LIBRARIES} ${NGHTTP3_LIBRARIES} ${OPENSSL_LIBRARIES} CURL::libcurl ${LIBUV_LIBRARIES} ${LIBURING_LIBRARIES})
    target_compile_definitions(test_http3 PRIVATE USE_NGTCP2)

    add_executable(test_http3_range src/tests/test_http3_range.cpp
        src/http3_client.cpp
        src/quic_socket.cpp
        src/async_file_writer.cpp
        src/http_client.cpp
    )
    target_include_directories(test_http3_range PRIVATE ${CMAKE_SOURCE_DIR}/include ${NGTCP2_INCLUDE_DIRS} ${NGHTTP3_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${LIBUV_INCLUDE_DIRS})
    target_link_libraries(test_http3_range PRIVATE ${NGTCP2_LIBRARIES} ${NGHTTP3_LIBRARIES} ${OPENSSL_LIBRARIES} CURL::libcurl ${LIBUV_LIBRARIES} ${LIBURING_LIBRARIES})
    target_compile_definitions(test_http3_range PRIVATE USE_NGTCP2)

    add_executable(benchmark_h3_h2 src/tests/benchmark_h3_h2.cpp
        src/http3_client.cpp
        src/quic_socket.cpp
        src/async_file_writer.cpp
        src/http_client.cpp
    )
    target_include_directories(benchmark_h3_h2 PRIVATE ${CMAKE_SOURCE_DIR}/include ${NGTCP2_INCLUDE_DIRS} ${NGHTTP3_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${LIBUV_INCLUDE_DIRS})
    target_link_libraries(benchmark_h3_h2 PRIVATE ${NGTCP2_LIBRARIES} ${NGHTTP3_LIBRARIES} ${OPENSSL_LIBRARIES} CURL::libcurl ${LIBUV_LIBRARIES} ${LIBURING_LIBRARIES})
    target_compile_definitions(benchmark_h3_h2 PRIVATE USE_NGTCP2)

    add_executable(test_providers_h3 src/tests/test_providers_h3.cpp
        src/http3_client.cpp
        src/quic_socket.cpp
        src/async_file_writer.cpp
        src/http_client.cpp
    )
    target_include_directories(test_providers_h3 PRIVATE ${CMAKE_SOURCE_DIR}/include ${NGTCP2_INCLUDE_DIRS} ${NGHTTP3_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${LIBUV_INCLUDE_DIRS})
    target_link_libraries(test_providers_h3 PRIVATE ${NGTCP2_LIBRARIES} ${NGHTTP3_LIBRARIES} ${OPENSSL_LIBRARIES} CURL::libcurl ${LIBUV_LIBRARIES} ${LIBURING_LIBRARIES})
    target_compile_definitions(test_providers_h3 PRIVATE USE_NGTCP2)
endif()