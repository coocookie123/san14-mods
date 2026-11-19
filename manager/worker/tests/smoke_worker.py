"""Run the compiled Host, frozen Worker, and a simulated adapter together."""
import argparse
import json
import os
from pathlib import Path
import queue
import subprocess
import threading
from urllib.request import ProxyHandler, Request, build_opener


def smoke(probe, assets):
    with subprocess.Popen([str(probe), str(assets)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True, encoding="utf-8",
                          creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)) as process:
        ports = queue.Queue()
        threading.Thread(target=lambda: ports.put(process.stdout.readline()), daemon=True).start()
        try:
            startup = ports.get(timeout=45).strip()
            if not startup.isdecimal():
                raise RuntimeError(process.stderr.read())
            endpoint = f"http://127.0.0.1:{int(startup)}/mcp"
            opener = build_opener(ProxyHandler({}))
            headers = {"Accept": "application/json, text/event-stream", "Content-Type": "application/json"}

            def exchange(method, params, request_id=None):
                message = {"jsonrpc": "2.0", "method": method, "params": params}
                if request_id is not None:
                    message["id"] = request_id
                request = Request(endpoint, data=json.dumps(message).encode(), headers=headers, method="POST")
                with opener.open(request, timeout=5) as reply:
                    session = reply.headers.get("Mcp-Session-Id")
                    if session:
                        headers["Mcp-Session-Id"] = session
                    if request_id is None:
                        return None
                    if reply.headers.get_content_type() == "application/json":
                        return json.load(reply)
                    for part in reply:
                        if part.startswith(b"data:"):
                            result = json.loads(part[5:].strip())
                            if result.get("id") == request_id:
                                return result
                    raise RuntimeError(f"Missing response for {method}")

            initialized = exchange("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                                                  "clientInfo": {"name": "fixture", "version": "1"}}, 1)["result"]
            headers["Mcp-Protocol-Version"] = initialized["protocolVersion"]
            exchange("notifications/initialized", {})
            listed = exchange("tools/list", {}, 2)["result"]["tools"]
            assert [tool["name"] for tool in listed] == ["fixture_city"]
            assert listed[0]["inputSchema"]["properties"]["city_id"]["type"] == "integer"
            result = exchange("tools/call", {"name": "fixture_city", "arguments": {"city_id": 7}}, 3)["result"]
            assert result["structuredContent"] == {"city_id": 7, "name": "Fixture city"}
            rejected = exchange("tools/call", {"name": "fixture_city", "arguments": {"city_id": "bad"}}, 4)
            assert "error" in rejected or rejected["result"]["isError"] is True
            process.stdin.write("verified\n")
            process.stdin.flush()
            if process.wait(timeout=20):
                raise RuntimeError(process.stderr.read())
        finally:
            if process.poll() is None:
                process.kill()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path)
    parser.add_argument("assets", type=Path)
    args = parser.parse_args()
    smoke(args.probe.resolve(), args.assets.resolve())
    print("Host launch, MCP catalogue, semantic call, validation and graceful stop passed")
