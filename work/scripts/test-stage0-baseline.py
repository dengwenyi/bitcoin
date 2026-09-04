#!/usr/bin/env python3
"""Exercise the minimal bitcoind baseline on all supported chain modes."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


CHAINS = {
    "main": ([], Path(".cookie")),
    "testnet4": (["-testnet4"], Path("testnet4/.cookie")),
    "signet": (["-signet"], Path("signet/.cookie")),
    "regtest": (["-regtest"], Path("regtest/.cookie")),
}


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def rpc_call(port: int, cookie_path: Path, method: str, params: list[Any] | None = None) -> Any:
    credentials = cookie_path.read_text(encoding="utf-8").strip()
    authorization = base64.b64encode(credentials.encode("utf-8")).decode("ascii")
    body = json.dumps({"jsonrpc": "2.0", "id": "stage0", "method": method, "params": params or []}).encode("utf-8")
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}/",
        data=body,
        headers={"Authorization": f"Basic {authorization}", "Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=2) as response:
        result = json.loads(response.read().decode("utf-8"))
    if result.get("error") is not None:
        raise RuntimeError(f"RPC {method} failed: {result['error']}")
    return result["result"]


def wait_for_rpc(process: subprocess.Popen[bytes], port: int, cookie_path: Path, timeout: float = 45.0) -> dict[str, Any]:
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"bitcoind exited before RPC became ready (exit {process.returncode})")
        if cookie_path.exists():
            try:
                return rpc_call(port, cookie_path, "getblockchaininfo")
            except (OSError, ValueError, RuntimeError, urllib.error.URLError) as error:
                last_error = error
        time.sleep(0.1)
    raise TimeoutError(f"RPC did not become ready: {last_error}")


def start_node(binary: Path, chain: str, datadir: Path, port: int) -> tuple[subprocess.Popen[bytes], Any]:
    datadir.mkdir(parents=True, exist_ok=True)
    output = (datadir / "console.log").open("ab")
    chain_args, _ = CHAINS[chain]
    command = [
        str(binary),
        *chain_args,
        f"-datadir={datadir}",
        "-server=1",
        "-listen=0",
        "-dnsseed=0",
        "-discover=0",
        "-connect=0",
        "-printtoconsole=0",
        f"-rpcport={port}",
    ]
    process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
    return process, output


def stop_node(process: subprocess.Popen[bytes], output: Any, port: int, cookie_path: Path) -> None:
    try:
        rpc_call(port, cookie_path, "stop")
        process.wait(timeout=30)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=10)
        output.close()
    if process.returncode != 0:
        raise RuntimeError(f"bitcoind did not stop cleanly (exit {process.returncode})")


def smoke_chain(binary: Path, chain: str, datadir: Path) -> dict[str, Any]:
    port = free_port()
    _, cookie_relative = CHAINS[chain]
    cookie_path = datadir / cookie_relative
    process, output = start_node(binary, chain, datadir, port)
    try:
        blockchain = wait_for_rpc(process, port, cookie_path)
        if blockchain["chain"] != chain:
            raise AssertionError(f"requested {chain}, daemon reported {blockchain['chain']}")
        result = {
            "chain": blockchain["chain"],
            "blocks": blockchain["blocks"],
            "headers": blockchain["headers"],
            "bestblockhash": blockchain["bestblockhash"],
            "genesis": rpc_call(port, cookie_path, "getblockhash", [0]),
            "initialblockdownload": blockchain["initialblockdownload"],
            "networkversion": rpc_call(port, cookie_path, "getnetworkinfo")["version"],
            "mempool_loaded": rpc_call(port, cookie_path, "getmempoolinfo")["loaded"],
        }
    finally:
        stop_node(process, output, port, cookie_path)
    return result


def regtest_persistence(binary: Path, datadir: Path) -> dict[str, Any]:
    _, cookie_relative = CHAINS["regtest"]
    cookie_path = datadir / cookie_relative
    mining_address = "mipcBbFg9gMiCh81Kj8tqqdgoZub1ZJRfn"

    port = free_port()
    process, output = start_node(binary, "regtest", datadir, port)
    try:
        wait_for_rpc(process, port, cookie_path)
        generated = rpc_call(port, cookie_path, "generatetoaddress", [3, mining_address])
        before = rpc_call(port, cookie_path, "getblockchaininfo")
    finally:
        stop_node(process, output, port, cookie_path)

    port = free_port()
    process, output = start_node(binary, "regtest", datadir, port)
    try:
        after_clean_restart = wait_for_rpc(process, port, cookie_path)
        if after_clean_restart["bestblockhash"] != before["bestblockhash"]:
            raise AssertionError("best block changed after clean restart")
        process.kill()
        process.wait(timeout=10)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=10)
        output.close()

    port = free_port()
    process, output = start_node(binary, "regtest", datadir, port)
    try:
        after_forced_restart = wait_for_rpc(process, port, cookie_path)
        if after_forced_restart["bestblockhash"] != before["bestblockhash"]:
            raise AssertionError("best block changed after forced termination and restart")
    finally:
        stop_node(process, output, port, cookie_path)

    return {
        "generated_blocks": len(generated),
        "height": before["blocks"],
        "bestblockhash": before["bestblockhash"],
        "clean_restart_match": True,
        "forced_restart_match": True,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=Path("build-node-relay-vs2022/bin/Debug/bitcoind.exe"))
    parser.add_argument("--work-dir", type=Path, default=Path(".tmp/stage0-baseline"))
    args = parser.parse_args()

    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error(f"bitcoind binary not found: {binary}")
    run_dir = args.work_dir.resolve() / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + f"-{os.getpid()}")
    run_dir.mkdir(parents=True, exist_ok=False)

    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    report: dict[str, Any] = {
        "binary": str(binary),
        "binary_size": binary.stat().st_size,
        "binary_sha256": digest,
        "chains": {},
    }
    for chain in CHAINS:
        report["chains"][chain] = smoke_chain(binary, chain, run_dir / chain)
    report["regtest_persistence"] = regtest_persistence(binary, run_dir / "regtest-persistence")
    print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
