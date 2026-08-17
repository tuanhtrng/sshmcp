"""Fake ssh: one-shot replay, or stateful session-shell mode."""
import json
import os
import re
import sys
import time


def emit(reply: dict, counter: str) -> None:
    time.sleep(reply.get("sleep_ms", 0) / 1000.0)
    out = reply.get("stdout", "") + \
        f"\n@sshmcp:{counter}:{reply.get('exit', 0)}@\n"
    err = reply.get("stderr", "") + f"\n@sshmcp:{counter}@\n"
    sys.stdout.buffer.write(out.encode("utf-8"))
    sys.stdout.buffer.flush()
    sys.stderr.buffer.write(err.encode("utf-8"))
    sys.stderr.buffer.flush()


def load_replies() -> list:
    with open(os.environ["FAKE_SSH_REPLIES"],
              encoding="utf-8") as f:
        return [json.loads(x) for x in f if x.strip()]


def session_mode() -> int:
    log_path = os.environ["FAKE_SSH_LOG"]
    replies = load_replies()
    with open(log_path, "a", encoding="utf-8") as log:
        log.write(json.dumps(
            {"argv": sys.argv[1:], "stdin": ""}) + "\n")
    index = 0
    command: list = []
    collecting = False
    for raw in sys.stdin.buffer:
        line = raw.decode("utf-8").rstrip("\n")
        if line == "{":
            collecting = True
            command = []
            continue
        if line == "} </dev/null":
            collecting = False
            continue
        found = re.match(
            r"printf '\\n@sshmcp:(\d+):%s@\\n' \"\$\?\"", line)
        if found:
            with open(log_path, "a", encoding="utf-8") as log:
                log.write(json.dumps(
                    {"session_command": command}) + "\n")
            if index < len(replies):
                reply = replies[index]
            else:
                reply = {"stdout": "",
                         "stderr": "fake_ssh: no reply",
                         "exit": 1}
            index += 1
            emit(reply, found.group(1))
            continue
        if collecting:
            command.append(line)
    return 0


def oneshot_mode() -> int:
    log_path = os.environ["FAKE_SSH_LOG"]
    replies_path = os.environ["FAKE_SSH_REPLIES"]
    stdin_data = sys.stdin.buffer.read().decode("utf-8")
    with open(log_path, "a", encoding="utf-8") as log:
        log.write(json.dumps(
            {"argv": sys.argv[1:], "stdin": stdin_data}) + "\n")
    replies = load_replies()
    index_path = replies_path + ".index"
    index = 0
    if os.path.exists(index_path):
        with open(index_path, encoding="utf-8") as f:
            index = int(f.read().strip() or 0)
    with open(index_path, "w", encoding="utf-8") as f:
        f.write(str(index + 1))
    if index < len(replies):
        reply = replies[index]
    else:
        reply = {"stdout": "", "stderr": "fake_ssh: no reply",
                 "exit": 1}
    time.sleep(reply.get("sleep_ms", 0) / 1000.0)
    sys.stdout.buffer.write(reply.get("stdout", "").encode("utf-8"))
    sys.stderr.buffer.write(reply.get("stderr", "").encode("utf-8"))
    sys.stdout.buffer.flush()
    sys.stderr.buffer.flush()
    return int(reply.get("exit", 0))


def forward_mode() -> int:
    log_path = os.environ["FAKE_SSH_LOG"]
    with open(log_path, "a", encoding="utf-8") as log:
        log.write(json.dumps(
            {"argv": sys.argv[1:], "stdin": ""}) + "\n")
    fail = os.environ.get("FAKE_SSH_FORWARD_FAIL")
    if fail:
        sys.stderr.buffer.write(fail.encode("utf-8"))
        sys.stderr.buffer.flush()
        return 1
    time.sleep(3600)  # killed by the server
    return 0


def main() -> int:
    if "-N" in sys.argv[1:]:
        return forward_mode()
    if sys.argv[1:] and sys.argv[-1] == "sh":
        return session_mode()
    return oneshot_mode()


if __name__ == "__main__":
    sys.exit(main())
