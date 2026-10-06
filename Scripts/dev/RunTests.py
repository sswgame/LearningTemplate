#!/usr/bin/env python3
"""
@file RunTests.py
@brief 스위트 · 케이스 이름만으로 테스트를 돌린다 — 그 케이스가 사는 실행 파일을 찾아 올바른 폴더에서.

테스트 하나를 돌리려면 세 가지를 알아야 했다: 그 스위트가 **어느 실행 파일**에 있는지(CoreTest? EngineTest?),
작업 폴더가 **`Bin`** 이어야 한다는 것(아니면 `Resource/` 를 못 찾는다), 그리고 Shipping 은 실행 파일이 **`TestBin`** 에
있다는 것. 셋 다 CLAUDE.md 의 함정 목록에 있다. 이 스크립트가 셋을 대신 안다.

사용법 (빌드 후):
    py -3 Scripts/dev/RunTests.py SceneTest.*                         # Debug, 그 스위트가 사는 실행 파일에서
    py -3 Scripts/dev/RunTests.py "SceneTest.*,ResourceTest.Ensure*"   # 여러 패턴 · 여러 실행 파일
    py -3 Scripts/dev/RunTests.py ResourceTest.* --preset Ninja-Shipping
    py -3 Scripts/dev/RunTests.py ProcessTest.* --repeat 20             # 간헐 실패 찾기(--test_repeat)
    py -3 Scripts/dev/RunTests.py "*" --shuffle                         # 순서에 기대는 테스트 찾기(--test_shuffle)
    py -3 Scripts/dev/RunTests.py Scene* --list                        # 어느 실행 파일에 어떤 케이스가 있나

패턴은 실행 파일의 `--test_filter` 와 같다(쉼표로 여럿, 앞에 `-` 면 뺀다). 이 스크립트가 모르는 인자(`--test_brief` 처럼)는
실행 파일에 그대로 간다.
종료 코드는 돌린 실행 파일 중 하나라도 지면 1 이다.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import getProjectRoot, mapConcurrent  # noqa: E402

#: `--test_list` 가 고른 케이스를 찍는 줄(`  Suite.Case`).
_kListedCaseRe = re.compile(r"^  ([A-Z]\w*Test)\.(\w+)\s*$")


def findTestExecutables(buildDir: Path) -> tuple[Path, list[Path]]:
    """(작업 폴더 Bin, 테스트 실행 파일들). Shipping 은 실행 파일이 TestBin 에 있다."""
    binDir = buildDir / "Bin"
    for candidateDir in (buildDir / "TestBin", binDir):
        listExecutable = sorted(path for path in candidateDir.glob("*Test*") if path.is_file() and path.suffix in ("", ".exe"))
        if listExecutable:
            return binDir, listExecutable
    return binDir, []


def listSelectedCases(executable: Path, workingDir: Path, pattern: str) -> list[str]:
    """그 실행 파일에서 패턴이 고르는 케이스 이름들(`Suite.Case`)."""
    result = subprocess.run([str(executable), "--test_list", f"--test_filter={pattern}"], cwd=workingDir,
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    return [f"{match.group(1)}.{match.group(2)}" for line in result.stdout.splitlines() if (match := _kListedCaseRe.match(line))]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run tests by suite/case name in the right executable and folder.")
    parser.add_argument("pattern", help="--test_filter 패턴(예: SceneTest.* · \"A.*,B.Case\" · \"*\")")
    parser.add_argument("--preset", default="Ninja-Debug", help="빌드 프리셋 폴더 이름 (기본 Ninja-Debug)")
    parser.add_argument("--repeat", type=int, default=0, help="고른 케이스를 N 번 되풀이한다(--test_repeat)")
    parser.add_argument("--shuffle", nargs="?", const="random", default=None, help="순서를 섞는다 — 씨앗을 주면 그 순서를 다시 만든다")
    parser.add_argument("--list", action="store_true", help="돌리지 않고 어느 실행 파일에 어떤 케이스가 있는지만 찍는다")
    # 모르는 인자는 실행 파일 몫이다. 위치 인자 REMAINDER 로 받으면 패턴 뒤의 --list · --repeat 까지 삼킨다.
    args, listPassthrough = parser.parse_known_args(argv)

    buildDir = getProjectRoot() / "build" / args.preset
    workingDir, listExecutable = findTestExecutables(buildDir)
    if not listExecutable:
        print(f"[RunTests] {buildDir} 에 테스트 실행 파일이 없습니다 — 먼저 빌드하세요 (cmake --build --preset {args.preset})", file=sys.stderr)
        return 2

    # 실행 파일마다 `--test_list` 로 묻는다 — 엔진 서비스를 세우느라 하나에 수백 ms 라 동시에.
    listSelection = list(mapConcurrent(lambda executable: (executable, listSelectedCases(executable, workingDir, args.pattern)),
                                       listExecutable, workerCount=len(listExecutable)))
    listSelection = sorted((item for item in listSelection if item[1]), key=lambda item: item[0].name)
    if not listSelection:
        print(f"[RunTests] '{args.pattern}' 가 고르는 케이스가 어느 실행 파일에도 없습니다 ({len(listExecutable)} 개를 물었다)", file=sys.stderr)
        return 1

    for executable, listCase in listSelection:
        print(f"[RunTests] {executable.name}: {len(listCase)} cases")
        if args.list:
            for caseName in listCase:
                print(f"    {caseName}")
    if args.list:
        return 0

    listExtra = [argument for argument in listPassthrough if argument != "--"]
    if args.repeat > 1:
        listExtra.append(f"--test_repeat={args.repeat}")
    if args.shuffle is not None:
        listExtra.append("--test_shuffle" if args.shuffle == "random" else f"--test_shuffle={args.shuffle}")

    listFailed: list[str] = []
    for executable, _ in listSelection:
        command = [str(executable), f"--test_filter={args.pattern}", *listExtra]
        print(f"\n[RunTests] ({workingDir}) {' '.join(command)}", flush=True)
        if subprocess.run(command, cwd=workingDir).returncode != 0:
            listFailed.append(executable.name)

    print()
    if listFailed:
        print(f"[RunTests] 실패: {', '.join(listFailed)}")
        return 1
    print(f"[RunTests] 모두 통과 ({len(listSelection)} 실행 파일)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
