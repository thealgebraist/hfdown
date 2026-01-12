// Simple JSON parser for C++23
// This is a minimal implementation for parsing HuggingFace API responses

#pragma once

#include <string>
#include <string_view>
#include <map>
#include <vector>
#include <variant>
#include <memory>
#include <stdexcept>
#include <cctype>

namespace json {

class Value;

using Null = std::monostate;
using Boolean = bool;
using Number = double;
using String = std::string;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

class Value {
public:
    using VariantType = std::variant<Null, Boolean, Number, String, Array, Object>;
    
    Value() : data_(Null{}) {}
    Value(Null) : data_(Null{}) {}
    Value(bool b) : data_(b) {}
    Value(int i) : data_(static_cast<double>(i)) {}
    Value(double d) : data_(d) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(const char* s) : data_(std::string(s)) {}
    Value(Array a) : data_(std::move(a)) {}
    Value(Object o) : data_(std::move(o)) {}
    
    bool is_null() const { return std::holds_alternative<Null>(data_); }
    bool is_bool() const { return std::holds_alternative<Boolean>(data_); }
    bool is_number() const { return std::holds_alternative<Number>(data_); }
    bool is_string() const { return std::holds_alternative<String>(data_); }
    bool is_array() const { return std::holds_alternative<Array>(data_); }
    bool is_object() const { return std::holds_alternative<Object>(data_); }
    
    bool as_bool() const { return std::get<Boolean>(data_); }
    double as_number() const { return std::get<Number>(data_); }
    const String& as_string() const { return std::get<String>(data_); }
    const Array& as_array() const { return std::get<Array>(data_); }
    const Object& as_object() const { return std::get<Object>(data_); }
    
    Array& as_array() { return std::get<Array>(data_); }
    Object& as_object() { return std::get<Object>(data_); }
    
    const Value& operator[](const std::string& key) const {
        static const Value null_value;
        if (!is_object()) return null_value;
        const auto& obj = as_object();
        auto it = obj.find(key);
        return it != obj.end() ? it->second : null_value;
    }
    
    const Value& operator[](size_t index) const {
        static const Value null_value;
        if (!is_array()) return null_value;
        const auto& arr = as_array();
        return index < arr.size() ? arr[index] : null_value;
    }

private:
    VariantType data_;
};

class Parser {
public:
    static Value parse(std::string_view json) {
        Parser parser(json);
        return parser.parse_value();
    }

private:
    std::string_view input_;
    size_t pos_ = 0;
    
    Parser(std::string_view json) : input_(json) {}
    
    void skip_whitespace() {
        while (pos_ < input_.size() && std::isspace(input_[pos_])) {
            ++pos_;
        }
    }
    
    char peek() const {
        return pos_ < input_.size() ? input_[pos_] : '\0';
    }
    
    char next() {
        return pos_ < input_.size() ? input_[pos_++] : '\0';
    }
    
    bool match(char c) {
        if (peek() == c) {
            next();
            return true;
        }
        return false;
    }
    
    Value parse_value() {
        skip_whitespace();
        char c = peek();
        
        if (c == 'n') return parse_null();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == '"') return parse_string();
        if (c == '[') return parse_array();
        if (c == '{') return parse_object();
        if (c == '-' || std::isdigit(c)) return parse_number();
        
        throw std::runtime_error("Invalid JSON");
    }
    
    Value parse_null() {
        if (input_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return Value();
        }
        throw std::runtime_error("Invalid null");
    }
    
    Value parse_bool() {
        if (input_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return Value(true);
        }
        if (input_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return Value(false);
        }
        throw std::runtime_error("Invalid boolean");
    }
    
    Value parse_number() {
        size_t start = pos_;
        if (peek() == '-') next();
        
        while (std::isdigit(peek())) next();
        
        if (peek() == '.') {
            next();
            while (std::isdigit(peek())) next();
        }
        
        if (peek() == 'e' || peek() == 'E') {
            next();
            if (peek() == '+' || peek() == '-') next();
            while (std::isdigit(peek())) next();
        }
        
        std::string num_str(input_.substr(start, pos_ - start));
        return Value(std::stod(num_str));
    }
    
    Value parse_string() {
        if (next() != '"') throw std::runtime_error("Expected '\"'");
        
        std::string result;
        while (peek() != '"' && peek() != '\0') {
            if (peek() == '\\') {
                next();
                char c = next();
                switch (c) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    default: result += c;
                }
            } else {
                result += next();
            }
        }
        
        if (next() != '"') throw std::runtime_error("Expected '\"'");
        return Value(result);
    }
    
    Value parse_array() {
        if (next() != '[') throw std::runtime_error("Expected '['");
        
        Array arr;
        skip_whitespace();
        
        if (peek() == ']') {
            next();
            return Value(arr);
        }
        
        while (true) {
            arr.push_back(parse_value());
            skip_whitespace();
            
            if (match(']')) break;
            if (!match(',')) throw std::runtime_error("Expected ',' or ']'");
        }
        
        return Value(arr);
    }
    
    Value parse_object() {
        if (next() != '{') throw std::runtime_error("Expected '{'");
        
        Object obj;
        skip_whitespace();
        
        if (peek() == '}') {
            next();
            return Value(obj);
        }
        
        while (true) {
            skip_whitespace();
            auto key = parse_string().as_string();
            skip_whitespace();
            
            if (!match(':')) throw std::runtime_error("Expected ':'");
            
            auto value = parse_value();
            obj[key] = std::move(value);
            
            skip_whitespace();
            if (match('}')) break;
            if (!match(',')) throw std::runtime_error("Expected ',' or '}'");
        }
        
        return Value(obj);
    }
};

inline Value parse(std::string_view json) {
    return Parser::parse(json);
}

} // namespace json
