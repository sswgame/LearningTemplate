#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
지금 이 트리에 남아 있는 **컴파일러 경고 전부**를 묻는다.

[왜 필요한가 — 경고는 딱 한 번만 보인다]
컴파일러 경고는 **TU 가 컴파일되는 그 순간에만** 출력된다. ninja 는 바뀌지 않은 TU 를 다시
컴파일하지 않으므로, 오늘 들어온 경고는 한 번 지나가고 그 뒤로는 영원히 보이지 않는다.
빌드를 50번 더 돌려도, 출력을 한 줄도 빠짐없이 읽어도 마찬가지다 — 그 파일은 다시 컴파일되지
않기 때문이다. 2026-09-13 에 다섯 종류가 쌓여 있던 이유가 이것이다(그중 하나는
`inline static inline static` 이라는 명백한 오타였다).

그래서 문제는 "경고를 못 보고 지나쳤다" 가 아니라 **"지금 트리에 경고가 몇 개인지 물어볼
방법이 없었다"** 였다. clean 빌드를 하면 되지만 아무도 매번 clean 빌드를 하지 않는다.
이 스크립트가 그 질문에 답한다 — 컴파일 DB 의 **실제 빌드 명령 그대로**, 코드 생성 없이
(`-fsyntax-only`) 전 TU 를 훑는다. 무엇이 dirty 인지와 무관하게 늘 같은 답이 나온다.

[게이트가 아니다]
지적이 있어도 실패로 만들지 않는다(`RunClangTidy.py` 와 같은 규칙 — `Run*` 은 보고하고
`Check*` 이 막는다). `-Werror` 를 걸지 않은 이유는 **구성마다·컴파일러 버전마다 경고 집합이
다르기 때문**이다. 그래서 막는 대신 **보이게** 만든다.

[구성마다 다르다 — 그래서 기본이 셋 다 이다]
`-Wunused-function` 은 Release 에만, `-Wunused-private-field` 는 Shipping 에만 나온다
(로그 매크로가 컴파일에서 빠지면서 소비자가 사라지는 자리들이다). 한 구성만 보면 그만큼을
놓친다.

[언제 쓰나 — 매번은 아니다]
**방금 내가 만든 경고는 방금 그 빌드가 이미 알려 준다.** 내 변경으로 다시 컴파일된 TU 가 곧
영향 범위이기 때문이다(헤더를 고쳤으면 그걸 포함한 TU 도 전부 다시 컴파일된다). 이 스크립트가
답하는 것은 다른 질문이다 — **"지금 트리 전체에 남아 있는 경고가 몇 개인가."** 그래서 한 덩어리
작업을 마칠 때, 오래 안 본 구성을 볼 때, 남의 커밋을 받은 뒤에 돌린다.

사용법:
  py -3 Scripts/lint/RunBuildWarnings.py                       # Debug · Release · Shipping 전부
  py -3 Scripts/lint/RunBuildWarnings.py --preset Ninja-Debug  # 한 구성만
  py -3 Scripts/lint/RunBuildWarnings.py --filter Graphics     # 경로에 Graphics 가 든 TU 만
  py -3 Scripts/lint/RunBuildWarnings.py --jobs 8 --out warnings.txt
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import getProjectRoot, useUtf8Stdout

# "path(line,col): warning: 본문 [-Wname]" / GNU 드라이버의 "path:line:col: warning: ..." 둘 다 받는다.
_kDiagnosticRe = re.compile(r"^(?P<where>.+?):\s*(?P<kind>warning|error):\s*(?P<text>.*)$")
_kFlagRe = re.compile(r"\[-W(?P<flag>[A-Za-z0-9_#+-]+)\]\s*$")

#: 결과물을 만드는 인자들. 문법 검사만 할 것이므로 전부 뺀다.
_kDropExact = ("-c", "/c", "/showIncludes", "-MD", "-clang:-MD")
_kDropWithValue = ("-o", "-MT", "-MF")
_kDropPrefix = ("/Fo", "/Fd", "/Fp", "/Yu", "/Yc", "-clang:-MT", "-clang:-MF")

_kDefaultPresets = ("Ninja-Debug", "Ninja-Release", "Ninja-Shipping")


def splitCommandInternal(entry: dict) -> tuple[list[str], bool]:
    """
    컴파일 DB 한 줄을 인자 목록으로 만든다. `arguments` 가 있으면 그쪽이 정확하다.

    @return (토큰 목록, 목록 그대로 실행해도 되는가)

    CMake 의 Ninja 제너레이터는 `arguments` 가 아니라 `command` **문자열**을 낸다. 윈도우에서는
    그 안에 `-DSW_LOG_TAG=\\"Engine\\"` 처럼 **명령줄 규칙으로 이스케이프된** 값이 들어 있어서,
    토큰으로 쪼갠 뒤 목록으로 넘기면 파이썬이 따옴표를 다시 붙여 깨진다(실제로
    `missing terminating '"' character` 가 났다). 그래서 윈도우는 원본 철자를 보존하는
    posix=False 로 쪼갠 뒤 **다시 이어 붙여 명령줄 문자열로** 넘기고, POSIX 는 posix=True 로
    제대로 쪼개 **목록 그대로** 넘긴다(셸을 태우지 않으므로 경로의 공백도 안전하다).
    """
    if isinstance(entry.get("arguments"), list):
        return list(entry["arguments"]), True
    import shlex

    bIsWindows = os.name == "nt"
    listToken = shlex.split(entry.get("command", ""), posix=not bIsWindows)
    return listToken, not bIsWindows


def buildSyntaxOnlyCommandInternal(entry: dict) -> tuple[list[str] | str, bool]:
    """
    빌드 명령을 **문법 검사 전용**으로 바꾼다. 경고 관련 인자는 하나도 건드리지 않는다 —
    그래야 여기서 나오는 경고가 실제 빌드에서 나오는 경고와 같다.

    @return (실행할 명령, 셸이 필요한가 — 지금은 늘 False)
    """
    listArgument, bIsArgumentList = splitCommandInternal(entry)
    if not listArgument:
        return [], False

    # sccache 래퍼를 벗긴다. 문법 검사는 캐시 대상이 아니고, 래퍼를 거치면 인자 해석만 한 겹 는다.
    if "sccache" in Path(listArgument[0].strip('"')).name.lower():
        listArgument = listArgument[1:]

    listOut: list[str] = []
    skipNext = False
    for argument in listArgument:
        if skipNext:
            skipNext = False
            continue
        if argument in _kDropWithValue:
            skipNext = True
            continue
        if argument in _kDropExact:
            continue
        if argument.startswith(_kDropPrefix):
            continue
        listOut.append(argument)

    # PCH 를 쓰지 않는다. `/Yu` 로 미리 파싱된 헤더를 불러오면 **그 헤더들의 경고가 다시 나오지
    # 않는다** — 경고가 숨는 것을 없애려는 도구가 같은 방식으로 숨으면 안 된다.
    #
    # 드라이버 판별은 **실행 파일 이름**으로 한다. "슬래시로 시작하는 인자가 있으면 MSVC 드라이버"
    # 로 보면 POSIX 에서 소스 파일의 절대경로(`/home/...`)에 걸려 GNU 드라이버에 `/Y-` 를 넘기게 되고,
    # 그러면 그것이 입력 파일로 해석된다.
    driverName = Path(listOut[0].strip('"')).name.lower()
    bIsMsvcDriver = "clang-cl" in driverName or driverName in ("cl.exe", "cl")
    if bIsMsvcDriver:
        listOut.insert(1, "/Y-")

    # 입력 파일 앞의 `--` 뒤는 전부 입력으로 해석된다. 추가 인자는 반드시 그 앞에 넣어야 한다.
    syntaxOnly = "-fsyntax-only"
    if "--" in listOut:
        listOut.insert(listOut.index("--"), syntaxOnly)
    else:
        listOut.insert(1, syntaxOnly)

    if bIsArgumentList:
        return listOut, False
    # 윈도우만 명령줄 문자열로 돌려준다(위 splitCommandInternal 의 사연). 셸은 어느 쪽도 태우지 않는다.
    return " ".join(listOut), False


def collectTranslationUnits(buildDir: Path, pathFilter: str) -> list[dict]:
    """컴파일 DB 에서 우리 소스의 TU 만 고른다. PCH 더미와 생성 코드는 뺀다."""
    databasePath = buildDir / "compile_commands.json"
    if databasePath.exists() is False:
        return []

    entries = json.loads(databasePath.read_text(encoding="utf-8"))
    listEntry: list[dict] = []
    for entry in entries:
        filePath = entry["file"].replace("\\", "/")
        if filePath.endswith("cmake_pch.cxx") or filePath.endswith("cmake_pch.c"):
            continue
        # 생성 코드는 우리가 고칠 대상이 아니다(리플렉션 코드젠 산출물).
        if "/generated/" in filePath or filePath.endswith(".gen.cpp"):
            continue
        if pathFilter and pathFilter.lower() not in filePath.lower():
            continue
        listEntry.append(entry)
    return listEntry


def runOne(entry: dict) -> str:
    """TU 하나를 문법 검사한다. 진단은 stderr 로 나온다."""
    command, bNeedShell = buildSyntaxOnlyCommandInternal(entry)
    if not command:
        return ""
    try:
        completed = subprocess.run(command, cwd=entry.get("directory") or None, shell=bNeedShell,
                                   capture_output=True, text=True,
                                   encoding="utf-8", errors="replace", timeout=900)
    except subprocess.TimeoutExpired:
        return f"{entry['file']}: warning: [RunBuildWarnings] 시간 초과(900s) — 건너뜁니다 [-Wtimeout]\n"
    except OSError as exception:
        return f"{entry['file']}: error: [RunBuildWarnings] 실행 실패: {exception}\n"
    return (completed.stderr or "") + (completed.stdout or "")


def collectDiagnosticsInternal(rawText: str) -> list[str]:
    """진단 줄만 남긴다. 같은 헤더가 여러 TU 에서 중복되므로 유일화한다."""
    listLine: list[str] = []
    for line in rawText.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        matched = _kDiagnosticRe.match(stripped)
        if matched is None:
            continue
        # `note:` 와 소스 발췌는 버린다 — 자리와 종류만 있으면 다시 찾아갈 수 있다.
        listLine.append(stripped)
    return sorted(set(listLine))


def summarize(mapPresetToText: dict[str, str]) -> int:
    """구성별로 종류와 자리를 묶어 낸다. 돌려주는 값은 고유 경고 총합이다."""
    totalUnique = 0
    for presetName, rawText in mapPresetToText.items():
        listDiagnostic = collectDiagnosticsInternal(rawText)
        listWarning = [line for line in listDiagnostic if ": warning:" in line]
        listError = [line for line in listDiagnostic if ": error:" in line]
        totalUnique += len(listWarning)

        counter: Counter[str] = Counter()
        for line in listWarning:
            matched = _kFlagRe.search(line)
            counter[f"-W{matched.group('flag')}" if matched else "(분류 없음)"] += 1

        print("")
        print("=" * 78)
        print(f"  {presetName} — 고유 경고 {len(listWarning)}건" + (f", 오류 {len(listError)}건" if listError else ""))
        print("=" * 78)
        if not listWarning and not listError:
            print("  경고 없음.")
            continue

        for name, count in counter.most_common():
            print(f"  {count:5}  {name}")
        print("")
        print("-" * 78)
        for line in listError + listWarning:
            print(f"  {line}")

    return totalUnique


def main() -> int:
    useUtf8Stdout()
    projectRoot = getProjectRoot()

    parser = argparse.ArgumentParser(description="트리에 남아 있는 컴파일러 경고를 전부 보고합니다")
    parser.add_argument("--preset", action="append", default=None,
                        help=f"검사할 빌드 프리셋 (여러 번 줄 수 있습니다. 기본: {' '.join(_kDefaultPresets)})")
    parser.add_argument("--filter", default="", help="경로 부분 문자열로 TU 를 고릅니다 (예: Graphics)")
    parser.add_argument("--jobs", type=int, default=max(4, (os.cpu_count() or 8)), help="병렬 실행 수")
    parser.add_argument("--out", default="", help="원본 출력을 저장할 파일")
    args = parser.parse_args()

    listPreset = args.preset or list(_kDefaultPresets)
    mapPresetToText: dict[str, str] = {}
    listRawChunk: list[str] = []

    for presetName in listPreset:
        buildDir = projectRoot / "build" / presetName
        listEntry = collectTranslationUnits(buildDir, args.filter)
        if not listEntry:
            print(f"[RunBuildWarnings] {presetName}: compile_commands.json 이 없거나 대상 TU 가 없습니다 "
                  f"— `cmake --preset {presetName}` 으로 configure 하세요. 건너뜁니다.")
            continue

        print(f"[RunBuildWarnings] {presetName}: TU {len(listEntry)}개, 병렬 {args.jobs}")
        listOutput: list[str] = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = [pool.submit(runOne, entry) for entry in listEntry]
            for index, future in enumerate(concurrent.futures.as_completed(futures), start=1):
                listOutput.append(future.result())
                if index % 100 == 0:
                    print(f"  ... {index}/{len(listEntry)}")

        rawText = "".join(listOutput)
        mapPresetToText[presetName] = rawText
        listRawChunk.append(f"==== {presetName}\n{rawText}\n")

    if args.out:
        Path(args.out).write_text("".join(listRawChunk), encoding="utf-8")
        print(f"[RunBuildWarnings] 원본 출력 → {args.out}")

    totalUnique = summarize(mapPresetToText)

    print("")
    if totalUnique == 0:
        print("[RunBuildWarnings] 경고 0건 — 이 값이 정상입니다.")
    else:
        print(f"[RunBuildWarnings] 경고 {totalUnique}건. 새 경고는 분류해서 고치거나, 의도한 것이면 "
              f"그 자리에 이유를 주석으로 남기세요.")

    # **게이트가 아니다.** 경고가 있어도 0 을 돌려준다 — 구성·컴파일러 버전마다 집합이 달라서
    # 막으면 남의 PC 에서 빨개진다. 막는 일은 `Check*` 스크립트가 맡는다(RunClangTidy 와 같은 규칙).
    return 0


if __name__ == "__main__":
    sys.exit(main())
