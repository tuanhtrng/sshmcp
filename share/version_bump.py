"""commit-msg hook: bump project(VERSION) per conventional commit."""
import re
import subprocess
import sys
from pathlib import Path


def main() -> int:
    message = Path(sys.argv[1]).read_text(
        encoding="utf-8").splitlines()
    first = message[0] if message else ""
    if first.startswith("Merge") or "[no bump]" in first:
        return 0
    root = Path(subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"],
        text=True).strip())
    cmakelists = root / "CMakeLists.txt"
    staged = subprocess.check_output(
        ["git", "diff", "--cached", "--", "CMakeLists.txt"],
        cwd=root, text=True)
    if "project(sshmcp VERSION" in staged:
        return 0  # version already changed in this commit
    text = cmakelists.read_text(encoding="utf-8")
    found = re.search(
        r"project\(sshmcp VERSION (\d+)\.(\d+)\.(\d+)", text)
    if not found:
        print("version_bump: no project(VERSION) found")
        return 1
    major, minor, patch = map(int, found.groups())
    header = first.split(":", 1)[0]
    body = "\n".join(message)
    if "!" in header or "BREAKING CHANGE" in body:
        major, minor, patch = major + 1, 0, 0
    elif header.startswith("feat"):
        minor, patch = minor + 1, 0
    else:
        patch += 1
    replacement = f"project(sshmcp VERSION {major}.{minor}.{patch}"
    cmakelists.write_text(
        text[:found.start()] + replacement + text[found.end():],
        encoding="utf-8")
    subprocess.run(["git", "add", "CMakeLists.txt"],
                   cwd=root, check=True)
    print(f"version_bump: {major}.{minor}.{patch}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
