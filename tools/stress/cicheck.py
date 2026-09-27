#!/usr/bin/env python3
# Usage: cicheck.py .github/workflows/build.yml SERIAL_LOG  (exit 0 = all Boot-test patterns present, no panic)
"""Apply the CI Boot-test serial.log grep patterns (build.yml) to a local log."""
import re, sys
wf, log = sys.argv[1], sys.argv[2]
text = open(log, 'rb').read().decode('latin1')
lines = open(wf).read().splitlines()
start = next(i for i, l in enumerate(lines) if 'name: Boot test' in l)
end = next(i for i in range(start + 1, len(lines)) if '- name:' in lines[i])
missing = []
for l in lines[start:end]:
    m = re.search(r'grep -(E?)q "([^"]+)" build/serial\.log', l) or re.search(r"grep -(E?)q '([^']+)' build/serial\.log", l)
    if not m or re.search(r"!\s*grep|if grep", l):
        continue
    pat = m.group(2)
    if not m.group(1):  # BRE: only . and * are special here
        pat = re.escape(pat).replace('\\.', '.').replace('\\*', '*')
    ok = re.search(pat, text)
    if not ok:
        missing.append(pat)
bad = re.findall(r'ZEROOS PANIC:[^\r\n]*|[^\r\n]*FAILED[^\r\n]*', text)
if bad: print('BAD:', bad[:3])
if missing: print('MISSING', len(missing), missing[:3])
sys.exit(1 if bad or missing else 0)
