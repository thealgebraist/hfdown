#pragma once

#include <vector>
#include <string>
#include <memory>
#include <variant>
#include <optional>
#include <iostream>

namespace ir {

struct Term;
using TermPtr = std::shared_ptr<Term>;

// --- 1. Base Types (Structural) ---

struct PiType {
    TermPtr domain;
    TermPtr codomain; // De Bruijn 0 is the bound variable
};

struct SigmaType {
    TermPtr first;
    TermPtr second; // Dependent on first
};

struct SumType {
    TermPtr left;
    TermPtr right;
};

struct Universe {
    size_t level;
};

// --- 2. Inductive Definitions (ADTs) ---

struct Constructor {
    std::vector<TermPtr> fields;
};

struct Mu {
    std::vector<Constructor> variants;
};

// --- 3. Terms ---

struct Variable {
    size_t index;
};

struct Lambda {
    TermPtr domain;
    TermPtr body;
};

struct App {
    TermPtr fn;
    TermPtr arg;
};

struct Pair {
    TermPtr first;
    TermPtr second;
};

struct Inject {
    TermPtr val;
    bool is_left;
};

// Explicit Recursor (Total Recursion)
struct Match {
    TermPtr discriminant;
    TermPtr motif; // Πx:T. Type
    struct Branch {
        size_t arity;
        TermPtr body;
    };
    std::vector<Branch> branches;
};

// --- 4. The AST Node ---

struct Term : public std::enable_shared_from_this<Term> {
    std::variant<
        Universe,
        Variable,
        PiType,
        SigmaType,
        SumType,
        Mu,
        Lambda,
        App,
        Pair,
        Inject,
        Match
    > node;

    // A=>IR(A) Mapping support
    template<typename T>
    static TermPtr from(T&& val) { return std::make_shared<Term>(std::forward<T>(val)); }

    // [Ir](){return this}
    TermPtr ir() { return shared_from_this(); }
};

// Helper for variables
inline TermPtr Var(size_t i) { return Term::from(Variable{i}); }

} // namespace ir
