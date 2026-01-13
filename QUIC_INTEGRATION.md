QUIC (quiche) Integration

This project can optionally use the quiche QUIC implementation for real HTTP/3 support.

Build steps (recommended):

1. Build/install quiche (see quiche README):
   - Clone quiche and build a native library or use a prebuilt package.
   - Ensure you have a usable libquiche and headers available.

2. Configure CMake with quiche root path:

```sh
cmake -S . -B build -DUSE_QUIC=ON -DQUICHE_ROOT=/path/to/quiche/build
cmake --build build
```

3. If using Homebrew OpenSSL on macOS, set OpenSSL root too:

```sh
cmake -S . -B build -DUSE_QUIC=ON -DQUICHE_ROOT=/path/to/quiche/build -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)
cmake --build build
```

Notes:
- The current code contains guarded placeholders for `init_quic()`, `handshake()`, `send()`, and `recv()` in `src/quic_socket.cpp`. These will compile when `USE_QUIC` is enabled, but you still need to implement the detailed wiring to `libquiche` APIs (creating connections, streams, event loop integration).
- If you want, I can implement a minimal quiche-based client here (requires detailing the build of quiche and linking static/dynamic lib), or add a mocked `QuicSocket` for unit tests so tests don't require quiche at runtime.
