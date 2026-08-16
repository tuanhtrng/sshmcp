"""clang-format --dry-run --Werror over our sources."""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


PINNED_MAJOR = 22  # repo is formatted with clang-format 22


def main() -> int:
    binary = os.environ.get("SSHMCP_CLANG_FORMAT",
                            "clang-format")
    if shutil.which(binary) is None:
        print("clang-format not found; skipping")
        return 77
    version = subprocess.check_output(
        [binary, "--version"], text=True)
    found = re.search(r"version (\d+)", version)
    if not found or int(found.group(1)) != PINNED_MAJOR:
        print(f"clang-format major != {PINNED_MAJOR} "
              f"({version.strip()}); skipping")
        return 77
    root = Path(sys.argv[1])
    files = []
    for pattern in ("app/**/*.cpp", "include/sshmcp/**/*.hpp",
                    "regress/unit/**/*.cpp",
                    "regress/unit/**/*.hpp"):
        files += [str(p) for p in sorted(root.glob(pattern))]
    result = subprocess.run(
        [binary, "--dry-run", "--Werror"] + files)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
