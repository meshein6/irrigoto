#!/usr/bin/env python3
"""Regenerate components/irrigoto/*_html.h from html/*.html sources.

The .html files (in this directory) are the source of truth; the *_html.h
files ONE LEVEL UP (component root) are what the firmware embeds (C++
raw-string literals). Edit the .html, run this script, commit both files.
`regen.py --check` exits nonzero if any pair is out of sync without
rewriting anything.

The generated headers live at the component ROOT (not here) so ESPHome's
standard source copy ships them into the build tree next to irrigoto.c —
see the include-path note in ../__init__.py (GitHub issue #4: ESPHome
2026.7's native-IDF builder drops add_build_flag -I flags, which broke
the old keep-them-in-html/ approach).

History: these pairs drifted when edits went straight into the .h --
zone_setup.html was ~220 lines behind its header by b435, and cal/fs/landing
had no .html source at all until they were re-extracted from the headers.
"""
import re
import shutil
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).parent          # html/ — .html sources
OUT = HERE.parent                     # component root — *_html.h payloads
WRAP = re.compile(r'R"([A-Z]+)\(\r?\n(.*)\)\1"', re.S)


# b564: this check was silently skipped whenever node was absent, which on a
# Mac without a JS toolchain is always -- so the guard the docstring below
# describes had not actually run in a long time. macOS ships JavaScriptCore,
# which parses the same language, so fall back to it rather than skipping.
# Skipping quietly is how the b525 bug reached three units in the first place.
_JSC = ("/System/Library/Frameworks/JavaScriptCore.framework"
        "/Versions/A/Helpers/jsc")


def js_checker():
    """Return fn(path) -> error string or "", or None if nothing can check."""
    if shutil.which("node"):
        def _node(f):
            r = subprocess.run(["node", "--check", str(f)],
                               capture_output=True, text=True)
            return "" if r.returncode == 0 else \
                chr(10).join(r.stderr.strip().splitlines()[:4])
        return _node
    if os.path.exists(_JSC):
        def _jsc(f):
            # new Function() parses without executing -- we want syntax only.
            probe = ("try { new Function(readFile(%r)); }"
                     "catch (e) { print('ERR ' + e); }" % str(f))
            r = subprocess.run([_JSC, "-e", probe], capture_output=True, text=True)
            out = (r.stdout or "").strip()
            return out[4:].strip() if out.startswith("ERR ") else ""
        return _jsc
    return None


def js_syntax_check(html_path) -> list:
    """Parse each inline <script> with node --check.

    b527: a JS syntax error in one of these pages takes out the WHOLE inline
    script block, so the page renders as bare markup -- zone list stuck on
    "Loading...", every device field showing an em dash, no button doing
    anything. The firmware still compiles and the header still regenerates
    (it's just text to them), so nothing else in this repo notices. That is
    exactly how a broken string literal reached three units in b525: a newline
    escape in a confirm() string had become a REAL newline, which a JS string
    cannot span. Cheap to check, invisible until someone opens the page.

    Returns a list of error strings; empty means clean.
    """
    chk = js_checker()
    if chk is None:
        return []
    src = html_path.read_text(encoding="utf-8")
    errors = []
    with tempfile.TemporaryDirectory() as td:
        for i, block in enumerate(re.findall(r"<script>(.*?)</script>", src, re.S)):
            f = Path(td) / f"{html_path.stem}_{i}.js"
            f.write_text(block, encoding="utf-8")
            err = chk(f)
            if err:
                errors.append(f"{html_path.name} <script> #{i}:{chr(10)}{err}")
    return errors


def main() -> int:
    check = "--check" in sys.argv
    fail = False
    for html in sorted(HERE.glob("*.html")):
        for err in js_syntax_check(html):
            print(f"JS SYNTAX ERROR -- {err}")
            fail = True
    # b535: standalone .js files get the same check.
    chk = js_checker()
    if chk is None:
        print("WARNING: no JavaScript parser found (no node, no jsc) -- "
              "the pages were NOT syntax-checked.")
    else:
        for js in sorted(HERE.glob("*.js")):
            err = chk(js)
            if err:
                print(f"JS SYNTAX ERROR -- {js.name}:{chr(10)}{err}")
                fail = True
    if fail:
        print("Refusing to regenerate: fix the JavaScript first.")
        return 1
    # b535: *_js.h <- *.js alongside *_html.h <- *.html. Shared browser code
    # (path.js) is served as its own file, so all three pages can use one copy
    # instead of each embedding a duplicate.
    pairs = [(h, HERE / h.name.replace("_html.h", ".html"))
             for h in sorted(OUT.glob("*_html.h"))]
    pairs += [(h, HERE / h.name.replace("_js.h", ".js"))
              for h in sorted(OUT.glob("*_js.h"))]
    for h, html in pairs:
        raw = h.read_bytes().decode("utf-8")
        m = WRAP.search(raw)
        if not m:
            print(f"{h.name}: cannot parse raw-string wrapper")
            fail = True
            continue
        if not html.exists():
            print(f"{h.name}: MISSING source {html.name} -- extract it from the header body")
            fail = True
            continue
        body = html.read_bytes().decode("utf-8")
        # The include guard keeps the fragment inert when included outside
        # irrigoto.c's payload sites (ESPHome 2026.7+ esphome.h glob-includes
        # every copied component header — GitHub issue #4). Headers lacking
        # it are stale even if the body matches.
        guarded = "#ifdef IRRIGOTO_HTML_PAYLOAD" in raw
        if guarded and m.group(2).replace("\r\n", "\n") == body.replace("\r\n", "\n"):
            print(f"{h.name}: in sync with {html.name}")
            continue
        if not guarded and check:
            print(f"{h.name}: missing IRRIGOTO_HTML_PAYLOAD guard")
            fail = True
            continue
        if check:
            print(f"{h.name}: OUT OF SYNC with {html.name}")
            fail = True
            continue
        delim = m.group(1)
        new = (
            f"/* Auto-generated from {html.name} -- edit the .html, then run regen.py */\n"
            "#ifdef IRRIGOTO_HTML_PAYLOAD\n"
            f'R"{delim}(\n{body}'
            + ("" if body.endswith("\n") else "\n")
            + f'){delim}"\n'
            "#endif /* IRRIGOTO_HTML_PAYLOAD */\n"
        )
        h.write_bytes(new.encode("utf-8"))
        print(f"{h.name}: regenerated from {html.name}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
