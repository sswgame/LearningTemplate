#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
지금 이 트리에서 **혼자 서지 못하는 헤더**를 전부 묻는다.

[왜 필요한가 — 혼자 서지 못하는 헤더는 남의 사정에 기대어 컴파일된다]
헤더가 자기가 쓰는 이름의 선언을 직접 include 하지 않아도, 그것을 먼저 include 해 준 다른 헤더가
있으면 빌드는 통과한다. 그러다 그 "다른 헤더" 가 정리되는 날 **내 코드를 한 줄도 안 고쳤는데**
빌드가 깨진다. 그리고 그 깨짐은 늘 엉뚱한 파일에서 난다.

[이 검사는 오래 무의미했다]
`ReflectionParser` 가 만들던 `FlagOps.gen.h` 우산이 `/FI` 로 타깃 **전 TU** 에 강제 include 되면서
`RHITypes.h` · `MaterialTypes.h` · `ShaderCompiler.h` · `ResourcePackTypes.h` 를 끌고 들어왔다.
그 넷이 다시 `Engine/Common/Common.h`(File · Math · String · Time · ResourceUtil 우산)까지 끌어오니,
웬만한 이름은 전부 "이미 있는" 것이 되어 누락이 보이지 않았다. 게다가 그 우산의 내용은 "플래그
열거형을 가진 헤더가 무엇이냐" 에 따라 바뀌므로, **오늘 서는 헤더가 내 코드를 안 고쳐도 내일 못 설
수 있었다.** 2026-09-18 에 우산을 걷어내고(비트 연산자 트레이트는 이제 열거형 헤더와 함께 다닌다 —
`CheckFlagEnumTraitInclude.py` 가 지킨다) 다시 재니 Engine 헤더 230 개 중 5 개가 서지 못했다.

[게이트가 아니다]
`Run*` 은 보고하고 `Check*` 이 막는다(`RunBuildWarnings.py` 와 같은 규칙). 막지 않는 이유는 비용이다 —
헤더 하나에 컴파일러를 한 번씩 부르므로 6코어에서 전 트리 약 3 분이 걸린다. 린트 스위트 전체가
30 초인데 여기에 3 분을 얹으면 아무도 린트를 돌리지 않게 된다. 대신 **되돌아오는 길을 막는 것**은
게이트가 한다: `CheckFlagEnumTraitInclude.py` 가 우산이 다시 생기는 것을 막는다.

[플래그는 진짜 빌드에서 그대로 빌려 온다]
`compile_commands.json` 에서 그 헤더와 경로가 가장 많이 겹치는 TU 를 골라 그 명령줄을 쓴다 —
Editor 헤더는 Editor TU 의 플래그로, Engine 헤더는 Engine TU 의 플래그로 본다. 검사기가 자기만의
플래그 목록을 들면 그 목록이 또 썩는다. PCH(`/Yu` · `/Fp` · `/FI cmake_pch`)만 떼어 낸다 —
그대로 두면 pch 가 미리 넣어 준 것이 또 누락을 가린다.

[언제 쓰나 — 매번은 아니다]
한 폴더를 훑어 끝냈을 때, 헤더를 여럿 옮기거나 include 를 정리한 뒤, 남의 커밋을 받은 뒤에 돌린다.

사용법:
  py -3 Scripts/lint/report/RunHeaderSelfContained.py                       # Source/ 전부
  py -3 Scripts/lint/report/RunHeaderSelfContained.py --filter Engine/Graphics
  py -3 Scripts/lint/report/RunHeaderSelfContained.py --files Source/Engine/Scene/Scene.h
  py -3 Scripts/lint/report/RunHeaderSelfContained.py --build build/Ninja-Shipping --jobs 8
"""

from __future__ import annotations

import argparse
import concurrent.futures as futures
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

from common import useUtf8Stdout  # noqa: E402

kDefaultBuildDir = Path("build") / "Ninja-Debug"
kScanRoot = "Source"
# 생성 헤더와 PCH 는 애초에 혼자 서는 것이 목적이 아니다.
kSkipSuffixes = (".gen.h",)
kSkipNames = ("pch.h",)


def loadCompileDatabase(buildDir: Path) -> list[dict]:
    """compile_commands.json 을 읽는다."""
    dbPath = buildDir / "compile_commands.json"
    if not dbPath.is_file():
        print(f"[HeaderSelfContained] 컴파일 DB 가 없습니다: {dbPath.as_posix()}", file=sys.stderr)
        print("[HeaderSelfContained] `cmake --preset Ninja-Debug` 를 먼저 돌리세요.", file=sys.stderr)
        return []
    try:
        return json.loads(dbPath.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        print(f"[HeaderSelfContained] 컴파일 DB 를 읽지 못했습니다: {error}", file=sys.stderr)
        return []


def normalizePath(rawPath: str) -> str:
    """경로 구분자를 `/` 로 통일한다."""
    return rawPath.replace("\\", "/")


def makeSeedIndex(database: Sequence[dict]) -> list[tuple[str, dict]]:
    """TU 를 자기 디렉터리와 함께 늘어놓는다 — 헤더와 가장 많이 겹치는 것을 고르기 위한 표다."""
    seeds: list[tuple[str, dict]] = []
    for entry in database:
        filePath = normalizePath(entry.get("file", ""))
        if filePath:
            seeds.append((filePath.rsplit("/", 1)[0] + "/", entry))
    return seeds


def findSeedEntry(headerPath: str, seeds: Sequence[tuple[str, dict]]) -> dict | None:
    """헤더와 디렉터리 경로가 가장 길게 겹치는 TU 를 고른다."""
    bestEntry: dict | None = None
    bestLength = -1
    for seedDir, entry in seeds:
        length = len(os.path.commonprefix([seedDir, headerPath]))
        if length > bestLength:
            bestLength = length
            bestEntry = entry
    return bestEntry


def makeSyntaxOnlyCommand(entry: dict, probeSource: Path) -> list[str]:
    """TU 의 명령줄에서 PCH·출력 경로·소스 파일을 떼고 `-fsyntax-only` 로 바꾼다."""
    rawCommand = entry.get("command") or " ".join(entry.get("arguments", []))
    command: list[str] = []
    for token in shlex.split(rawCommand, posix=False):
        if token.startswith(("/Yu", "/Fp", "/Fo", "/Fd")):
            continue
        if token.startswith("/FI") and "cmake_pch" in token:
            continue
        if token in ("-c", "--"):
            continue
        if token.endswith((".cpp", ".cc", ".cxx")):
            continue
        command.append(token)

    command.extend(["-fsyntax-only", "-Wno-unused-command-line-argument", str(probeSource)])
    return command


def isCheckedHeader(headerPath: Path) -> bool:
    """검사 대상 헤더인지 본다."""
    return headerPath.name not in kSkipNames and not headerPath.name.endswith(kSkipSuffixes)


def collectHeaders(repositoryRoot: Path, pathFilter: str) -> list[Path]:
    """검사 대상 헤더를 모은다."""
    scanRoot = repositoryRoot / kScanRoot
    if not scanRoot.is_dir():
        return []
    headers = [p for p in sorted(scanRoot.rglob("*.h")) if isCheckedHeader(p)]
    if pathFilter:
        needle = normalizePath(pathFilter)
        headers = [p for p in headers if needle in normalizePath(str(p))]
    return headers


def makeIncludeSpelling(repositoryRoot: Path, headerPath: Path) -> str | None:
    """`Source/` 기준 상대 경로 — 저장소가 실제로 쓰는 include 철자다."""
    try:
        return headerPath.resolve().relative_to((repositoryRoot / kScanRoot).resolve()).as_posix()
    except ValueError:
        return None


def checkOneHeader(repositoryRoot: Path, buildDir: Path, headerPath: Path,
                   seeds: Sequence[tuple[str, dict]], probeDir: Path) -> tuple[str, str] | None:
    """헤더 하나를 단독 컴파일한다. 서지 못하면 (철자, 첫 오류) 를 반환한다."""
    includeSpelling = makeIncludeSpelling(repositoryRoot, headerPath)
    if includeSpelling is None:
        return None

    entry = findSeedEntry(normalizePath(str(headerPath.resolve())), seeds)
    if entry is None:
        return None

    probeSource = probeDir / (includeSpelling.replace("/", "_")[:-2] + "_probe.cpp")
    probeSource.write_text('#include "%s"\n' % includeSpelling, encoding="utf-8")

    completed = subprocess.run(
        " ".join(makeSyntaxOnlyCommand(entry, probeSource)),
        shell=True, capture_output=True, text=True, errors="replace", cwd=str(buildDir),
    )
    if completed.returncode == 0:
        return None

    output = (completed.stdout or "") + (completed.stderr or "")
    reasons = re.findall(r"error: (.+)", output)
    return includeSpelling, (reasons[0].strip() if reasons else "컴파일 실패 (오류 메시지 없음)")


def main() -> int:
    parser = argparse.ArgumentParser(description="혼자 서지 못하는 헤더를 보고한다 (게이트 아님).")
    parser.add_argument("--root", default=".", help="저장소 루트")
    parser.add_argument("--build", default=str(kDefaultBuildDir), help="컴파일 DB 가 있는 빌드 디렉터리")
    parser.add_argument("--filter", default="", help="경로에 이 문자열이 든 헤더만")
    parser.add_argument("--files", nargs="*", default=None, help="검사할 헤더 경로 목록")
    parser.add_argument("--jobs", type=int, default=0, help="동시 실행 수 (0 이면 CPU 수 × 4)")
    args = parser.parse_args()

    useUtf8Stdout()

    repositoryRoot = Path(args.root).resolve()
    buildDir = Path(args.build)
    if not buildDir.is_absolute():
        buildDir = repositoryRoot / buildDir

    seeds = makeSeedIndex(loadCompileDatabase(buildDir))
    if not seeds:
        return 0   # 보고 스크립트는 막지 않는다 — 돌 수 없으면 그렇다고 말하고 끝낸다.

    if args.files:
        headers = [Path(p) if Path(p).is_absolute() else repositoryRoot / p for p in args.files]
        headers = [p for p in headers if p.suffix == ".h" and p.is_file() and isCheckedHeader(p)]
    else:
        headers = collectHeaders(repositoryRoot, args.filter)

    if not headers:
        print("[HeaderSelfContained] 검사할 헤더가 없습니다.")
        return 0

    jobCount = args.jobs if args.jobs > 0 else min(32, (os.cpu_count() or 4) * 4)
    print(f"[HeaderSelfContained] 헤더 {len(headers)}개를 단독 컴파일합니다 (동시 {jobCount}) …")

    with tempfile.TemporaryDirectory(prefix="swHeaderProbe") as probeDirName:
        probeDir = Path(probeDirName)
        with futures.ThreadPoolExecutor(max_workers=jobCount) as pool:
            results = list(pool.map(
                lambda header: checkOneHeader(repositoryRoot, buildDir, header, seeds, probeDir),
                headers,
            ))

    failures = [r for r in results if r is not None]
    if not failures:
        print(f"[HeaderSelfContained] OK — 헤더 {len(headers)}개가 전부 혼자 섭니다.")
        return 0

    print(f"\n[HeaderSelfContained] 혼자 서지 못하는 헤더 {len(failures)}개:\n")
    for spelling, reason in failures:
        print(f"  {spelling}\n      {reason}")
    print("\n  그 헤더가 직접 쓰는 이름의 선언을 그 헤더가 직접 include 하세요.")
    print("  지금 컴파일되는 것은 남이 먼저 include 해 준 덕이고, 그 남이 바뀌면 깨집니다.")
    return 0   # 보고만 한다.


if __name__ == "__main__":
    sys.exit(main())
