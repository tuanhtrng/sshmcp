"""No .cpp outside app/ and regress/."""
import sys
from pathlib import Path

ALLOWED = ("app/", "regress/")


def main() -> int:
    root = Path(sys.argv[1])
    bad = []
    for path in root.rglob("*.cpp"):
        rel = path.relative_to(root).as_posix()
        if rel.startswith("build/"):
            continue
        if not rel.startswith(ALLOWED):
            bad.append(rel)
    for rel in bad:
        print(f"header-only violation: {rel}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
