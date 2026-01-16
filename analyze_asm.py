import re
import sys
from collections import defaultdict

# Regex to match function headers
func_re = re.compile(r'^[0-9a-f]+ <(.*)>:$')
# Regex to match instructions (simplistic for common ARM64 mnemonics)
inst_re = re.compile(r'^\s*[0-9a-f]+:\s+[0-9a-f]+\s+(\w+)')

def categorize(mnemonic):
    mnemonic = mnemonic.lower()
    # Categorize common ARM64 instructions
    if mnemonic in ['b', 'bl', 'blr', 'br', 'ret', 'cbz', 'cbnz', 'tbz', 'tbnz'] or mnemonic.startswith('b.'):
        return 'Branch/Call'
    if mnemonic in ['ldr', 'ldp', 'ldrb', 'ldrh', 'ldur', 'ldurb', 'str', 'stp', 'strb', 'strh', 'stur', 'sturb']:
        return 'Load/Store'
    if mnemonic in ['add', 'sub', 'mov', 'movk', 'movz', 'mvn', 'cmp', 'csel', 'cset', 'and', 'orr', 'eor', 'lsl', 'lsr', 'asr']:
        return 'ALU/Data'
    if mnemonic in ['fadd', 'fsub', 'fmul', 'fdiv', 'fmov', 'fcmp', 'fcvt', 'scvtf', 'ucvtf']:
        return 'FloatingPoint'
    if mnemonic in ['nop', 'hint', 'isb', 'dsb', 'dmb']:
        return 'System/Other'
    return 'Other'

stats = defaultdict(lambda: defaultdict(int))
current_func = "unknown"

with open('build/hfdown_full.asm', 'r') as f:
    for line in f:
        m = func_re.match(line)
        if m:
            current_func = m.group(1)
            continue
        
        m = inst_re.match(line)
        if m:
            mnemonic = m.group(1)
            cat = categorize(mnemonic)
            stats[current_func][cat] += 1
            stats[current_func]['total'] += 1

# Aggregate by module
module_stats = defaultdict(lambda: defaultdict(int))
for func, data in stats.items():
    module = "Other/StdLib"
    if "HuggingFaceClient" in func: module = "HuggingFace"
    elif "KaggleClient" in func: module = "Kaggle"
    elif "HttpClient" in func: module = "HTTP/1.1"
    elif "Http3Client" in func: module = "HTTP/3"
    elif "QuicSocket" in func: module = "QUIC/H3 Core"
    elif "AsyncFileWriter" in func: module = "IO/MMap"
    elif "SecretScanner" in func: module = "Security"
    elif "json::" in func: module = "JSON"
    elif "RsyncClient" in func: module = "Rsync"
    elif "std::" in func or "abi:ne" in func: module = "StdLib/Templates"
    
    for cat, count in data.items():
        module_stats[module][cat] += count

print("{:<18} | {:<8} | {:<8} | {:<8} | {:<8} | {:<8}".format('Module', 'Total', 'ALU', 'Mem', 'Branch', 'FP'))
print("-" * 75)

# Sort by total instructions
sorted_modules = sorted(module_stats.items(), key=lambda x: x[1]['total'], reverse=True)

for module, data in sorted_modules:
    print("{:<18} | {:<8} | {:<8} | {:<8} | {:<8} | {:<8}".format(
        module, 
        data['total'], 
        data['ALU/Data'], 
        data['Load/Store'], 
        data['Branch/Call'], 
        data['FloatingPoint']))

print("\nTop 10 Largest Functions:")
all_funcs = []
for func, data in stats.items():
    all_funcs.append((func, data['total']))
all_funcs.sort(key=lambda x: x[1], reverse=True)

for i in range(min(10, len(all_funcs))):
    print("{:>5} instructions : {}".format(all_funcs[i][1], all_funcs[i][0]))