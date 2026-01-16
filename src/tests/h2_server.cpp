#include <iostream>
#include <vector>
#include <string>
#include <format>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <nghttp2/nghttp2.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace h2 {

struct Connection {
    int fd;
    nghttp2_session* session = nullptr;
    std::string current_path;
    std::map<int32_t, std::string> response_lengths;
};

struct FileSource {
    int fd;
    size_t remaining;
};

ssize_t send_cb(nghttp2_session*, const uint8_t* data, size_t length, int, void* user_data) {
    auto* conn = static_cast<Connection*>(user_data);
    ssize_t n = write(conn->fd, data, length);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return NGHTTP2_ERR_WOULDBLOCK;
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return n;
}

ssize_t data_prd_cb(nghttp2_session*, int32_t, uint8_t* buf, size_t length,
                    uint32_t* data_flags, nghttp2_data_source* source, void*) {
    auto* fs = static_cast<FileSource*>(source->ptr);
    ssize_t n = read(fs->fd, buf, std::min(length, fs->remaining));
    if (n < 0) return NGHTTP2_ERR_CALLBACK_FAILURE;
    fs->remaining -= n;
    if (fs->remaining == 0) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        close(fs->fd);
        delete fs;
    }
    return n;
}

int on_header_cb(nghttp2_session*, const nghttp2_frame* frame,
                 const uint8_t* name, size_t namelen, const uint8_t* value,
                 size_t valuelen, uint8_t, void* user_data) {
    auto* conn = static_cast<Connection*>(user_data);
    if (frame->hd.type == NGHTTP2_HEADERS && frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
        std::string_view n(reinterpret_cast<const char*>(name), namelen);
        std::string_view v(reinterpret_cast<const char*>(value), valuelen);
        if (n == ":path") conn->current_path = std::string(v);
    }
    return 0;
}

int on_frame_recv_cb(nghttp2_session* session, const nghttp2_frame* frame, void* user_data) {
    auto* conn = static_cast<Connection*>(user_data);
    if (frame->hd.type == NGHTTP2_HEADERS && frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
        std::string path_v = conn->current_path;
        // Search for file relative to current directory
        std::string rel_path = path_v;
        if (rel_path.starts_with("/")) rel_path = rel_path.substr(1);
        size_t query_pos = rel_path.find('?');
        if (query_pos != std::string::npos) rel_path = rel_path.substr(0, query_pos);
        
        struct stat st;
        if (stat(rel_path.c_str(), &st) != 0) {
            // Fallback for metadata if path is exactly what curl sends
            if (path_v.find("api/models") != std::string::npos) {
                rel_path = "api/models/test/stress/tree/main?recursive=true";
            }
        }

        if (stat(rel_path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            int fd = open(rel_path.c_str(), O_RDONLY);
            conn->response_lengths[frame->hd.stream_id] = std::to_string(st.st_size);
            const std::string& len_str = conn->response_lengths[frame->hd.stream_id];
            
            nghttp2_nv hdrs[] = {
                {(uint8_t*)":status", (uint8_t*)"200", 7, 3, NGHTTP2_NV_FLAG_NONE},
                {(uint8_t*)"content-length", (uint8_t*)len_str.c_str(), 14, len_str.size(), NGHTTP2_NV_FLAG_NONE}
            };
            FileSource* fs = new FileSource{fd, (size_t)st.st_size};
            nghttp2_data_provider data_prd;
            data_prd.source.ptr = fs;
            data_prd.read_callback = data_prd_cb;
            nghttp2_submit_response(session, frame->hd.stream_id, hdrs, 2, &data_prd);
        } else {
            nghttp2_nv hdrs[] = {{(uint8_t*)":status", (uint8_t*)"404", 7, 3, NGHTTP2_NV_FLAG_NONE}};
            nghttp2_submit_response(session, frame->hd.stream_id, hdrs, 1, nullptr);
            std::cerr << "[H2] 404: " << rel_path << " (from " << path_v << ")" << std::endl;
        }
    }
    return 0;
}

int on_stream_close_cb(nghttp2_session*, int32_t stream_id, uint32_t, void* user_data) {
    auto* conn = static_cast<Connection*>(user_data);
    conn->response_lengths.erase(stream_id);
    return 0;
}

void run_server(int port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int val = 1; setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
    bind(listen_fd, (sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, 128); fcntl(listen_fd, F_SETFL, O_NONBLOCK);

    std::cerr << "H2C Server listening on " << port << " in " << std::filesystem::current_path() << std::endl;
    int kq = kqueue();
    struct kevent ev;
    EV_SET(&ev, listen_fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
    kevent(kq, &ev, 1, nullptr, 0, nullptr);

    while (true) {
        struct kevent events[32];
        int nev = kevent(kq, nullptr, 0, events, 32, nullptr);
        for (int i = 0; i < nev; ++i) {
            if (events[i].ident == (uint64_t)listen_fd) {
                int client_fd = accept(listen_fd, nullptr, nullptr);
                if (client_fd < 0) continue;
                fcntl(client_fd, F_SETFL, O_NONBLOCK);
                Connection* conn = new Connection{client_fd};
                nghttp2_session_callbacks* cb;
                nghttp2_session_callbacks_new(&cb);
                nghttp2_session_callbacks_set_send_callback(cb, send_cb);
                nghttp2_session_callbacks_set_on_header_callback(cb, on_header_cb);
                nghttp2_session_callbacks_set_on_frame_recv_callback(cb, on_frame_recv_cb);
                nghttp2_session_callbacks_set_on_stream_close_callback(cb, on_stream_close_cb);
                nghttp2_session_server_new(&conn->session, cb, conn);
                nghttp2_session_callbacks_del(cb);
                nghttp2_submit_settings(conn->session, NGHTTP2_FLAG_NONE, nullptr, 0);
                nghttp2_session_send(conn->session);
                EV_SET(&ev, client_fd, EVFILT_READ, EV_ADD, 0, 0, conn);
                kevent(kq, &ev, 1, nullptr, 0, nullptr);
                EV_SET(&ev, client_fd, EVFILT_WRITE, EV_ADD, 0, 0, conn);
                kevent(kq, &ev, 1, nullptr, 0, nullptr);
            } else {
                auto* conn = static_cast<Connection*>(events[i].udata);
                if (events[i].filter == EVFILT_READ) {
                    uint8_t buf[16384];
                    ssize_t n = read(conn->fd, buf, sizeof(buf));
                    if (n <= 0) {
                        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
                        close(conn->fd); nghttp2_session_del(conn->session); delete conn;
                        continue;
                    }
                    nghttp2_session_mem_recv(conn->session, buf, n);
                }
                nghttp2_session_send(conn->session);
            }
        }
    }
}
}

int main(int argc, char** argv) {
    h2::run_server(argc > 1 ? std::stoi(argv[1]) : 8888);
    return 0;
}