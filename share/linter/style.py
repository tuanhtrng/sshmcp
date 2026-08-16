"""Best-effort AAA + trailing-return checks. // style-ok escapes."""
import re
import sys
from pathlib import Path

NON_AUTO_LOCAL = re.compile(
    r"^\s*(?:std::[\w:<>, ]+|[a-z_]\w*_t)\s+\w+\s*=")
NON_TRAILING_FN = re.compile(
    r"^\s*(?:inline\s+|static\s+|constexpr\s+)*"
    r"(?!auto\b|return\b|using\b|namespace\b|template\b|"
    r"if\b|for\b|while\b|else\b|case\b|struct\b|class\b|"
    r"enum\b|public\b|private\b|protected\b)"
    r"[\w:]+(?:<[^;{]*>)?\s+\w+\s*\([^;]*\)\s*"
    r"(?:const\s*)?\{")
SCAN = ("app", "include/sshmcp", "regress/unit")


def main() -> int:
    root = Path(sys.argv[1])
    bad = []
    for base in SCAN:
        for path in sorted((root / base).rglob("*")):
            if path.suffix not in (".hpp", ".cpp"):
                continue
            rel = path.relative_to(root).as_posix()
            lines = path.read_text(encoding="utf-8").splitlines()
            for number, raw in enumerate(lines, 1):
                if "style-ok" in raw:
                    continue
                line = raw.split("//", 1)[0]
                if NON_AUTO_LOCAL.match(line):
                    bad.append(
                        f"{rel}:{number}: use AAA "
                        f"(auto x = type{{...}})")
                if NON_TRAILING_FN.match(line):
                    bad.append(
                        f"{rel}:{number}: use trailing return "
                        f"(auto f() -> type)")
    for item in bad:
        print(item)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
