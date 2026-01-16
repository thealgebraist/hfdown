#pragma once

#include <unistd.h>
#include <string_view>
#include <charconv>
#include <system_error>
#include <mutex>

namespace compact {

// Extremely lightweight buffer-based writer to replace std::format/std::cout
class Writer {
public:
    static void print(std::string_view s) {
        std::lock_guard<std::mutex> lock(get_mutex());
        ::write(STDOUT_FILENO, s.data(), s.size());
    }

    static void error(std::string_view s) {
        std::lock_guard<std::mutex> lock(get_mutex());
        ::write(STDERR_FILENO, s.data(), s.size());
    }

    template<typename T>
    static void print_num(T val) {
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
        if (ec == std::errc()) {
            std::lock_guard<std::mutex> lock(get_mutex());
            ::write(STDOUT_FILENO, buf, ptr - buf);
        }
    }

    static void nl() {
        char c = '\n';
        std::lock_guard<std::mutex> lock(get_mutex());
        ::write(STDOUT_FILENO, &c, 1);
    }

private:
    static std::mutex& get_mutex() {
        static std::mutex m;
        return m;
    }
};

} // namespace compact

