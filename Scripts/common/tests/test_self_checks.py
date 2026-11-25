"""
스크립트의 자체 검사(`self_check` · `run_self_check`)를 모두 돈다 — 목록이 없다: `Scripts/` 를 훑어 그 함수를 정의한 모듈을 찾는다.

모듈마다 `--self-check` 를 사람이 손으로 부르게 두면 썩는다(2026-11-25: app_arguments · linux_scenarios · naming_census · server_image · function_spans 의 검사를 아무것도 돌리지 않았다).
판정: 참 · None · 0 이면 통과, 거짓 · 0 아닌 수 · 예외면 실패.

실행: python -X utf8 Scripts/common/tests/test_self_checks.py (CTest `Scripts.SelfChecks`).
"""

from __future__ import annotations

import ast
import contextlib
import importlib
import io
import sys
import unittest
from pathlib import Path

SCRIPTS_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SCRIPTS_ROOT))

SELF_CHECK_NAMES = frozenset({"self_check", "run_self_check"})


def find_self_checks() -> list[tuple[str, str]]:
    """(모듈, 함수). 필수 인자가 있는 것(다른 규칙의 검사를 대신 부르는 `lint.pre_commit.run_self_check`)은 뺀다."""
    list_found = []
    for path in sorted(SCRIPTS_ROOT.rglob("*.py")):
        if "tests" in path.parts:
            continue
        text = path.read_text(encoding="utf-8")
        if "self_check" not in text:
            continue
        module = ".".join(path.relative_to(SCRIPTS_ROOT).with_suffix("").parts)
        for node in ast.parse(text).body:
            if isinstance(node, ast.FunctionDef) and node.name in SELF_CHECK_NAMES and len(node.args.args) == len(node.args.defaults):
                list_found.append((module, node.name))
    return list_found


class SelfChecks(unittest.TestCase):
    def test_every_self_check_passes(self) -> None:
        list_found = find_self_checks()
        self.assertTrue(list_found, "자체 검사를 하나도 찾지 못했다 - 이름 규칙이 바뀌었으면 SELF_CHECK_NAMES 를 고친다")
        for module, name in list_found:
            with self.subTest(module=module):
                output = io.StringIO()
                with contextlib.redirect_stdout(output):
                    result = getattr(importlib.import_module(module), name)()
                self.assertTrue(result is None or result is True or (result is not False and result == 0), f"{module}.{name} -> {result!r}\n{output.getvalue()}")


if __name__ == "__main__":
    unittest.main()
