"""Feeds a JSON-RPC transcript to sshmcp, diffs vs golden."""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def load_jsonl(path: Path) -> list:
    if not path.exists():
        return []
    return [json.loads(x)
            for x in path.read_text(encoding="utf-8").splitlines()
            if x.strip()]


def normalize(responses: list) -> list:
    """Version bumps every commit; goldens must not pin it."""
    for response in responses:
        info = response.get("result", {})
        if isinstance(info, dict) and "serverInfo" in info:
            info["serverInfo"]["version"] = "VERSION"
    return responses


def main() -> int:
    binary = Path(sys.argv[1])
    case_dir = Path(sys.argv[2])
    bless = "--bless" in sys.argv[3:]
    fake_ssh = Path(__file__).parent / "fake_ssh.py"
    with tempfile.TemporaryDirectory() as tmp:
        log_path = Path(tmp) / "calls.jsonl"
        replies_path = Path(tmp) / "replies.jsonl"
        replies_path.write_text(
            (case_dir / "replies.jsonl")
            .read_text(encoding="utf-8"),
            encoding="utf-8")
        env = dict(os.environ)
        env.update({
            "SSHMCP_TARGET": "dev@example",
            "SSHMCP_SSH_EXE": f'"{sys.executable}" "{fake_ssh}"',
            "SSHMCP_LOG": "off",
            "FAKE_SSH_LOG": str(log_path),
            "FAKE_SSH_REPLIES": str(replies_path),
        })
        extra_env = case_dir / "env.json"
        if extra_env.exists():
            env.update(json.loads(
                extra_env.read_text(encoding="utf-8")))
        transcript = (case_dir / "transcript.jsonl") \
            .read_text(encoding="utf-8")
        proc = subprocess.run(
            [str(binary)], input=transcript.encode("utf-8"),
            capture_output=True, env=env, timeout=120)
        got_lines = [x for x in
                     proc.stdout.decode("utf-8").splitlines()
                     if x.strip()]
        got = normalize([json.loads(x) for x in got_lines])
        calls = load_jsonl(log_path)
        if bless:
            (case_dir / "golden.jsonl").write_text(
                "".join(json.dumps(r) + "\n" for r in got),
                encoding="utf-8")
            (case_dir / "expected_calls.jsonl").write_text(
                "".join(json.dumps(c) + "\n" for c in calls),
                encoding="utf-8")
            print(f"blessed {case_dir}")
            return 0
        ok = True
        want = load_jsonl(case_dir / "golden.jsonl")
        if got != want:
            ok = False
            print("response mismatch")
            print("got: ", json.dumps(got, indent=2))
            print("want:", json.dumps(want, indent=2))
        want_calls = load_jsonl(case_dir / "expected_calls.jsonl")
        if calls != want_calls:
            ok = False
            print("ssh call mismatch")
            print("got: ", json.dumps(calls, indent=2))
            print("want:", json.dumps(want_calls, indent=2))
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
