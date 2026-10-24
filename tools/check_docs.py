#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Check the repository's documents for dangling links and banned text.

    python tools/check_docs.py

Two checks, over the files git tracks:

  links    every relative markdown link [text](path) in a .md file, and every
           backticked repository path in one (`docs/...`, `src/...` and the
           other top-level directories), must name a file or directory that
           exists. Anchors (#...) and :line suffixes are ignored, and so are
           http(s) links and paths holding a wildcard or a <placeholder>.
  banned   no tracked text file may hold a personal path, the owner's email
           address, a name from the private workspace, or an em-dash, and no
           markdown prose may use " -- " as a dash.

Each problem prints as path:line: message. The exit status is 1 when there is
any problem, so CI can run it.

docs/research/ is a dated working record: its notes cite fixture folders,
MANIFEST.md files and other paths in the author's private workspace, which its
README explains. Backticked paths are therefore not checked there; its markdown
links are.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir))

# Banned anywhere in a tracked text file.
BANNED = [
    (re.compile(r"lukef", re.I), "personal path or user name"),
    (re.compile(r"GPTReborn"), "private folder name"),
    (re.compile(r"[A-Za-z]:[\\/]+Users[\\/]"), "absolute user-profile path"),
    (re.compile(r"@gmail\.com", re.I), "personal email address"),
    (re.compile(r"lafayette", re.I), "private name"),
    (re.compile(r"TODELETE"), "private workspace folder"),
    (re.compile(r"CLEANROOM_PROBE_PLAN"), "private planning file"),
    (re.compile(chr(0x2014)), "em-dash (the house style uses none)"),
]

# Files exempt from the banned-text check, and why.
BANNED_EXEMPT = {
    "LICENSE",              # the AGPL's own text, unmodified
    "tools/check_docs.py",  # this file names the patterns it bans
}
# Third-party code is checked for links only where it is ours to edit.
BANNED_EXEMPT_DIRS = ("third_party/",)

TOP_DIRS = ("docs/", "src/", "include/", "app/", "tools/", "tests/", "plugins/",
            "third_party/", "examples/", ".github/")
TEXT_EXT = (".md", ".cpp", ".h", ".c", ".inc", ".py", ".txt", ".yml", ".yaml",
            ".json", ".csv", ".cmake", ".patch", ".gitignore", ".gitattributes")

# A double hyphen standing for a dash in markdown prose; code spans are removed
# before this is applied, so `cmake --build` and `-- -j8` are not caught.
ASCII_DASH = re.compile(r"(?<![-!<|\w])--(?=\s|$)")
TICK_SPAN = re.compile(r"`[^`]*`")
LINK = re.compile(r"(?<!!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
TICK = re.compile(r"`([^`\s]+)`")


def tracked():
    out = subprocess.run(["git", "ls-files"], cwd=ROOT, check=True,
                         capture_output=True, text=True).stdout
    return [f for f in out.split("\n") if f]


def clean_target(t):
    t = t.split("#", 1)[0]
    t = re.sub(r":\d+(-\d+)?$", "", t)          # path:line or path:line-line
    return t.rstrip(".,;")


def skip_target(t):
    return (not t or t.startswith(("http://", "https://", "mailto:"))
            or any(c in t for c in "<>*{}$") or t.startswith("~"))


def exists(base_dir, target):
    path = os.path.normpath(os.path.join(ROOT, base_dir, target))
    return os.path.exists(path)


def main():
    files = tracked()
    problems = []
    for f in files:
        full = os.path.join(ROOT, f)
        if not os.path.isfile(full) or not (f.endswith(TEXT_EXT) or f == "LICENSE"):
            continue
        try:
            lines = open(full, encoding="utf-8").read().split("\n")
        except UnicodeDecodeError:
            continue
        is_md = f.endswith(".md")
        research = f.startswith("docs/research/")
        in_fence = False
        for n, line in enumerate(lines, 1):
            if f not in BANNED_EXEMPT and not f.startswith(BANNED_EXEMPT_DIRS):
                for rx, why in BANNED:
                    if rx.search(line):
                        problems.append(f"{f}:{n}: {why}")
            if not is_md:
                continue
            if line.lstrip().startswith("```"):
                in_fence = not in_fence
                continue
            if in_fence:
                continue
            prose = TICK_SPAN.sub("", line)
            if f not in BANNED_EXEMPT and ASCII_DASH.search(prose):
                problems.append(f"{f}:{n}: ' -- ' used as a dash (the house style uses none)")
            for m in LINK.finditer(line):
                t = clean_target(m.group(1))
                if skip_target(t):
                    continue
                if not exists(os.path.dirname(f), t):
                    problems.append(f"{f}:{n}: broken link {m.group(1)}")
            if research:
                continue
            for m in TICK.finditer(line):
                t = clean_target(m.group(1))
                if not t.startswith(TOP_DIRS) or skip_target(t):
                    continue
                if not exists("", t):
                    problems.append(f"{f}:{n}: no such path `{m.group(1)}`")
    for p in problems:
        print(p)
    print(f"check_docs: {len(problems)} problem(s) in {len(files)} tracked files",
          file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
