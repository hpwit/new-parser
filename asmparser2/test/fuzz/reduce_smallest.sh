#!/bin/sh
# Reduces the saved failure with the fewest statements and prints it.
cd "$(dirname "$0")"
f=$(for j in failures/seed_*[0-9].json; do python3 -c "
import json,fuzz
p=json.load(open('$j')); print(len(fuzz.statement_slots(p))+len(fuzz.expression_slots(p))//10, '$j')"; done | sort -n | head -1 | awk '{print $2}')
echo "reducing $f"
python3 fuzz.py --reduce "$f" --jobs 8 | tail -1
base=$(echo "$f" | sed 's/.json$//')_reduced
awk '/^int f0|^void main/,0' $base.sc | grep -vE "^ *for \(int z|^ *[ABMN]\[z[01]|emit\([ABMN]\[z|^   g[0-2] = [0-9]+;|^   emit\(g[0-2]\);"
