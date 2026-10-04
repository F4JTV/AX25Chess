#!/usr/bin/env python3
"""Static checks for build_all.bat against cmd.exe's parsing rules.

Two rules bite in practice and leave only "X was unexpected at this time":
  - inside a parenthesised block, an unquoted ')' in the text of an echo or
    rem closes the block ('(' in such text does NOT open one);
  - a line whose first argument is /? asks the command for its help
    (rem /?, echo /?) instead of being text.
Also reports CRLF-less files, which cmd.exe misparses.
"""
import re
import sys
from pathlib import Path

def check(path: Path) -> int:
    data = path.read_bytes()
    problems = []
    if b"\r\n" not in data:
        problems.append((0, "no CRLF line endings"))
    depth = 0
    for n, raw in enumerate(data.decode("utf-8", "replace").split("\r\n"), 1):
        line = raw.strip()
        if not line:
            continue
        unquoted = re.sub(r'"[^"]*"', '""', line).replace("^)", "").replace("^(", "")
        if re.match(r'(?i)^(rem|echo)\s+/\?(\s|$)', line):
            problems.append((n, "/? as the first argument of rem/echo"))
        is_text = re.match(r'(?i)^(echo|rem)\b', unquoted) is not None
        if is_text:
            if depth > 0 and ")" in unquoted:
                problems.append((n, "')' in echo/rem text inside a block closes it: " + line))
            continue
        # Block structure on other lines: '(' opens where cmd allows it (after
        # if/for/else or alone), ')' closes.
        for ch in unquoted:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth < 0:
                    problems.append((n, "stray ')'"))
                    depth = 0
    if depth != 0:
        problems.append((0, f"unbalanced blocks at end of file (depth {depth})"))
    for n, text in problems:
        print(f"{path.name}:{n}: {text}")
    return 1 if problems else 0

if __name__ == "__main__":
    root = Path(__file__).resolve().parent.parent
    rc = check(root / "build_all.bat")
    print("build_all.bat: no problem found" if rc == 0 else "build_all.bat: problems above")
    sys.exit(rc)
