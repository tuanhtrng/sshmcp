"""Bans macros, exceptions, unsafe calls, using-directives."""
import re
import sys
from pathlib import Path

RULES = [
    (re.compile(r"^\s*#\s*(?!pragma\s+once\b|include\b)\w+"),
     "preprocessor directive (only #pragma once, #include)"),
    (re.compile(r"\bsystem\s*\("), "system()"),
    (re.compile(r"\b_?popen\b"), "popen"),
    (re.compile(r"std::regex"), "std::regex"),
    (re.compile(r"\bnew\s+[A-Za-z_(]"), "raw new"),
    (re.compile(r"\bdelete\b\s*(\[\s*\])?\s*[A-Za-z_(]"),
     "raw delete"),
    (re.compile(r"\busing\s+namespace\b"), "using namespace"),
    (re.compile(r"\busing\s+[\w:]+\s*;"),
     "using-declaration (alias form 'using x = y;' is ok)"),
    (re.compile(r"\bthrow\b"), "throw"),
    (re.compile(r"\btry\s*\{"), "try block"),
    (re.compile(r"\bcatch\s*\("), "catch"),
]
ENUM_COLLISION = re.compile(
    r"enum\s+class[^;{]*\{[^}]*"
    r"\b(ERROR|IGNORE|DELETE|SMALL|TRUE|FALSE|INFINITE)\b",
    re.DOTALL)
SCAN = ("app", "include/sshmcp", "regress/unit")


def strip_comment(line: str) -> str:
    return line.split("//", 1)[0]


def main() -> int:
    root = Path(sys.argv[1])
    bad = []
    for base in SCAN:
        for path in sorted((root / base).rglob("*")):
            if path.suffix not in (".hpp", ".cpp"):
                continue
            text = path.read_text(encoding="utf-8")
            rel = path.relative_to(root).as_posix()
            for number, raw in enumerate(
                    text.splitlines(), 1):
                if "lint-ok" in raw:
                    continue
                line = strip_comment(raw)
                for rule, label in RULES:
                    if rule.search(line):
                        bad.append(f"{rel}:{number}: {label}")
            if ENUM_COLLISION.search(text):
                bad.append(f"{rel}: macro-colliding enumerator")
    for item in bad:
        print(item)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
