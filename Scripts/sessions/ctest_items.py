"""
여러 프로세스 네트워크 실행의 ctest 항목 — 이 폴더의 모듈을 훑어 `CTEST_ITEMS` 를 모은다. `IntegrationTests/Net/CMakeLists.txt` 가 `py -3 Scripts/sessions/ctest_items.py` 의 줄로 항목을 만든다.

모듈마다 `CTEST_ITEMS` 를 반드시 둔다(없으면 구성이 멈춘다) — 판정 명령을 만들고 등록을 잊으면 아무도 돌리지 않아 썩는다(`online-service-session people` 의 거래 단계는 한 번도 통과할 수 없던 기대를 품고 있었다).
"""

from __future__ import annotations

import importlib
import sys
from dataclasses import dataclass
from pathlib import Path

_SESSIONS_FOLDER = Path(__file__).resolve().parent
_NOT_TOOLS = frozenset({"__init__", "ctest_items"})


@dataclass(frozen=True)
class CtestItem:
    """항목 하나 — `py -3 -m Scripts` 뒤의 인자(명령 · 하위 명령), 시한, 띄우는 시나리오(`Content/` 기준)."""

    arguments: tuple[str, ...]
    timeout_seconds: int
    scenarios: tuple[str, ...] = ()

    @property
    def test_name(self) -> str:
        """`Net.<세션>.<하위 명령>` — 명령 이름에서 짓는다(`login-connect-session mmo` → `Net.loginConnect.mmo`). 따로 적으면 명령과 어긋났다."""
        command, subcommand = self.arguments[0], self.arguments[1]
        first, *rest = command.removesuffix("-session").split("-")
        return f"Net.{first}{''.join(word.capitalize() for word in rest)}.{subcommand}"


def read_items() -> list[CtestItem]:
    list_item: list[CtestItem] = []
    for path in sorted(_SESSIONS_FOLDER.glob("*.py")):
        if path.stem in _NOT_TOOLS:
            continue
        module = importlib.import_module(f"sessions.{path.stem}")
        if not hasattr(module, "CTEST_ITEMS"):
            raise RuntimeError(f"{path.name} 에 CTEST_ITEMS 가 없다 - 이 도구의 판정 명령을 ctest 항목으로 적는다(판정이 없으면 빈 튜플)")
        list_item += module.CTEST_ITEMS
    return list_item


def main() -> int:
    for item in read_items():
        print("|".join((item.test_name, str(item.timeout_seconds), ",".join(item.scenarios), ",".join(item.arguments))))
    return 0


if __name__ == "__main__":
    sys.path.insert(0, str(_SESSIONS_FOLDER.parent))  # Scripts
    raise SystemExit(importlib.import_module("sessions.ctest_items").main())
