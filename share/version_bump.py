"""post-commit hook: bump project(VERSION) per conventional commit.

Runs after the commit exists, then amends it with the bump. The
amend uses --no-verify; the re-entrant post-commit run exits early
because HEAD's version already differs from its parent's.
"""
import re
import subprocess
import sys
from pathlib import Path


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], text=True)


def parse_version(text: str):
    found = re.search(
        r"project\(sshmcp VERSION (\d+)\.(\d+)\.(\d+)", text)
    return tuple(map(int, found.groups())) if found else None


def main() -> int:
    parents = git("log", "-1", "--format=%P").split()
    if len(parents) != 1:
        return 0  # merge or root commit
    message = git("log", "-1", "--format=%B").splitlines()
    first = message[0] if message else ""
    if "[no bump]" in first:
        return 0
    root = Path(git("rev-parse", "--show-toplevel").strip())
    head = parse_version(git("show", "HEAD:CMakeLists.txt"))
    try:
        parent = parse_version(
            git("show", "HEAD~1:CMakeLists.txt"))
    except subprocess.CalledProcessError:
        parent = None
    if head is None:
        print("version_bump: no project(VERSION) found")
        return 1
    if parent is not None and head != parent:
        return 0  # this commit already bumped
    major, minor, patch = head
    header = first.split(":", 1)[0]
    body = "\n".join(message)
    if "!" in header or "BREAKING CHANGE" in body:
        major, minor, patch = major + 1, 0, 0
    elif header.startswith("feat"):
        minor, patch = minor + 1, 0
    else:
        patch += 1
    cmakelists = root / "CMakeLists.txt"
    text = cmakelists.read_text(encoding="utf-8")
    found = re.search(
        r"project\(sshmcp VERSION \d+\.\d+\.\d+", text)
    replacement = f"project(sshmcp VERSION {major}.{minor}.{patch}"
    cmakelists.write_text(
        text[:found.start()] + replacement + text[found.end():],
        encoding="utf-8")
    subprocess.run(["git", "add", "CMakeLists.txt"],
                   cwd=root, check=True)
    subprocess.run(["git", "commit", "--amend", "--no-edit",
                    "--no-verify"],
                   cwd=root, check=True,
                   capture_output=True)
    print(f"version_bump: {major}.{minor}.{patch}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
