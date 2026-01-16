#include "lisp.hpp"
#include <iostream>
#include <stdexcept>

namespace lisp {

ValPtr eval(ValPtr v, Env& env) {
    switch (v->kind) {
        case Kind::Integer:
        case Kind::Nil:
        case Kind::Builtin:
        case Kind::Lambda:
            return v;
        case Kind::Symbol:
            if (env.contains(v->s)) return env[v->s];
            throw std::runtime_error("Undefined symbol: " + v->s);
        case Kind::List: {
            if (v->list.empty()) return Val::make_nil();
            auto fn = eval(v->list[0], env);
            std::vector<ValPtr> args;
            for (size_t i = 1; i < v->list.size(); ++i) {
                args.push_back(eval(v->list[i], env));
            }
            return lisp_apply(fn, args);
        }
    }
    return Val::make_nil();
}

ValPtr lisp_apply(ValPtr fn, const std::vector<ValPtr>& args) {
    if (fn->kind == Kind::Lambda) {
        Env local_env = fn->closure;
        for (size_t i = 0; i < fn->params.size() && i < args.size(); ++i) {
            local_env[fn->params[i]] = args[i];
        }
        return eval(fn->body, local_env);
    }
    throw std::runtime_error("Cannot apply non-function");
}

} // namespace lisp
