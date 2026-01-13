## Plan: Complete hfdown HTTP/3 Client Implementation

Build a fully functional HTTP/3 client using ngtcp2/nghttp3 to replace UDP placeholders with proper QUIC protocol implementation, enabling real-world HTTP/3 requests.

### Steps

1. **Implement ngtcp2 QUIC handshake event loop** - Replace handshake scaffolding with proper packet exchange using ngtcp2_conn_writev_stream/read_pkt APIs for connection establishment
2. **Complete HTTP/3 request transmission** - Fix send_headers() to drive ngtcp2 packet flushing after nghttp3_conn_submit_request, enabling actual HTTP/3 GET requests  
3. **Implement HTTP/3 response handling** - Replace recv_headers() placeholder with nghttp3 event polling and header parsing from QUIC streams
4. **Integrate stream data flow** - Update send()/recv() methods to use proper QUIC streams via ngtcp2_conn_writev_stream instead of UDP fallback
5. **Add connection lifecycle management** - Implement timeout handling, error recovery, and proper cleanup for production-ready QUIC connections
6. **Validate with end-to-end testing** - Test complete HTTP/3 GET requests against real servers and benchmark vs HTTP/1.1 fallback

### Further Considerations

1. **Performance optimization vs HTTP/1.1 fallback** - Should we prioritize connection reuse, stream multiplexing, or keep simple single-request model?
2. **Error handling strategy** - Graceful degradation to HTTP/2/1.1 on QUIC failures, or fail-fast approach?
3. **Testing scope** - Integration tests against specific HTTP/3 servers (Cloudflare, Google) or generic compatibility testing?
