"""
cmake 구성 · 빌드의 입구 — 워크트리에서는 멈추고(빌드는 메인 작업 폴더 하나), 지어 둔 App 을 돌리기 전 증분 빌드를 한다.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Sequence

from common.run_process import ProcessResult, run_process


def run_cmake(list_argument: Sequence[str | Path], *, cwd: Path, capture: bool = True) -> ProcessResult:
    """
    cmake 구성 · 빌드의 입구(`cmake --preset` · `cmake --build --preset`). 연결된 워크트리(`.git` 이 파일)에서는 멈춘다 — 빌드는 메인 작업 폴더
    하나에서만 한다(공용 vcpkg · 디스크가 겹친다, 사용자 지시 2026-11-12). 작업자는 코드 → 린트 → 커밋만 하고, 컴파일 · 실행은 통합이 본 트리에서 한다.
    """
    if (cwd / ".git").is_file():
        raise SystemExit(f"[Scripts] 워크트리({cwd})에서 cmake {' '.join(str(part) for part in list_argument)} 를 시작하지 않는다 — 빌드 · 구성은 메인 작업 폴더에서만")
    return run_process(["cmake", *list_argument], cwd=cwd, capture=capture)


def build_preset_or_exit(preset: str, *, cwd: Path) -> None:
    """
    지어 둔 App 으로 무엇을 돌리기 전에 `preset` 을 증분 빌드한다(바뀐 것이 없으면 몇 초). 짓지 않으면 고친 코드 대신 옛 바이너리가 돌아 고친 단계가 다시 지거나,
    모듈 리로드처럼 소스와 바이너리를 견주는 시나리오가 거짓으로 진다(preflight.sh --from 과 같은 틈). 지면 멈춘다 — 반쯤 지은 바이너리로 돌리지 않는다.
    """
    print(f"{preset} 증분 빌드", flush=True)
    result = run_cmake(["--build", "--preset", preset], cwd=cwd)
    if not result.succeeded:
        sys.exit("\n".join([line for line in result.stdout.splitlines() if "error" in line or "FAILED" in line][:30] + [f"{preset} 빌드 실패 — 고친 뒤 다시 돌린다"]))
