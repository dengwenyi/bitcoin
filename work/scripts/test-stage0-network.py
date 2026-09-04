#!/usr/bin/env python3
"""Verify local V1/V2 sync, reorg, and transaction relay behavior."""

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
from typing import Any, Callable


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def base58check(payload: bytes) -> str:
    alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
    checksum = hashlib.sha256(hashlib.sha256(payload).digest()).digest()[:4]
    value = int.from_bytes(payload + checksum, "big")
    result = ""
    while value:
        value, remainder = divmod(value, 58)
        result = alphabet[remainder] + result
    leading_zeroes = len(payload + checksum) - len((payload + checksum).lstrip(b"\0"))
    return "1" * leading_zeroes + result


def regtest_wif(secret: int) -> str:
    return base58check(b"\xef" + secret.to_bytes(32, "big") + b"\x01")


class Node:
    def __init__(self, name: str, binary: Path, root: Path, *, v2transport: bool = True) -> None:
        self.name = name
        self.binary = binary
        self.datadir = root / name
        self.datadir.mkdir(parents=True, exist_ok=False)
        self.rpc_port = free_port()
        self.p2p_port = free_port()
        self.cookie = self.datadir / "regtest/.cookie"
        self.output = (self.datadir / "console.log").open("ab")
        command = [
            str(binary),
            "-regtest",
            f"-datadir={self.datadir}",
            "-server=1",
            "-listen=1",
            "-dnsseed=0",
            "-discover=0",
            "-connect=0",
            "-printtoconsole=0",
            f"-rpcport={self.rpc_port}",
            f"-port={self.p2p_port}",
            "-bind=127.0.0.1",
            f"-v2transport={1 if v2transport else 0}",
        ]
        self.process = subprocess.Popen(command, stdout=self.output, stderr=subprocess.STDOUT)
        self.wait_rpc()

    @property
    def address(self) -> str:
        return f"127.0.0.1:{self.p2p_port}"

    def rpc(self, method: str, params: list[Any] | None = None) -> Any:
        credentials = self.cookie.read_text(encoding="utf-8").strip()
        authorization = base64.b64encode(credentials.encode("utf-8")).decode("ascii")
        body = json.dumps({"jsonrpc": "2.0", "id": self.name, "method": method, "params": params or []}).encode("utf-8")
        request = urllib.request.Request(
            f"http://127.0.0.1:{self.rpc_port}/",
            data=body,
            headers={"Authorization": f"Basic {authorization}", "Content-Type": "application/json"},
        )
        with urllib.request.urlopen(request, timeout=3) as response:
            result = json.loads(response.read().decode("utf-8"))
        if result.get("error") is not None:
            raise RuntimeError(f"{self.name} RPC {method} failed: {result['error']}")
        return result["result"]

    def wait_rpc(self, timeout: float = 45.0) -> None:
        deadline = time.monotonic() + timeout
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"{self.name} exited during startup (exit {self.process.returncode})")
            if self.cookie.exists():
                try:
                    self.rpc("getblockchaininfo")
                    return
                except (OSError, ValueError, RuntimeError, urllib.error.URLError) as error:
                    last_error = error
            time.sleep(0.1)
        raise TimeoutError(f"{self.name} RPC did not become ready: {last_error}")

    def stop(self) -> None:
        if self.process.poll() is None:
            try:
                self.rpc("stop")
                self.process.wait(timeout=30)
            finally:
                if self.process.poll() is None:
                    self.process.kill()
                    self.process.wait(timeout=10)
        self.output.close()
        if self.process.returncode != 0:
            raise RuntimeError(f"{self.name} did not stop cleanly (exit {self.process.returncode})")


def wait_until(description: str, predicate: Callable[[], bool], timeout: float = 45.0) -> None:
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            if predicate():
                return
        except (OSError, RuntimeError, urllib.error.URLError) as error:
            last_error = error
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {description}: {last_error}")


def descriptor_address(node: Node, secret: int) -> tuple[str, str]:
    wif = regtest_wif(secret)
    descriptor = node.rpc("getdescriptorinfo", [f"wpkh({wif})"])["descriptor"]
    address = node.rpc("deriveaddresses", [descriptor])[0]
    return wif, address


def transport_to(node: Node, address: str) -> str:
    for peer in node.rpc("getpeerinfo"):
        if peer["addr"] == address:
            return str(peer.get("transport_protocol_type", "unknown"))
    raise RuntimeError(f"{node.name} has no peer {address}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=Path("build-node-relay-vs2022/bin/Debug/bitcoind.exe"))
    parser.add_argument("--work-dir", type=Path, default=Path(".tmp/stage0-network"))
    args = parser.parse_args()

    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error(f"bitcoind binary not found: {binary}")
    run_root = args.work_dir.resolve() / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + f"-{os.getpid()}")
    run_root.mkdir(parents=True, exist_ok=False)

    nodes: list[Node] = []
    try:
        node_a = Node("node-a", binary, run_root)
        nodes.append(node_a)
        node_b = Node("node-b-v1", binary, run_root, v2transport=False)
        nodes.append(node_b)
        node_c = Node("node-c-v2", binary, run_root)
        nodes.append(node_c)

        mining_wif, mining_address = descriptor_address(node_a, 1)
        _, destination_address = descriptor_address(node_a, 2)
        node_a.rpc("generatetoaddress", [101, mining_address])
        source_tip = node_a.rpc("getbestblockhash")

        node_a.rpc("addnode", [node_b.address, "onetry"])
        node_a.rpc("addnode", [node_c.address, "onetry"])
        wait_until("V1 peer connection", lambda: transport_to(node_a, node_b.address) == "v1")
        wait_until("V2 peer connection", lambda: transport_to(node_a, node_c.address) == "v2")
        wait_until("node B initial block sync", lambda: node_b.rpc("getbestblockhash") == source_tip)
        wait_until("node C initial block sync", lambda: node_c.rpc("getbestblockhash") == source_tip)

        node_a.rpc("disconnectnode", [node_b.address])
        wait_until("node B disconnect", lambda: all(peer["addr"] != node_b.address for peer in node_a.rpc("getpeerinfo")))
        node_a.rpc("generatetoaddress", [1, mining_address])
        node_b.rpc("generatetoaddress", [2, mining_address])
        winning_tip = node_b.rpc("getbestblockhash")
        node_a.rpc("addnode", [node_b.address, "onetry"])
        wait_until("node A reorg", lambda: node_a.rpc("getbestblockhash") == winning_tip)
        wait_until("node C reorg", lambda: node_c.rpc("getbestblockhash") == winning_tip)

        block_one = node_a.rpc("getblock", [node_a.rpc("getblockhash", [1]), 2])
        coinbase = block_one["tx"][0]
        prevout = coinbase["vout"][0]
        unsigned = node_a.rpc(
            "createrawtransaction",
            [[{"txid": coinbase["txid"], "vout": prevout["n"]}], [{destination_address: 49.999}]],
        )
        signed = node_a.rpc(
            "signrawtransactionwithkey",
            [unsigned, [mining_wif], [{
                "txid": coinbase["txid"],
                "vout": prevout["n"],
                "scriptPubKey": prevout["scriptPubKey"]["hex"],
                "amount": prevout["value"],
            }]],
        )
        if not signed["complete"]:
            raise AssertionError(f"raw transaction was not completely signed: {signed.get('errors')}")
        txid = node_a.rpc("sendrawtransaction", [signed["hex"]])
        wait_until("transaction relay to V1 peer", lambda: txid in node_b.rpc("getrawmempool"))
        wait_until("transaction relay to V2 peer", lambda: txid in node_c.rpc("getrawmempool"))

        report = {
            "initial_sync": {"height": 101, "tip": source_tip, "node_b_match": True, "node_c_match": True},
            "transport": {"node_b": transport_to(node_a, node_b.address), "node_c": transport_to(node_a, node_c.address)},
            "reorg": {"height": node_a.rpc("getblockcount"), "winning_tip": winning_tip, "all_nodes_match": True},
            "transaction_relay": {"txid": txid, "v1_peer_mempool": True, "v2_peer_mempool": True},
        }
        print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    finally:
        errors: list[str] = []
        for node in reversed(nodes):
            try:
                node.stop()
            except Exception as error:  # keep stopping the remaining nodes
                errors.append(str(error))
        if errors:
            raise RuntimeError("; ".join(errors))
    return 0


if __name__ == "__main__":
    sys.exit(main())
