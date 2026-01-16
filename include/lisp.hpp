#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>

namespace lisp {

enum class Kind { Integer, Symbol, List, Nil, Builtin, Lambda };

struct Val;
using ValPtr = std::shared_ptr<Val>;
using Env = std::map<std::string, ValPtr>;

struct Val {
    Kind kind;
    long n;
    std::string s;
    std::vector<ValPtr> list;
    // For functions
    std::vector<std::string> params;
    ValPtr body;
    Env closure;

    static ValPtr make_int(long n) { return std::make_shared<Val>(Kind::Integer, n); }
    static ValPtr make_sym(std::string s) { return std::make_shared<Val>(Kind::Symbol, 0, s); }
    static ValPtr make_nil() { return std::make_shared<Val>(Kind::Nil); }
    
    Val(Kind k, long n_val = 0, std::string s_val = "") : kind(k), n(n_val), s(s_val) {}
};

ValPtr eval(ValPtr v, Env& env);
ValPtr lisp_apply(ValPtr fn, const std::vector<ValPtr>& args);

} // namespace lisp
