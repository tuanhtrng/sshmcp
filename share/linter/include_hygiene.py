"""#pragma once in headers; include rank order."""
import re
import sys
from pathlib import Path

INCLUDE = re.compile(r'^\s*#\s*include\s*(["<])([^">]+)[">]')
SCAN = ("app", "include/sshmcp", "regress/unit")


def rank(kind: str, target: str) -> int:
    if kind == '"':
        return 0
    if target.startswith("sshmcp/"):
        return 0
    if target.startswith("nlohmann/"):
        return 1
    return 2


def main() -> int:
    root = Path(sys.argv[1])
    bad = []
    for base in SCAN:
        for path in sorted((root / base).rglob("*")):
            if path.suffix not in (".hpp", ".cpp"):
                continue
            rel = path.relative_to(root).as_posix()
            lines = path.read_text(encoding="utf-8").splitlines()
            if path.suffix == ".hpp":
                first = next(
                    (line for line in lines if line.strip()), "")
                if first.strip() != "#pragma once":
                    bad.append(f"{rel}: missing #pragma once first")
            last = -1
            for number, line in enumerate(lines, 1):
                found = INCLUDE.match(line)
                if not found:
                    continue
                level = rank(found.group(1), found.group(2))
                if level < last:
                    bad.append(
                        f"{rel}:{number}: include out of order "
                        f"(sshmcp, nlohmann, rest)")
                last = max(last, level)
    for item in bad:
        print(item)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
