#!/usr/bin/env python3
import argparse
import os
import socket
from datetime import datetime


def make_session_path(base_dir: str, host: str) -> str:
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    safe_host = host.replace(":", "_")
    return os.path.join(base_dir, f"remote-log-{stamp}-{safe_host}.log")


def main() -> int:
    parser = argparse.ArgumentParser(description="Receive NX-Cast remote log uploads")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=28772)
    parser.add_argument("--output-dir", default=os.path.join(os.getcwd(), "logs"))
    parser.add_argument("--stdout", action="store_true", help="also mirror received logs to stdout")
    parser.add_argument("--once", action="store_true", help="exit after the first completed upload")
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((args.host, args.port))
    server.listen(4)

    print(f"[log-receiver] listening on {args.host}:{args.port}", flush=True)

    try:
        while True:
            conn, addr = server.accept()
            session_path = make_session_path(args.output_dir, addr[0])
            print(f"[log-receiver] connection from {addr[0]}:{addr[1]} -> {session_path}", flush=True)
            with conn, open(session_path, "w", encoding="utf-8") as f:
                upload_complete = False
                try:
                    while True:
                        data = conn.recv(4096)
                        if not data:
                            upload_complete = True
                            break
                        text = data.decode("utf-8", errors="replace")
                        f.write(text)
                        f.flush()
                        if args.stdout:
                            print(text, end="", flush=True)
                except ConnectionResetError:
                    print(f"[log-receiver] connection reset by peer {addr[0]}:{addr[1]}", flush=True)
                if upload_complete:
                    try:
                        conn.sendall(b"OK\n")
                    except OSError:
                        print(f"[log-receiver] ack send failed {addr[0]}:{addr[1]}", flush=True)
                else:
                    print(f"[log-receiver] upload incomplete {addr[0]}:{addr[1]}", flush=True)
            print(f"[log-receiver] saved {session_path}", flush=True)
            if args.once:
                break
    except KeyboardInterrupt:
        print("[log-receiver] stopped", flush=True)
    finally:
        server.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
