import re
import numpy as np
from scipy.optimize import milp, LinearConstraint, Bounds

def find_merge_opportunities(asm_file):
    # Regex for ARM64 instructions: addr: hex mnemonic operands
    # Example: 10003d624: a9ba6ffc     stp     x28, x27, [sp, #-0x60]!
    # Example: 10003d6a4: f940e3e8     ldr     x8, [sp, #0x1c0]
    inst_re = re.compile(r'^\s*[0-9a-f]+:\s+[0-9a-f]+\s+(\w+)\s+(.*)$')
    
    instructions = []
    with open(asm_file, 'r') as f:
        for line in f:
            m = inst_re.match(line)
            if m:
                instructions.append({'op': m.group(1), 'args': m.group(2)})

    candidates = []
    # Heuristic: Look for consecutive LDR or STR instructions to the same base register with adjacent offsets
    # This is a classic pattern for LDP/STP (Load/Store Pair) merging.
    for i in range(len(instructions) - 1):
        inst1 = instructions[i]
        inst2 = instructions[i+1]
        
        if inst1['op'] == inst2['op'] and inst1['op'] in ['ldr', 'str']:
            # Simplified parsing: look for [reg, #offset]
            # args example: x8, [sp, #0x1c0]
            m1 = re.search(r'(\w+),\s+\[(\w+)(?:,\s+#(-?0x[0-9a-f]+|[0-9]+))?\]', inst1['args'])
            m2 = re.search(r'(\w+),\s+\[(\w+)(?:,\s+#(-?0x[0-9a-f]+|[0-9]+))?\]', inst2['args'])
            
            if m1 and m2:
                reg1, base1, off1 = m1.groups()
                reg2, base2, off2 = m2.groups()
                
                if base1 == base2:
                    # Potential merge if they are adjacent
                    # (This is a simplified check for the MILP demonstration)
                    candidates.append((i, i+1))

    return len(instructions), candidates

def solve_milp_reduction(total_instr, candidates):
    if not candidates:
        return 0

    num_vars = len(candidates)
    
    # Objective: Maximize reduction (each chosen merge reduces instruction count by 1)
    # scipy.optimize.milp minimizes c^T * x, so c = [-1, -1, ...]
    c = np.full(num_vars, -1.0)
    
    # Constraints: No instruction can be part of two merges
    # If candidate j merges (i, i+1) and candidate k merges (i+1, i+2), they conflict.
    
    # Build conflict matrix
    constraints = []
    for j in range(num_vars):
        for k in range(j + 1, num_vars):
            set_j = set(candidates[j])
            set_k = set(candidates[k])
            if set_j.intersection(set_k):
                # Conflict! x_j + x_k <= 1
                row = np.zeros(num_vars)
                row[j] = 1
                row[k] = 1
                constraints.append(row)
    
    if not constraints:
        # No conflicts, all can be merged
        return num_vars

    A = np.array(constraints)
    b_u = np.ones(len(constraints))
    b_l = np.full(len(constraints), -np.inf)
    
    # All variables are binary (0 or 1)
    integrality = np.ones(num_vars) 
    bounds = Bounds(0, 1)
    
    res = milp(c=c, constraints=LinearConstraint(A, b_l, b_u), 
               integrality=integrality, bounds=bounds)
    
    if res.success:
        return int(-res.fun)
    return 0

if __name__ == "__main__":
    asm_file = 'build/hfdown_full.asm'
    total, candidates = find_merge_opportunities(asm_file)
    reduction = solve_milp_reduction(total, candidates)
    
    print(f"--- Mixed-Integer Linear Programming (MILP) Analysis ---")
    print(f"Total Instructions analyzed: {total}")
    print(f"Potential merge locations found: {len(candidates)}")
    print(f"Optimal merges (non-overlapping): {reduction}")
    print(f"Theoretical Size Reduction: {(reduction/total)*100:.2f}%")
    
    if reduction > 0:
        print(f"\nExample mergeable patterns:")
        for i in range(min(5, len(candidates))):
            idx1, idx2 = candidates[i]
            print(f"  Merge [{idx1}, {idx2}]")
