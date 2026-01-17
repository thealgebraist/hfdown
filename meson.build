project('hfdown', 'cpp',
  version : '1.0.0',
  default_options : [
    'warning_level=3',
    'cpp_std=c++23'
  ]
)

# Dependencies
threads_dep = dependency('threads')
openssl_dep = dependency('openssl')


# Optional libuv
libuv_dep = dependency('libuv', required : false)
if not libuv_dep.found()
  # Mac/Homebrew fallback
  cpp = meson.get_compiler('cpp')
  libuv_dep = cpp.find_library('uv', dirs : ['/opt/homebrew/lib', '/usr/local/lib'], required : false)
endif

# Optional ngtcp2 (QUIC)
ngtcp2_dep = dependency('libngtcp2', required : false)
nghttp3_dep = dependency('libnghttp3', required : false)
ngtcp2_crypto_dep = dependency('libngtcp2_crypto_ossl', required : false)
use_ngtcp2 = false

if ngtcp2_dep.found() and nghttp3_dep.found() and ngtcp2_crypto_dep.found()
  use_ngtcp2 = true
  add_project_arguments('-DUSE_NGTCP2', language : 'cpp')
  add_project_arguments('-DUSE_NGTCP2_CRYPTO_OSSL', language : 'cpp')
else
  # Try to find headers/libs manually if pkg-config failed (common on macOS homebrew)
  # This is a simplified fallback compared to CMake but covers common cases
  cpp = meson.get_compiler('cpp')
  ngtcp2_lib = cpp.find_library('ngtcp2', dirs : ['/opt/homebrew/lib'], required : false)
  nghttp3_lib = cpp.find_library('nghttp3', dirs : ['/opt/homebrew/lib'], required : false)
  ngcrypto_lib = cpp.find_library('ngtcp2_crypto_ossl', dirs : ['/opt/homebrew/lib'], required : false)
  
  if ngtcp2_lib.found() and nghttp3_lib.found() and ngcrypto_lib.found()
    use_ngtcp2 = true
    add_project_arguments('-DUSE_NGTCP2', language : 'cpp')
    add_project_arguments('-DUSE_NGTCP2_CRYPTO_OSSL', language : 'cpp')
    ngtcp2_dep = ngtcp2_lib
    nghttp3_dep = nghttp3_lib
    ngtcp2_crypto_dep = ngcrypto_lib
    # Assuming includes are in standard paths or handled by compiler default search
  endif
endif

quic_deps = []
if use_ngtcp2
  quic_deps = [ngtcp2_dep, nghttp3_dep, ngtcp2_crypto_dep]
endif

inc = include_directories('include', 'src')

# Sources
hfdown_sources = files(
  'src/main.cpp',
  'src/http_client.cpp',
  'src/hf_client.cpp',
  'src/kaggle_client.cpp',
  'src/cache_manager.cpp',
  'src/github_client.cpp',
  'src/file_monitor.cpp',
  'src/git_uploader.cpp',
  'src/secret_scanner.cpp',
  'src/quic_socket.cpp',
  'src/async_file_writer.cpp',
  'src/http3_client.cpp',
  'src/rsync_client.cpp',
  'src/tls_socket.cpp',
  'src/socket_wrapper.cpp'
)

agentimpl_sources = files(
  'src/agent_impl.cpp',
  'src/http_client.cpp',
  'src/async_file_writer.cpp',
  'src/quic_socket.cpp',
  'src/http3_client.cpp',
  'src/lisp.cpp',
  'src/tls_socket.cpp',
  'src/socket_wrapper.cpp'
)

# Executables
executable('hfdown',
  hfdown_sources,
  include_directories : inc,
  dependencies : [threads_dep, openssl_dep, libuv_dep] + quic_deps
)

executable('agentimpl',
  agentimpl_sources,
  include_directories : inc,
  dependencies : [threads_dep, openssl_dep, libuv_dep] + quic_deps
)

# Tests
if use_ngtcp2
  test_sources_common = files(
    'src/http3_client.cpp',
    'src/quic_socket.cpp',
    'src/async_file_writer.cpp',
    'src/http_client.cpp',
    'src/tls_socket.cpp',
    'src/socket_wrapper.cpp'
  )
  
  executable('test_http3',
    ['src/tests/test_http3.cpp'] + test_sources_common,
    include_directories : inc,
    dependencies : [threads_dep, openssl_dep, libuv_dep] + quic_deps
  )

  executable('benchmark_h3_h2',
    ['src/tests/benchmark_h3_h2.cpp'] + test_sources_common,
    include_directories : inc,
    dependencies : [threads_dep, openssl_dep, libuv_dep] + quic_deps
  )
endif
