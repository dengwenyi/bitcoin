#!/usr/bin/env python3
"""Run a command after removing case-insensitive duplicate environment keys."""

from __future__ import annotations

import os
import subprocess
import sys


def main() -> int:
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} COMMAND [ARG ...]", file=sys.stderr)
        return 2

    # Windows environment keys are case-insensitive, while some automation
    # hosts can still inject both Path and PATH. MSBuild/CL fail when copying
    # such an environment to child processes. Canonicalizing all names removes
    # the duplicate without changing values or the parent process.
    environment = {name.upper(): value for name, value in os.environ.items()}
    path_prepend = environment.pop("CODEX_CLEAN_PATH_PREPEND", "")
    if path_prepend:
        environment["PATH"] = path_prepend + os.pathsep + environment.get("PATH", "")
    return subprocess.call(sys.argv[1:], env=environment, stdin=subprocess.PIPE)


if __name__ == "__main__":
    sys.exit(main())
