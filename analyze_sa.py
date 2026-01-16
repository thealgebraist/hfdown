import re
import random
import math
from collections import defaultdict

def parse_asm(asm_file):
    # Matches addr: hex mnemonic operands
    inst_re = re.compile(r'^\s*[0-9a-f]+:\s+[0-9a-f]+\s+(\w+)\s+(.*)$')
    instructions = []
    with open(asm_file, 'r') as f:
        for line in f:
            m = inst_re.match(line)
            if m:
                op, args = m.group(1), m.group(2)
                # Track registers used/defined (simplified)
                defs = set(re.findall(r'[xw][0-9]+', args.split(',')[0]) if ',' in args else [])
                uses = set(re.findall(r'[xw][0-9]+', args)) - defs
                instructions.append({'op': op, 'args': args, 'defs': defs, 'uses': uses})
    return instructions

def is_mergeable(i1, i2):
    # Rule 1: CMP + B.cond -> CBZ/CBNZ
    if i1['op'] == 'cmp' and i2['op'] in ['b.eq', 'b.ne']:
        if '0' in i1['args']: return True
    # Rule 2: LDR/STR pairs for LDP/STP
    if i1['op'] == i2['op'] and i1['op'] in ['ldr', 'str']:
        m1 = re.search(r'\[(\w+)', i1['args'])
        m2 = re.search(r'\[(\w+)', i2['args'])
        if m1 and m2 and m1.group(1) == m2.group(1): return True
    # Rule 3: MOV + ADD -> ADD (immediate)
    if i1['op'] == 'mov' and i2['op'] == 'add':
        if i1['defs'].intersection(i2['uses']): return True
    return False

def simulated_annealing(instructions):
    # Potential merge candidates (adjacent or can be made adjacent)
    candidates = []
    for i in range(len(instructions) - 1):
        # Check immediate adjacency first
        if is_mergeable(instructions[i], instructions[i+1]):
            candidates.append((i, i+1))
    
    # SA State: binary vector of which candidates to merge
    current_state = [random.random() > 0.5 for _ in range(len(candidates))]
    
    def get_energy(state):
        # Count non-conflicting merges
        merged_indices = set()
        count = 0
        for i, active in enumerate(state):
            if active:
                c1, c2 = candidates[i]
                if c1 not in merged_indices and c2 not in merged_indices:
                    count += 1
                    merged_indices.add(c1)
                    merged_indices.add(c2)
        return -count # Minimize negative count

    current_energy = get_energy(current_state)
    best_state = list(current_state)
    best_energy = current_energy
    
    temp = 10.0
    cooling_rate = 0.995
    
    for _ in range(2000):
        # Move: Flip a random bit
        idx = random.randint(0, len(candidates) - 1)
        current_state[idx] = not current_state[idx]
        new_energy = get_energy(current_state)
        
        if new_energy < current_energy or random.random() < math.exp((current_energy - new_energy) / temp):
            current_energy = new_energy
            if current_energy < best_energy:
                best_energy = current_energy
                best_state = list(current_state)
        else:
            current_state[idx] = not current_state[idx] # Backtrack
            
        temp *= cooling_rate

    return -best_energy

if __name__ == "__main__":
    instrs = parse_asm('build/hfdown_full.asm')
    reduction = simulated_annealing(instrs)
    
    print(f"--- Simulated Annealing Instruction Equivalence Analysis ---")
    print(f"Total Instructions: {len(instrs)}")
    print(f"Mergeable pairs found: {reduction}")
    print(f"Instruction Reduction: {(reduction/len(instrs))*100:.2f}%")
    print(f"New Code Size Estimate: {len(instrs) - reduction} instructions")
    
    print("\nMerge logic includes:")
    print(" - Comparison + Branch -> CBZ/CBNZ")
    print(" - Load/Store Sequences -> LDP/STP")
    print(" - Mov + Add -> Add (immediate)")
