#!/bin/sh
# Rechecks each saved failure with the current compiler and reduces the
# ones that still fail, smallest program first. Prints a summary line per
# seed; reduced programs land in failures/seed_N_reduced.*.
cd "$(dirname "$0")"
order=$(for j in "$@"; do python3 -c "
import json,fuzz
p=json.load(open('failures/seed_$j.json')); print(len(fuzz.statement_slots(p))*10+len(fuzz.expression_slots(p)), $j)"; done | sort -n | awk '{print $2}')
for s in $order; do
    st=$(python3 -c "
import json,fuzz
g,q=fuzz.tool_paths(); c=fuzz.build_compiler()
p=json.load(open('failures/seed_$s.json'))
st,d=fuzz.run_program(fuzz.render(p,False),fuzz.render(p,True),c,g,q)
print(st)" 2>/dev/null | tail -1)
    if [ "$st" = "ok" ]; then echo "seed $s: now passes"; continue; fi
    echo "seed $s: $st -- reducing"
    printf '%s\n' "$st" > failures/seed_$s.txt
    python3 fuzz.py --reduce failures/seed_$s.json --jobs 8 2>&1 | tail -1
done
echo "queue done"
