#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
clang-tidy 정적 분석 실행기.

검사 목록은 저장소 루트의 `.clang-tidy` 가 정한다 — 이 스크립트는 **무엇을 검사할지 정하지
않는다.** 여기서 하는 일은 (1) 컴파일 DB 를 찾고, (2) 우리 소스만 골라내고, (3) 결과를
종류별로 묶어 읽을 수 있게 내놓는 것이다.

왜 스크립트가 필요한가: 호출 인자를 매번 손으로 만들면 사람마다 다른 검사 집합을 돌린다.
실제로 처음 돌렸을 때 `bugprone-easily-swappable-parameters` 와
`clang-analyzer-optin.core.EnumCastOutOfRange` 가 158건 쏟아져 진짜 결함을 덮었다. 그 둘을
`.clang-tidy` 에서 끈 이유도 거기 적혀 있다 — 같은 분류 작업을 다시 하지 않으려면 설정과
실행 방법이 같이 저장소에 있어야 한다.

사용법:
  py -3 Scripts/lint/RunClangTidy.py                      # Source 전체
  py -3 Scripts/lint/RunClangTidy.py --filter Core        # 경로에 Core 가 들어간 TU 만
  py -3 Scripts/lint/RunClangTidy.py --preset Ninja-Debug-ASAN
  py -3 Scripts/lint/RunClangTidy.py --jobs 4 --out tidy.txt
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import getProjectRoot, useUtf8Stdout

_kDiagnosticRe = re.compile(r"\[([a-z][a-zA-Z0-9.-]*-[a-zA-Z0-9.-]+)\]\s*$")


def findClangTidy() -> str:
    """clang-tidy 실행 파일 경로. PATH 에 없으면 LLVM 기본 설치 위치를 본다."""
    for candidate in ("clang-tidy", r"C:\Utility\LLVM\bin\clang-tidy.exe", "/usr/bin/clang-tidy"):
        try:
            subprocess.run([candidate, "--version"], capture_output=True, check=True)
            return candidate
        except (OSError, subprocess.CalledProcessError):
            continue
    print("[RunClangTidy] clang-tidy 를 찾지 못했습니다. LLVM 설치를 확인하세요.")
    sys.exit(2)


def collectTranslationUnits(buildDir: Path, pathFilter: str) -> list[str]:
    """컴파일 DB 에서 우리 Source 의 TU 만 고른다. 생성 코드와 서드파티는 뺀다."""
    databasePath = buildDir / "compile_commands.json"
    if databasePath.exists() is False:
        print(f"[RunClangTidy] {databasePath} 가 없습니다. 먼저 configure 하세요.")
        sys.exit(2)

    entries = json.loads(databasePath.read_text(encoding="utf-8"))
    listUnit: list[str] = []
    for entry in entries:
        filePath = entry["file"].replace("\\", "/")
        if "/Source/" not in filePath:
            continue
        # 생성 코드는 우리가 고칠 대상이 아니다(리플렉션 코드젠 산출물).
        if "/generated/" in filePath or filePath.endswith(".gen.cpp"):
            continue
        if pathFilter and pathFilter.lower() not in filePath.lower():
            continue
        listUnit.append(entry["file"])

    return sorted(set(listUnit))


def runOne(tidyExe: str, buildDir: Path, sourceFile: str) -> str:
    """TU 하나를 검사한다. 설정은 .clang-tidy 가 가지므로 검사 목록을 넘기지 않는다."""
    # `/Y-` 로 PCH 옵션을 무효화한다. clang-tidy 는 clang-cl 이 만든 MSVC PCH 를 쓸 수 없는데,
    # 컴파일 DB 에는 `/Yu` 와 `/FI cmake_pch.hxx` 가 남아 있다. 그러면 같은 헤더를 강제 포함으로
    # 한 번, 정상 include 로 또 한 번 파싱하고, 경로 표기가 엇갈려(`Source\Core/Common/...`)
    # `#pragma once` 가 같은 파일로 보지 못해 **"redefinition of ..." 오류가 쏟아진다.**
    # 코드 결함이 아니라 전부 이 설정 때문이다.
    command = [tidyExe, "-p", str(buildDir), "--quiet", "--extra-arg-before=/Y-", sourceFile]
    try:
        completed = subprocess.run(command, capture_output=True, text=True,
                                   encoding="utf-8", errors="replace", timeout=900)
    except subprocess.TimeoutExpired:
        return f"{sourceFile}: [RunClangTidy] 시간 초과(900s) — 건너뜁니다\n"
    return completed.stdout or ""


def summarize(diagnosticText: str) -> None:
    """종류별 건수와 자리를 묶어 낸다. 같은 헤더가 여러 TU 에서 중복되므로 유일화한다."""
    listLine = [line for line in diagnosticText.splitlines() if ": warning:" in line or ": error:" in line]
    setUnique = sorted(set(listLine))

    counter: Counter[str] = Counter()
    for line in setUnique:
        matched = _kDiagnosticRe.search(line)
        counter[matched.group(1) if matched else "(분류 없음)"] += 1

    print("")
    print("=" * 70)
    print(f"  clang-tidy 결과 — 고유 지적 {len(setUnique)}건")
    print("=" * 70)
    if not setUnique:
        print("  지적 없음.")
        return

    for name, count in counter.most_common():
        print(f"  {count:5}  {name}")

    print("")
    print("-" * 70)
    for line in setUnique:
        print(f"  {line.strip()}")


def main() -> int:
    useUtf8Stdout()
    projectRoot = getProjectRoot()

    parser = argparse.ArgumentParser(description="clang-tidy 정적 분석 실행")
    parser.add_argument("--preset", default="Ninja-Debug", help="컴파일 DB 를 가져올 빌드 프리셋")
    parser.add_argument("--filter", default="", help="경로 부분 문자열로 TU 를 고릅니다 (예: Core)")
    parser.add_argument("--jobs", type=int, default=5, help="병렬 실행 수")
    parser.add_argument("--out", default="", help="원본 출력을 저장할 파일")
    args = parser.parse_args()

    buildDir = projectRoot / "build" / args.preset
    tidyExe = findClangTidy()
    listUnit = collectTranslationUnits(buildDir, args.filter)

    if not listUnit:
        print("[RunClangTidy] 검사할 TU 가 없습니다.")
        return 0

    print(f"[RunClangTidy] {len(listUnit)}개 TU, 병렬 {args.jobs} ({args.preset})")
    listOutput: list[str] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(runOne, tidyExe, buildDir, unit) for unit in listUnit]
        for index, future in enumerate(concurrent.futures.as_completed(futures), start=1):
            listOutput.append(future.result())
            if index % 25 == 0:
                print(f"  ... {index}/{len(listUnit)}")

    diagnosticText = "".join(listOutput)
    if args.out:
        Path(args.out).write_text(diagnosticText, encoding="utf-8")
        print(f"[RunClangTidy] 원본 출력 → {args.out}")

    summarize(diagnosticText)

    # 지적이 있어도 실패로 만들지 않는다. 정적 분석은 판단이 필요한 자료이고, 게이트는
    # CheckCodeConventions.py 가 맡는다. 게이트로 쓰려면 .clang-tidy 의 WarningsAsErrors 를 켠다.
    return 0


if __name__ == "__main__":
    sys.exit(main())
