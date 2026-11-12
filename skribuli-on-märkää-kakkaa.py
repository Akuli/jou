import os
import re
import glob

errors = {}

def identify_error(text):
    m = re.search(r'(at|by) 0x[0-9A-F]+: (llvm::.*) \(in /usr/.*\)', text)
    if m:
        return m.group(2)

    if (
        text.startswith("Source and destination overlap in memcpy")
        and '/home/runner/work/jou/jou/examples/aoc2023/day12/jou_compiled/part2' in text
    ):
        return "aoc memcpy"

    print("Could not identify:", text)
    print()
    print()
    text = re.sub(r'^   at 0x[0-9A-F]+:', '   at ...:', text, flags=re.MULTILINE)
    text = re.sub(r'^   by 0x[0-9A-F]+:', '   by ...:', text, flags=re.MULTILINE)
    return text

for path in sorted(glob.glob("valgrind-logs*/valgrind*arm*/6_*")):
    errors[path] = set()
    with open(path, "r") as file:
        error = []
        for line in file:
            m = re.search(r'==[0-9]+== ([^\x1b]*)', line)
            if m:
                text = m.group(1)
                if re.fullmatch(r'Thread [0-9]+:', text):
                    continue

                if not text:
                    if error:
                        errors[path].add(identify_error('\n'.join(error)))
                    error = []
                    continue

                error.append(text)

for path, errors in errors.items():
    print(path.split('(')[1].split(')')[0])
    for error_id in sorted(errors):
        print("  ", error_id.split('(')[0])
    print()
