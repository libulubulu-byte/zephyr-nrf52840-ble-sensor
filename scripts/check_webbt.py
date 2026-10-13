#!/usr/bin/env python3
"""
Static checks for webbt/index.html.

No JavaScript engine exists on this machine, so the page cannot be executed
here. These are the checks that are still worth doing statically, chosen for the
failure modes that actually bite in a single-file page:

  1. '</script' inside the script body  - truncates it, page half-loads
  2. element ids referenced from JS that do not exist in the HTML
     - the classic: a typo makes getElementById return null and the whole
       interaction dies silently, with nothing in the console unless you look
  3. duplicate ids
  4. unbalanced brackets / parens / braces in the script
  5. references to CONFIG./CTRL. members that were never defined
  6. 'await' inside a function that is not declared async

Run:  python3 scripts/84_static_check.py
"""

import re
import sys
import pathlib

PAGE = pathlib.Path(__file__).resolve().parent.parent / "webbt" / "index.html"

problems = []
notes = []

src = PAGE.read_text(encoding="utf-8")

# --- split the page -----------------------------------------------------------
m = re.search(r"<script>(.*?)</script>", src, re.S)
if not m:
    print("FAIL: no <script> block found")
    sys.exit(1)
js = m.group(1)
html = src[:m.start()] + src[m.end():]

notes.append("script block: %d bytes, page: %d bytes" % (len(js), len(src)))

# --- 1. '</script' inside the body -------------------------------------------
if "</script" in js.lower():
    problems.append("script body contains '</script' - it would be truncated")

# --- 2/3. ids ----------------------------------------------------------------
html_ids = re.findall(r"\bid\s*=\s*\"([^\"]+)\"", html)
dupes = {i for i in html_ids if html_ids.count(i) > 1}
if dupes:
    problems.append("duplicate ids in HTML: %s" % ", ".join(sorted(dupes)))

referenced = set(re.findall(r"\$\(\s*'([^']+)'\s*\)", js))
referenced |= set(re.findall(r"getElementById\(\s*'([^']+)'\s*\)", js))

missing = sorted(referenced - set(html_ids))
if missing:
    problems.append("JS references ids that do not exist: %s" % ", ".join(missing))
notes.append("ids in HTML: %d, referenced from JS: %d" % (len(html_ids), len(referenced)))

# --- 4. bracket balance ------------------------------------------------------
def strip_literals(code: str) -> str:
    """Remove strings and comments so brackets inside them do not count."""
    out = []
    i, n = 0, len(code)
    while i < n:
        c = code[i]
        if c == "/" and i + 1 < n and code[i + 1] == "/":
            while i < n and code[i] != "\n":
                i += 1
        elif c == "/" and i + 1 < n and code[i + 1] == "*":
            i += 2
            while i + 1 < n and not (code[i] == "*" and code[i + 1] == "/"):
                i += 1
            i += 2
        elif c in "'\"`":
            quote = c
            i += 1
            while i < n:
                if code[i] == "\\":
                    i += 2
                    continue
                if code[i] == quote:
                    i += 1
                    break
                i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)

clean = strip_literals(js)
pairs = {")": "(", "]": "[", "}": "{"}
stack = []
for ch in clean:
    if ch in "([{":
        stack.append(ch)
    elif ch in ")]}":
        if not stack or stack[-1] != pairs[ch]:
            problems.append("unbalanced '%s' in script" % ch)
            break
        stack.pop()
if stack:
    problems.append("unclosed bracket(s) in script: %s" % "".join(stack))

# --- 5. CONFIG / CTRL members ------------------------------------------------
for obj in ("CONFIG", "CTRL"):
    blocks = re.findall(r"\b%s\s*=\s*\{(.*?)\n\};" % obj, js, re.S)
    if not blocks:
        problems.append("%s is referenced but never defined" % obj)
        continue
    keys = set(re.findall(r"(\w+)\s*:", blocks[0]))
    used = set(re.findall(r"\b%s\.(\w+)" % obj, js))
    undef = sorted(used - keys)
    if undef:
        problems.append("%s members used but never defined: %s" % (obj, ", ".join(undef)))

# --- 6. await outside async --------------------------------------------------
funcs = re.findall(r"(async\s+)?function\s+(\w+)\s*\([^)]*\)\s*\{", js)
# Cheap and approximate: report every function that contains await but is not
# declared async. Arrow functions are not covered; the page only awaits inside
# the named ones.
for decl, name in funcs:
    body_start = js.find("function %s" % name)
    # crude brace walk to find the body
    i = js.find("{", body_start)
    depth, j = 0, i
    while j < len(js):
        if js[j] == "{":
            depth += 1
        elif js[j] == "}":
            depth -= 1
            if depth == 0:
                break
        j += 1
    body = js[i:j]
    if "await " in body and not decl.strip():
        problems.append("function '%s' uses await but is not declared async" % name)

notes.append("named functions found: %d" % len(funcs))

# --- 7. all user-visible text is English -------------------------------------
# The requirement is "the page is English", and the way it was verified on the
# previous project for this hardware was to scan for CJK codepoints and expect
# none. Doing it here rather than in a note is the difference between a
# requirement and a wish: it cannot quietly rot back to mixed-language.
cjk = re.findall(r"[\u4e00-\u9fff\u3000-\u303f\uff00-\uffef]", src)
if cjk:
    problems.append("page contains %d CJK character(s): %s"
                    % (len(cjk), " ".join(sorted(set(cjk)))))
else:
    notes.append("no CJK characters: the page is English-only")

# --- report ------------------------------------------------------------------
print("=== notes ===")
for n in notes:
    print("  " + n)
print()
if problems:
    print("=== PROBLEMS ===")
    for p in problems:
        print("  x " + p)
    sys.exit(1)
print("=== static checks: PASS ===")
