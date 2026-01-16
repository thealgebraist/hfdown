#include <iostream>

class Production {
public:
    // Assume Production has some member variables and methods
    // For demonstration, let's assume it has a name
    std::string name;

    Production(const std::string& n) : name(n) {}

    // Override the << operator to provide meaningful output
    friend std::ostream& operator<<(std::ostream& os, const Production& prod) {
        return os << "Production(" << prod.name << ")";
    }
};
