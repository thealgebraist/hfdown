import numpy as np
from scipy.optimize import linprog

def solve_refined_frontier():
    # Hardware Parameters
    L1_LATENCY = 1.0      # cycle
    L2_LATENCY = 12.0     # cycles
    RAM_LATENCY = 250.0   # cycles
    BR_MIS_PENALTY = 16.0 # cycles
    BASE_CPI = 0.25       # 4-wide execution

    # CURRENT REALITY (from analyze_asm.py)
    # Binary is dominated by StdLib/Templates (~35k instructions)
    # Total unique instructions in binary: ~60,000
    # Let's assume the "Hot Loop" + dependencies is ~40,000 instructions
    total_instr = 40000 
    total_branches = total_instr * 0.15
    
    # L1 I-Cache Capacity: 32KB = 8,192 instructions
    L1_CAP = 8192
    
    # Variables: x0 (L1 hit), x1 (L2 hit), x2 (RAM), x3 (Correct Br), x4 (MisBr)
    c = [0, L2_LATENCY - L1_LATENCY, RAM_LATENCY - L1_LATENCY, 0, BR_MIS_PENALTY]

    # Current State Estimation:
    # Most instructions (>80%) are MISSING L1 because the hot set (40k) is 5x larger than L1 (8k).
    curr_l1_hits = L1_CAP
    curr_l2_hits = (total_instr - L1_CAP) * 0.7
    curr_ram_hits = (total_instr - L1_CAP) * 0.3
    curr_misbr = total_branches * 0.12 # 12% misprediction in bloated code
    
    curr_cycles = (total_instr * BASE_CPI) + \
                  (curr_l1_hits * L1_LATENCY) + \
                  (curr_l2_hits * L2_LATENCY) + \
                  (curr_ram_hits * RAM_LATENCY) + \
                  (curr_misbr * BR_MIS_PENALTY)

    # FRONTIER STATE (Shrinking hot path to fit in L1)
    # Target: total_instr_hot = 8000
    target_instr = 8000
    target_branches = target_instr * 0.10 # Lower branch density in SIMD/Flat code
    
    A_eq = [[1, 1, 1, 0, 0], [0, 0, 0, 1, 1]]
    b_eq = [target_instr, target_branches]
    A_ub = [[1, 0, 0, 0, 0], [0, 0, 0, 0, -1]]
    b_ub = [L1_CAP, -0.02 * target_branches] # 2% mispredict floor

    res = linprog(c, A_ub=A_ub, b_ub=b_ub, A_eq=A_eq, b_eq=b_eq, method='highs')
    opt_cycles = (target_instr * BASE_CPI) + (target_instr * L1_LATENCY) + res.fun

    print(f"--- Code Size & Branch Prediction Frontier ---")
    print(f"Current Working Set: {total_instr} instructions (~{total_instr*4/1024:.1f} KB)")
    print(f"Target Working Set:  {target_instr} instructions (~{target_instr*4/1024:.1f} KB)")
    print(f"\nEstimated Cycles (Current): {curr_cycles:,.0f}")
    print(f"Estimated Cycles (Target):  {opt_cycles:,.0f}")
    print(f"Potential Speedup: {curr_cycles/opt_cycles:.2f}x")
    
    print(f"\nBreakdown of Gains:")
    cache_gain = (curr_l2_hits * L2_LATENCY + curr_ram_hits * RAM_LATENCY) - res.fun
    branch_gain = (curr_misbr - res.x[4]) * BR_MIS_PENALTY
    print(f"  From Cache Locality:   {cache_gain/curr_cycles*100:.1f}% reduction")
    print(f"  From Branch Predictor: {branch_gain/curr_cycles*100:.1f}% reduction")

if __name__ == "__main__":
    solve_refined_frontier()
