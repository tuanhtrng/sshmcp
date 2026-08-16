"""Records argv+stdin, replays canned replies in order."""
import json
import os
import sys
import time


def main() -> int:
    log_path = os.environ["FAKE_SSH_LOG"]
    replies_path = os.environ["FAKE_SSH_REPLIES"]
    stdin_data = sys.stdin.buffer.read().decode("utf-8")
    with open(log_path, "a", encoding="utf-8") as log:
        log.write(json.dumps(
            {"argv": sys.argv[1:], "stdin": stdin_data}) + "\n")
    with open(replies_path, encoding="utf-8") as f:
        replies = [json.loads(x) for x in f if x.strip()]
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
    sys.stdout.buffer.write(
        reply.get("stdout", "").encode("utf-8"))
    sys.stderr.buffer.write(
        reply.get("stderr", "").encode("utf-8"))
    sys.stdout.buffer.flush()
    sys.stderr.buffer.flush()
    return int(reply.get("exit", 0))


if __name__ == "__main__":
    sys.exit(main())
