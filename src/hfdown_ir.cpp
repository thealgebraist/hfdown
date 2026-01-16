#include "lambda_ir.hpp"

namespace hfdown {
namespace ir_gen {

using namespace ir;

/**
 * hfdown 1-1 IR Construction
 * This translates the program logic into the total lambda calculus AST.
 */
TermPtr build_hfdown_ir() {
    // --- 1. Standard Library ADTs (Structural) ---

    // Bool = Unit + Unit
    auto BoolType = Term::from(SumType{Term::from(Universe{0}), Term::from(Universe{0})});

    // Nat = μ. [ {Unit}, {Self} ]
    Mu nat_sig;
    nat_sig.variants = { {{}}, {{Var(0)}} };
    auto NatType = Term::from(nat_sig);

    // List A = μ. [ {Unit}, {A, Self} ]
    auto build_list = [](TermPtr A) {
        Mu list_sig;
        list_sig.variants = { {{}}, {{A, Var(0)}} };
        return Term::from(list_sig);
    };

    // --- 2. Program Specific Types ---

    // ModelFile = { name: String, size: Nat, oid: String }
    // Represented as a nested Sigma type: Σ(name: String). Σ(size: Nat). String
    auto ModelFileType = Term::from(SigmaType{
        Var(1), // Assume String is in ctx
        Term::from(SigmaType{NatType, Var(2)})
    });

    // HFError = μ. [ {Unit} x 16 ] (16 variants)
    Mu err_sig;
    for(int i=0; i<16; ++i) err_sig.variants.push_back({{}});
    auto ErrorType = Term::from(err_sig);

    // Result T = T + Error
    auto build_result = [&](TermPtr T) {
        return Term::from(SumType{T, ErrorType});
    };

    // --- 3. Functional Kernels (Pi Types) ---

    // get_model_info: Π(env: Env). Π(mid: String). Result (List ModelFile)
    // env_type is a large Sigma of all environment capabilities
    auto env_type = Term::from(SigmaType{BoolType, Term::from(Universe{0})}); 

    auto get_info_kernel = Term::from(Lambda{
        env_type,
        Term::from(Lambda{
            Var(10), // String
            Term::from(Match{
                Var(0), // discriminant: model_id
                Term::from(Universe{1}), // motif
                { {0, Term::from(Inject{Var(1), true})} } // Success case
            })
        })
    });

    // --- 4. The 1-1 Program "Term" ---
    // The entire hfdown logic is a single Lambda term taking the environment
    // and returning the final Success/Failure of the download operation.
    
    auto program_ir = Term::from(Lambda{
        env_type,
        Term::from(App{
            get_info_kernel,
            Var(0) // Apply to environment
        })
    });

    return program_ir;
}

} // namespace ir_gen
} // namespace hfdown
