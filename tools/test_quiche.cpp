// Simple test program to call quiche config creation functions
#include <iostream>
#ifdef USE_QUIC
#include <quiche.h>
#endif

int main() {
#ifdef USE_QUIC
    std::cout << "quiche test: QUIC enabled at compile time\n";
    auto cfg = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    std::cout << "quiche_config_new -> " << cfg << "\n";
    if (!cfg) {
        std::cerr << "quiche_config_new failed\n";
        return 2;
    }

    auto h3 = quiche_h3_config_new();
    std::cout << "quiche_h3_config_new -> " << h3 << "\n";
    if (!h3) {
        std::cerr << "quiche_h3_config_new failed\n";
        return 3;
    }

    quiche_h3_config_free((quiche_h3_config*)h3);
    quiche_config_free((quiche_config*)cfg);
    std::cout << "quiche test: success\n";
    return 0;
#else
    std::cout << "quiche test: compiled without USE_QUIC\n";
    return 0;
#endif
}
