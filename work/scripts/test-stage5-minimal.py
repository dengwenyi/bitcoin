#!/usr/bin/env python3
"""Exercise the RPC-free bitcoind_min composition against a full control node."""

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


CHAINS = {
    "main": ([], "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
    "testnet4": (["-testnet4"], "00000000da84f2bafbbc53dee25a72ae507ff4914b867c565be350b0da8bf043"),
    "signet": (["-signet"], "00000008819873e925422c1ff0f99f7cc9bbb232af63a077a480a3633bee1ef6"),
    "regtest": (["-regtest"], "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"),
}


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_until(description: str, predicate: Callable[[], bool], timeout: float = 60.0) -> None:
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            if predicate():
                return
        except (OSError, RuntimeError, ValueError, urllib.error.URLError) as error:
            last_error = error
        time.sleep(0.05)
    raise TimeoutError(f"timed out waiting for {description}: {last_error}")


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


class ControlNode:
    def __init__(self, name: str, binary: Path, root: Path) -> None:
        self.name = name
        self.datadir = root / name
        self.datadir.mkdir(parents=True, exist_ok=False)
        self.rpc_port = free_port()
        self.p2p_port = free_port()
        self.cookie = self.datadir / "regtest/.cookie"
        self.output = (self.datadir / "console.log").open("ab")
        self.process = subprocess.Popen([
            str(binary), "-regtest", f"-datadir={self.datadir}", "-server=1",
            "-listen=1", "-dnsseed=0", "-discover=0", "-connect=0",
            "-printtoconsole=0", f"-rpcport={self.rpc_port}",
            f"-port={self.p2p_port}", "-bind=127.0.0.1", "-v2transport=1",
        ], stdout=self.output, stderr=subprocess.STDOUT)
        wait_until(f"{name} RPC", lambda: self.rpc("getblockchaininfo") is not None)

    @property
    def address(self) -> str:
        return f"127.0.0.1:{self.p2p_port}"

    def rpc(self, method: str, params: list[Any] | None = None) -> Any:
        credentials = self.cookie.read_text(encoding="utf-8").strip()
        authorization = base64.b64encode(credentials.encode()).decode()
        body = json.dumps({"jsonrpc": "2.0", "id": self.name, "method": method, "params": params or []}).encode()
        request = urllib.request.Request(
            f"http://127.0.0.1:{self.rpc_port}/", data=body,
            headers={"Authorization": f"Basic {authorization}", "Content-Type": "application/json"},
        )
        with urllib.request.urlopen(request, timeout=3) as response:
            result = json.loads(response.read().decode())
        if result.get("error") is not None:
            raise RuntimeError(f"{self.name} RPC {method} failed: {result['error']}")
        return result["result"]

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


class MinimalNode:
    def __init__(self, name: str, binary: Path, root: Path, connects: list[str], stop_height: int, *, v2transport: bool) -> None:
        self.name = name
        self.datadir = root / name
        self.datadir.mkdir(parents=True, exist_ok=False)
        self.p2p_port = free_port()
        self.output = (self.datadir / "console.log").open("ab")
        command = [
            str(binary), "-regtest", f"-datadir={self.datadir}", "-server=0",
            "-listen=1", "-dnsseed=0", "-discover=0", "-printtoconsole=0",
            f"-port={self.p2p_port}", "-bind=127.0.0.1", f"-v2transport={int(v2transport)}",
            "-listenonion=0", "-natpmp=0", f"-stopatheight={stop_height}",
        ]
        for address in connects:
            command.append(f"-connect={address}")
        self.process = subprocess.Popen(command, stdout=self.output, stderr=subprocess.STDOUT)

    @property
    def address(self) -> str:
        return f"127.0.0.1:{self.p2p_port}"

    @property
    def debug_log(self) -> Path:
        return self.datadir / "regtest/debug.log"

    def log_contains(self, text: str) -> bool:
        return self.debug_log.exists() and text in self.debug_log.read_text(encoding="utf-8", errors="replace")

    def wait_height(self, height: int) -> None:
        wait_until(f"{self.name} height {height}", lambda: self.log_contains(f"height={height} "))

    def wait_clean_exit(self) -> None:
        self.process.wait(timeout=60)
        self.output.close()
        if self.process.returncode != 0 or not self.log_contains("Shutdown done"):
            raise RuntimeError(f"{self.name} did not shut down cleanly (exit {self.process.returncode})")

    def kill(self) -> None:
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=10)
        if not self.output.closed:
            self.output.close()


def descriptor_address(node: ControlNode, secret: int) -> tuple[str, str]:
    wif = regtest_wif(secret)
    descriptor = node.rpc("getdescriptorinfo", [f"wpkh({wif})"])["descriptor"]
    return wif, node.rpc("deriveaddresses", [descriptor])[0]


def run_chain_smoke(binary: Path, root: Path) -> dict[str, bool]:
    results: dict[str, bool] = {}
    for chain, (chain_args, genesis) in CHAINS.items():
        datadir = root / f"smoke-{chain}"
        datadir.mkdir(parents=True, exist_ok=False)
        output_path = datadir / "console.log"
        with output_path.open("wb") as output:
            process = subprocess.run([
                str(binary), *chain_args, f"-datadir={datadir}", "-server=0",
                "-listen=0", "-dnsseed=0", "-discover=0", "-connect=0",
                "-stopafterblockimport=1", "-printtoconsole=1",
            ], stdout=output, stderr=subprocess.STDOUT, timeout=60, check=False)
        log = output_path.read_text(encoding="utf-8", errors="replace")
        if process.returncode != 0 or genesis not in log or "Shutdown done" not in log or list(datadir.rglob(".cookie")):
            raise RuntimeError(f"minimal {chain} smoke failed (exit {process.returncode})")
        results[chain] = True
    return results


def run_optional_service_rejections(binary: Path, root: Path) -> dict[str, bool]:
    results: dict[str, bool] = {}
    for name, option in {
        "rpc": "-server=1",
        "txindex": "-txindex=1",
        "blockfilterindex": "-blockfilterindex=1",
        "coinstatsindex": "-coinstatsindex=1",
    }.items():
        datadir = root / f"reject-{name}"
        datadir.mkdir(parents=True, exist_ok=False)
        process = subprocess.run(
            [str(binary), "-regtest", f"-datadir={datadir}", option],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30, check=False,
        )
        output = process.stdout.decode("utf-8", errors="replace")
        if process.returncode == 0 or "unavailable in bitcoind_min" not in output:
            raise AssertionError(f"minimal daemon did not reject {option}")
        results[name] = True
    return results


def transport_types(node: ControlNode) -> list[str]:
    return [str(peer.get("transport_protocol_type", "unknown")) for peer in node.rpc("getpeerinfo")]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--minimal-binary", type=Path, default=Path("build-node-relay-vs2022/bin/Debug/bitcoind_min.exe"))
    parser.add_argument("--control-binary", type=Path, default=Path("build-node-relay-vs2022/bin/Debug/bitcoind.exe"))
    parser.add_argument("--work-dir", type=Path, default=Path(".tmp/stage5-minimal"))
    args = parser.parse_args()
    minimal_binary = args.minimal_binary.resolve()
    control_binary = args.control_binary.resolve()
    if not minimal_binary.is_file() or not control_binary.is_file():
        parser.error("minimal or control daemon binary not found")

    run_root = args.work_dir.resolve() / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + f"-{os.getpid()}")
    run_root.mkdir(parents=True, exist_ok=False)
    report: dict[str, Any] = {
        "chain_smoke": run_chain_smoke(minimal_binary, run_root),
        "rejected_optional_services": run_optional_service_rejections(minimal_binary, run_root),
    }
    controls: list[ControlNode] = []
    minimals: list[MinimalNode] = []
    try:
        source = ControlNode("source", control_binary, run_root)
        controls.append(source)
        sink = ControlNode("sink", control_binary, run_root)
        controls.append(sink)
        mining_wif, mining_address = descriptor_address(source, 1)
        _, destination_address = descriptor_address(source, 2)
        source.rpc("generatetoaddress", [101, mining_address])

        relay = MinimalNode("minimal-v1-relay", minimal_binary, run_root, [source.address], 102, v2transport=False)
        minimals.append(relay)
        wait_until("V1 source connection", lambda: transport_types(source) == ["v1"])
        relay.wait_height(101)
        sink.rpc("addnode", [relay.address, "onetry"])
        wait_until("V1 sink connection", lambda: any(peer["addr"] == relay.address and peer.get("transport_protocol_type") == "v1" for peer in sink.rpc("getpeerinfo")))

        block_one = source.rpc("getblock", [source.rpc("getblockhash", [1]), 2])
        coinbase = block_one["tx"][0]
        prevout = coinbase["vout"][0]
        unsigned = source.rpc("createrawtransaction", [[{"txid": coinbase["txid"], "vout": prevout["n"]}], [{destination_address: 49.999}]])
        signed = source.rpc("signrawtransactionwithkey", [unsigned, [mining_wif], [{
            "txid": coinbase["txid"], "vout": prevout["n"],
            "scriptPubKey": prevout["scriptPubKey"]["hex"], "amount": prevout["value"],
        }]])
        if not signed["complete"]:
            raise AssertionError("transaction signing failed")
        txid = source.rpc("sendrawtransaction", [signed["hex"]])
        wait_until("transaction relayed through minimal node", lambda: txid in sink.rpc("getrawmempool"))
        source.rpc("generatetoaddress", [1, mining_address])
        relay.wait_clean_exit()
        wait_until("sink height 102", lambda: sink.rpc("getblockcount") == 102)

        source.rpc("generatetoaddress", [1, mining_address])
        sink.rpc("generatetoaddress", [2, mining_address])
        winning_tip = sink.rpc("getbestblockhash")
        reorg = MinimalNode("minimal-v2-reorg", minimal_binary, run_root, [source.address], 104, v2transport=True)
        minimals.append(reorg)
        reorg.wait_height(103)
        sink.rpc("addnode", [reorg.address, "onetry"])
        reorg.wait_clean_exit()
        sink_log = (sink.datadir / "regtest/debug.log").read_text(encoding="utf-8", errors="replace")
        if "New manual peer connected: transport: v2" not in sink_log:
            raise AssertionError("control node did not record a V2 connection to the minimal node")
        if not reorg.log_contains(winning_tip) or not reorg.log_contains("height=104 "):
            raise AssertionError("minimal node did not converge to the winning reorg tip")

        # Verify a clean reload, then force-terminate an idle process and verify recovery.
        reload_dir = reorg.datadir
        reload_output = (reload_dir / "reload.log").open("wb")
        reload_process = subprocess.Popen([
            str(minimal_binary), "-regtest", f"-datadir={reload_dir}", "-server=0",
            "-listen=0", "-connect=0", "-stopafterblockimport=1", "-printtoconsole=1",
        ], stdout=reload_output, stderr=subprocess.STDOUT)
        reload_process.wait(timeout=60)
        reload_output.close()
        if reload_process.returncode != 0 or "height=104 " not in (reload_dir / "reload.log").read_text(encoding="utf-8", errors="replace"):
            raise AssertionError("clean minimal reload did not retain height 104")

        forced_output = (reload_dir / "forced.log").open("wb")
        forced_process = subprocess.Popen([
            str(minimal_binary), "-regtest", f"-datadir={reload_dir}", "-server=0",
            "-listen=0", "-connect=0", "-printtoconsole=1",
        ], stdout=forced_output, stderr=subprocess.STDOUT)
        wait_until("minimal forced-run initialization", lambda: "Done loading" in (reload_dir / "forced.log").read_text(encoding="utf-8", errors="replace"))
        forced_process.kill()
        forced_process.wait(timeout=10)
        forced_output.close()

        recovery_output = (reload_dir / "recovery.log").open("wb")
        recovery_process = subprocess.Popen([
            str(minimal_binary), "-regtest", f"-datadir={reload_dir}", "-server=0",
            "-listen=0", "-connect=0", "-stopafterblockimport=1", "-printtoconsole=1",
        ], stdout=recovery_output, stderr=subprocess.STDOUT)
        recovery_process.wait(timeout=60)
        recovery_output.close()
        recovery_log = (reload_dir / "recovery.log").read_text(encoding="utf-8", errors="replace")
        if recovery_process.returncode != 0 or "height=104 " not in recovery_log or "Shutdown done" not in recovery_log:
            raise AssertionError("minimal recovery after forced termination failed")

        report.update({
            "v1": {"sync_height": 101, "relay_txid": txid, "mined_height": 102, "clean_exit": True},
            "v2_reorg": {"losing_height": 103, "winning_height": 104, "winning_tip": winning_tip, "clean_exit": True},
            "persistence": {"clean_reload_height": 104, "forced_recovery_height": 104},
        })
        print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    finally:
        for minimal in minimals:
            minimal.kill()
        errors: list[str] = []
        for control in reversed(controls):
            try:
                control.stop()
            except Exception as error:
                errors.append(str(error))
        if errors:
            raise RuntimeError("; ".join(errors))
    return 0


if __name__ == "__main__":
    sys.exit(main())
