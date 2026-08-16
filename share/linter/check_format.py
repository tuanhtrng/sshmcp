"""clang-format --dry-run --Werror over our sources."""
import shutil
import subprocess
import sys
from pathlib import Path


def main() -> int:
    if shutil.which("clang-format") is None:
        print("clang-format not found; skipping")
        return 77
    root = Path(sys.argv[1])
    files = []
    for pattern in ("app/**/*.cpp", "include/sshmcp/**/*.hpp",
                    "regress/unit/**/*.cpp",
                    "regress/unit/**/*.hpp"):
        files += [str(p) for p in sorted(root.glob(pattern))]
    result = subprocess.run(
        ["clang-format", "--dry-run", "--Werror"] + files)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
