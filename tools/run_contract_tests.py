#!/usr/bin/env python3
"""Run every contract test without requiring pytest."""

from __future__ import annotations

import importlib.util
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    test_path = root / "tests" / "test_cpp_contract.py"
    spec = importlib.util.spec_from_file_location("reboot_esp_contract", test_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {test_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    passed: list[str] = []
    for name, function in sorted(vars(module).items()):
        if name.startswith("test_") and callable(function):
            function()
            passed.append(name)
    print(f"CONTRACT_SUMMARY passed={len(passed)} failed=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
