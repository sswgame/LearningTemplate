"""
Engine 폴더 층 검사 — 낮은 층 폴더가 높은 층 폴더를 include 하지 못하게 한다(언리얼 Core → CoreUObject → Engine → Renderer 같은 모듈 DAG).

  py -3 -m Scripts engine-layers                 # Source/Engine 전체를 본다
  py -3 -m Scripts engine-layers --files a.cpp   # 고른 파일만(커밋 훅)
  py -3 -m Scripts engine-layers --update-baseline   # 지금 남은 역방향을 기준선에 적는다(줄일 때만)

형식으로 막을 수 없는 틈이라 게이트로 둔다: include 는 문자열이라 컴파일러가 층을 모른다. 층은 아래 `kLayer` 한 표가 정한다.
같은 층끼리는 include 해도 된다. 표에 없는 폴더는 오류다(새 폴더는 층을 정하고 들어온다).
기준선(`rules/EngineLayersBaseline.txt`)은 래칫이다 — 개편 중 아직 남은 역방향만 적혀 있고, 새 역방향은 통과하지 못하며,
고쳐서 사라진 줄은 `--update-baseline` 으로 지운다(늘리는 데 쓰지 않는다).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from common.ExitCode import ExitCode
from common.Paths import getProjectRoot

# 층(낮을수록 아래). 키는 `Source/Engine/` 아래 경로 접두어이고 가장 긴 것이 이긴다.
kLayer: dict[str, int] = {
    # 0 — 엔진 바닥: 공용 형식
    "Foundation": 0, "Compression": 0,
    "pch.h": 0, "pch.cpp": 0, "EngineMinimal.h": 0, "EngineFrameObserver.h": 0,
    # 1 — 기반: 리플렉션 · 직렬화 · 설정 · 모듈 · 관측
    "Reflection": 1, "Curve": 1, "Serialization": 1, "Config": 1, "Profiling": 1, "Observability": 1,
    "FileSystem": 1,  # 콘텐츠 파일 시스템(리소스 id · 경로 · 낱개 파일 · 팩 · 쿠킹 읽기 기록) — 직렬화 · 설정이 읽는다. 팩 매니저는 에셋 층이 건다(AssetIO::bindPackManager)
    "CoreServices": 1, "Console": 1,
    "Module/ModuleTypeRegistry.h": 1,  # 모듈 이미지의 타입 등록 선언(Core 만) — 구현(.cpp)은 리로드 때 씬을 다시 묶어 맨 위 층이다
    # 2 — 콘텐츠: 에셋 · 글 · 현지화
    "Asset": 2, "Text": 2, "Localization": 2,
    # 3 — 플랫폼: 창 · 입력
    "Window": 3, "Input": 3,
    # 4 — RHI · CPU 쪽 그래픽 데이터
    "Graphics": 4,
    # 5 — 월드: 컴포넌트 모델 코어 · 씬
    "Object": 5, "Scene": 5, "Automation/Watch": 5,  # 감시 값은 대상 객체를 찾아 읽는다 — 기능(6)이 자기 폴더에서 등록한다
    # 6 — 기능: 각자 자기 컴포넌트 · 씬 서비스를 든다
    "Physics": 6, "Audio": 6, "Animation": 6, "Navigation": 6, "TileMap": 6, "VFX": 6, "Sequencer": 6, "Dialogue": 6,
    "Environment": 6, "Destruction": 6, "DedicatedServer": 6, "Character": 6, "UI": 6, "Net": 6, "UserSettings": 6, "Automation/Scenario": 6,
    "2D": 6,  # 2D 컴포넌트(스프라이트 · 2D 라이트 · 그림자 · 패럴랙스 · 픽셀 퍼펙트 카메라) — 스프라이트 클립(Animation) · 타일맵을 쓴다(언리얼 Paper2D)
    "Telemetry": 6,  # 크래시 · 원격 측정은 엔진 전체(사용자 설정 · 하드웨어 · 팩 설정)를 보고한다 — 엔진 루프(8)만 include 한다
    # 7 — 렌더러(렌더 스레드 · GPU 파이프라인, 전용 서버는 짓지 않는다)
    "Renderer": 7,
    # 8 — 맨 위: 엔진 루프 조립 · 모듈 핫 리로드(씬 · 에셋 · 입력을 다시 묶는다) · 자동화 실행기
    "Module": 8, "Automation": 8, "Engine.h": 8, "Engine.cpp": 8, "EngineSubsystemSet.h": 8, "EngineSubsystemSet.cpp": 8,
    "HeadlessSubsystem.h": 8, "HeadlessSubsystem.cpp": 8, "HostServices.h": 8,
}
kBaselinePath = Path(__file__).with_name("rules") / "EngineLayersBaseline.txt"
kInclude = re.compile(r'^\s*#\s*include\s*"Engine/([^"]+)"', re.M)


def findLayer(relative: str) -> tuple[str, int] | None:
    """`Source/Engine/` 기준 경로의 층 키와 층. 표에 없으면 None."""
    best = None
    for key, layer in kLayer.items():
        if relative == key or relative.startswith(key + "/"):
            if best is None or len(key) > len(best[0]):
                best = (key, layer)
    return best


def collectViolations(root: Path, listFile: list[Path]) -> tuple[list[str], list[str]]:
    """(역방향 include 줄, 층이 없는 파일) — 줄 모양은 `<파일> -> Engine/<헤더>`."""
    engine = root / "Source" / "Engine"
    listUpward: list[str] = []
    listUnknown: list[str] = []
    for path in listFile:
        try:
            relative = path.resolve().relative_to(engine.resolve()).as_posix()
        except ValueError:
            continue
        source = findLayer(relative)
        if source is None:
            listUnknown.append(relative)
            continue
        for target in kInclude.findall(path.read_text(encoding="utf-8", errors="ignore")):
            found = findLayer(target)
            if found is None:
                listUnknown.append(target)
            elif found[1] > source[1]:
                listUpward.append(f"{relative} -> Engine/{target}")
    return listUpward, sorted(set(listUnknown))


def checkEngineLayers(root: Path, listFile: list[Path] | None = None) -> list[str]:
    """기준선에 없는 역방향 include · 층 없는 폴더를 돌려준다(커밋 훅이 부른다)."""
    files = listFile if listFile is not None else [p for p in (root / "Source" / "Engine").rglob("*") if p.suffix in {".h", ".cpp", ".inl"}]
    listUpward, listUnknown = collectViolations(root, files)
    baseline = set(kBaselinePath.read_text(encoding="utf-8").splitlines()) if kBaselinePath.is_file() else set()
    return [f"역방향 include: {line}" for line in listUpward if line not in baseline] + [f"층 표(kLayer)에 없는 경로: Engine/{item}" for item in listUnknown]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts engine-layers", description="Engine 폴더 층 검사")
    parser.add_argument("--files", nargs="*", help="이 파일만 본다")
    parser.add_argument("--update-baseline", action="store_true", help="지금 남은 역방향을 기준선으로(줄일 때만 쓴다)")
    args = parser.parse_args(argv)

    root = getProjectRoot()
    if args.update_baseline:
        files = [p for p in (root / "Source" / "Engine").rglob("*") if p.suffix in {".h", ".cpp", ".inl"}]
        listUpward, listUnknown = collectViolations(root, files)
        if listUnknown:
            print("[EngineLayers] 층 표에 없는 경로가 있어 기준선을 쓰지 않는다: " + ", ".join(listUnknown), file=sys.stderr)
            return ExitCode.Violation
        old = set(kBaselinePath.read_text(encoding="utf-8").splitlines()) if kBaselinePath.is_file() else set()
        added = sorted(set(listUpward) - old)
        if old and added:
            print("[EngineLayers] 기준선은 줄이기만 한다 — 새 역방향:\n  " + "\n  ".join(added), file=sys.stderr)
            return ExitCode.Violation
        kBaselinePath.parent.mkdir(parents=True, exist_ok=True)
        kBaselinePath.write_text("\n".join(sorted(set(listUpward))) + "\n", encoding="utf-8")
        print(f"[EngineLayers] 기준선 {len(set(listUpward))} 줄(이전 {len(old)})")
        return ExitCode.Passed

    files = [Path(f) if Path(f).is_absolute() else root / f for f in args.files] if args.files else None
    listViolation = checkEngineLayers(root, files)
    for line in listViolation:
        print(f"  - {line}")
    print(f"[EngineLayers] {'FAIL ' + str(len(listViolation)) + '건' if listViolation else 'OK'}")
    return ExitCode.Violation if listViolation else ExitCode.Passed
