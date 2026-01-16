import numpy as np
from scipy.optimize import linprog

def estimate_library_specialization():
    # We want to minimize the functional set of instructions by assuming 
    # everything else can be stripped if we specialize to the exact usage.
    
    # x0: libcurl used
    # x1: libcurl waste
    # x2: OpenSSL used
    # x3: OpenSSL waste
    # x4: ngtcp2 used
    # x5: ngtcp2 waste
    
    # Target: Minimize the SUM of the instructions actually included in the hot path.
    # Objective: min (x0 + x2 + x4)
    # The 'waste' variables x1, x3, x5 represent the potential for removal.
    c = [1, 0, 1, 0, 1, 0]

    CURL_TOTAL = 150
    OSSL_TOTAL = 800
    QUIC_TOTAL = 50

    # Equality Constraints (A_eq * x = b_eq)
    # Total = Used + Waste
    A_eq = [
        [1, 1, 0, 0, 0, 0], 
        [0, 0, 1, 1, 0, 0], 
        [0, 0, 0, 0, 1, 1]  
    ]
    b_eq = [CURL_TOTAL, OSSL_TOTAL, QUIC_TOTAL]

    # Inequality Constraints (Lower bounds for functional correctness)
    A_ub = [
        [-1, 0, 0, 0, 0, 0], # x0 >= 0.15 * CURL (min set for GET/H2)
        [0, 0, -1, 0, 0, 0], # x2 >= 0.05 * OSSL (min set for TLS1.3/SHA256)
        [0, 0, 0, 0, -1, 0], # x4 >= 0.40 * QUIC (min set for H3)
    ]
    b_ub = [-0.15 * CURL_TOTAL, -0.05 * OSSL_TOTAL, -0.40 * QUIC_TOTAL]

    res = linprog(c, A_ub=A_ub, b_ub=b_ub, A_eq=A_eq, b_eq=b_eq, method='highs')

    total_curr = CURL_TOTAL + OSSL_TOTAL + QUIC_TOTAL
    total_opt = res.fun
    saved = total_curr - total_opt

    print(f"--- Library Specialization LP Estimate (Refined) ---")
    print(f"Current Total Library Instructions: {total_curr}k")
    print(f"Specialized Hot Path Size:         {total_opt:.1f}k")
    print(f"Total Instructions Savable:        {saved:.1f}k")
    print(f"Instruction Reduction:             {(saved/total_curr)*100:.1f}%")

    print(f"\nOptimization Breakdown:")
    print(f"  libcurl:  {res.x[1]:.1f}k instructions removed (Stripping Proxies, FTP, SMTP, IMAP, etc)")
    print(f"  OpenSSL:  {res.x[3]:.1f}k instructions removed (Stripping RSA, DSA, AES-CBC, Legacy TLS)")
    print(f"  ngtcp2:   {res.x[5]:.1f}k instructions removed (Stripping unused Congestion Control/Debug)")

if __name__ == "__main__":
    estimate_library_specialization()