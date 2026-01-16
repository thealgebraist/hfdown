#pragma once

#include <unistd.h>
#include <string_view>
#include <charconv>
#include <system_error>

namespace compact {

// Extremely lightweight buffer-based writer to replace std::format/std::cout
class Writer {
public:
    static void print(std::string_view s) {
        ::write(STDOUT_FILENO, s.data(), s.size());
    }

    static void error(std::string_view s) {
        ::write(STDERR_FILENO, s.data(), s.size());
    }

    template<typename T>
    static void print_num(T val) {
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
        if (ec == std::errc()) {
            ::write(STDOUT_FILENO, buf, ptr - buf);
        }
    }

    static void nl() {
        char c = '\n';
        ::write(STDOUT_FILENO, &c, 1);
    }
};

} // namespace compact

