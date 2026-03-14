import sys
from collections import defaultdict

totals = defaultdict(float)

for line in sys.stdin:
    line = line.strip()
    if not line:
        continue

    # Expected format: "<Category>: <time>ms for <path>"
    category, rest = line.split(":", 1)
    time_str = rest.split("ms", 1)[0].strip()
    time_ms = float(time_str)
    totals[category] += time_ms

total_time = sum(totals.values())

for category, total in totals.items():
    percent = (total / total_time) * 100 if total_time > 0 else 0
    print(f"{category}: {total:.3f} ms ({percent:.2f}%)")
