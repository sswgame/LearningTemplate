#!/usr/bin/env python3
"""
@file RunTests.py
@brief 스위트 · 케이스 이름만으로 테스트를 돌린다 — 그 케이스가 사는 실행 파일을 찾아 올바른 폴더에서.

테스트 하나를 돌리려면 세 가지를 알아야 했다: 그 스위트가 **어느 실행 파일**에 있는지(CoreTest? EngineTest?),
작업 폴더가 **`Bin`** 이어야 한다는 것(아니면 `Resource/` 를 못 찾는다), 그리고 실행 파일은 **`TestBin`** 에
있다는 것. 셋 다 CLAUDE.md 의 함정 목록에 있다. 이 스크립트가 셋을 대신 안다.

돌리기 전에 테스트까지 짓는다(`cmake --build <폴더> --target all AllTests`) — 기본 빌드(`all`)는 테스트 실행 파일을 짓지 않고,
옛 실행 파일로 돌면 고친 것을 테스트하지 않은 채 통과한다. 이미 지은 트리면 ninja 가 할 일이 없어 몇 초다. `--no-build` 는 건너뛴다.

사용법:
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
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import ListChangedTests  # noqa: E402
from common import BuildTree, addBuildTreeArguments, getCpuCount, getProjectRoot, mapConcurrent, runGit, runProcess  # noqa: E402

#: `--test_list` 가 고른 케이스를 찍는 줄(`  Suite.Case`).
_kListedCaseRe = re.compile(r"^  ([A-Z]\w*Test)\.(\w+)\s*$")


def findTestExecutables(tree: BuildTree) -> tuple[Path, list[Path]]:
    """(작업 폴더 Bin, 테스트 실행 파일들). 실행 파일은 TestBin 에 있다."""
    for candidateDir in (tree.testBinDir, tree.binDir):
        listExecutable = sorted(path for path in candidateDir.glob("*Test*") if path.is_file() and path.suffix in ("", ".exe"))
        if listExecutable:
            return tree.binDir, listExecutable
    return tree.binDir, []


def listSelectedCases(executable: Path, workingDir: Path, pattern: str) -> list[str]:
    """그 실행 파일에서 패턴이 고르는 케이스 이름들(`Suite.Case`)."""
    result = runProcess([executable, "--test_list", f"--test_filter={pattern}"], cwd=workingDir)
    return [f"{match.group(1)}.{match.group(2)}" for line in result.stdout.splitlines() if (match := _kListedCaseRe.match(line))]


def listChangedFiles(root: Path, base: str | None) -> list[str] | None:
    """기준과 갈라진 곳부터 지금 작업 트리까지 바뀐 파일(커밋 · 스테이징 · 작업 중 · 추적 안 하는 새 파일). 기준을 못 찾으면 None.
    기준을 주지 않으면 main 이 아닌 브랜치는 `main`, main 은 `origin/main`(아직 내보내지 않은 것)이다."""
    if base is None:
        branch = runGit(["rev-parse", "--abbrev-ref", "HEAD"], cwd=root).stdout.strip()
        base = "origin/main" if branch == "main" else "main"
    mergeBase = runGit(["merge-base", base, "HEAD"], cwd=root)
    if not mergeBase.bSucceeded:
        return None
    setPath = set(runGit(["diff", "--name-only", mergeBase.stdout.strip()], cwd=root).stdout.splitlines())
    setPath |= set(runGit(["ls-files", "--others", "--exclude-standard"], cwd=root).stdout.splitlines())
    return sorted(path for path in setPath if path)


def selectCtestEntries(tree: BuildTree, selection: ListChangedTests.TestSelection, bGpu: bool) -> list[str]:
    """고른 실행 파일 · 라벨에 해당하는 ctest 항목 이름. 린트는 늘 든다."""
    listing = runProcess(["ctest", "--test-dir", str(tree.path), "-N", "--show-only=json-v1"])
    data = json.loads(listing.stdout) if listing.bSucceeded else {"tests": []}
    setLabelWanted = {"lint", "nogpu"} | ({"hostgpu"} if bGpu else set())
    listName: list[str] = []
    for test in data.get("tests", []):
        setLabel = {label for prop in test.get("properties", []) if prop.get("name") == "LABELS" for label in prop.get("value", [])}
        listCommand = test.get("command") or [""]
        executable = Path(listCommand[0]).stem
        bScript = len(listCommand) > 1 and Path(listCommand[1]).suffix == ".py"
        if "lint" in setLabel:
            listName.append(test["name"])
        elif not setLabel & setLabelWanted:
            continue
        elif selection.bFull:
            listName.append(test["name"])
        elif bScript:
            if selection.bPython and "python" in setLabel:
                listName.append(test["name"])
        elif executable in selection.setExecutable:
            listName.append(test["name"])
    return listName


def runChangedInternal(tree: BuildTree, args: argparse.Namespace, listExtra: list[str]) -> int:
    """`--changed` — 바뀐 파일이 닿는 테스트만. 실행 파일 단위는 ctest 로(조각 · 제한 시간 그대로), 스위트 단위는 실행 파일을 직접 돌린다."""
    root = getProjectRoot()
    listChanged = listChangedFiles(root, args.base)
    if listChanged is None:
        print(f"[RunTests] 기준 '{args.base}' 를 찾지 못했습니다", file=sys.stderr)
        return 2
    policy = json.loads((root / "Test" / "TestPolicy.json").read_text(encoding="utf-8"))
    setTestExecutable = set(policy.get("executables", {}))
    graph = ListChangedTests.readBuildGraph(tree.path, root)
    selection = ListChangedTests.selectTests(listChanged, root, setTestExecutable, graph)
    print(f"[RunTests] 바뀐 파일 {len(listChanged)} 개 → " + ("전체" if selection.bFull else
          f"실행 파일 {sorted(selection.setExecutable)} · 스위트 {dict((key, sorted(value)) for key, value in selection.mapExecutableSuite.items())}"
          f"{' · 파이썬' if selection.bPython else ''} (+ 린트)"))
    for reason in selection.listReason[:10]:
        print(f"    전체인 까닭: {reason}")

    listEntry = selectCtestEntries(tree, selection, args.bGpu)
    bFailed = False
    if listEntry:
        jobCount = args.jobs or getCpuCount()
        regex = "^(" + "|".join(re.escape(name) for name in listEntry) + ")$"
        command = ["ctest", "--test-dir", str(tree.path), "-R", regex, "-j", str(jobCount), "--output-on-failure", "-FS", "SWTestBinaries"]
        print(f"[RunTests] ctest 항목 {len(listEntry)} 개(-j {jobCount})", flush=True)
        bFailed = runProcess(command, bCapture=False).returnCode != 0

    workingDir, listExecutable = findTestExecutables(tree)
    mapPath = {path.stem: path for path in listExecutable}
    for executable, setSuite in sorted(selection.mapExecutableSuite.items()):
        if executable not in mapPath:
            continue
        hostArgument = "--host_suites=all" if args.bGpu else "--host_suites=exclude"
        command = [str(mapPath[executable]), "--test_filter=" + ":".join(f"{suite}.*" for suite in sorted(setSuite)), hostArgument,
                   "--allow_empty_suite", *listExtra]
        print(f"\n[RunTests] ({workingDir}) {' '.join(command)}", flush=True)
        bFailed = runProcess(command, cwd=workingDir, bCapture=False).returnCode != 0 or bFailed
    print("\n[RunTests] " + ("실패가 있습니다" if bFailed else "모두 통과"))
    return 1 if bFailed else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="스위트 · 케이스 이름으로 시험을 그 케이스가 사는 실행 파일 · 폴더에서 돌린다")
    parser.add_argument("pattern", nargs="?", help="--test_filter 패턴(예: SceneTest.* · \"A.*,B.Case\" · \"*\")")
    parser.add_argument("--changed", action="store_true", help="패턴 대신 바뀐 파일이 닿는 테스트만 돌린다(모르면 전체)")
    parser.add_argument("--base", default=None, help="--changed 의 기준(기본: main 브랜치면 origin/main, 아니면 main)")
    parser.add_argument("--gpu", dest="bGpu", action="store_true", help="--changed 에 hostgpu 항목 · 호스트 스위트까지 넣는다")
    parser.add_argument("-j", "--jobs", type=int, default=0, help="--changed 의 ctest 동시 실행 수(기본: 코어 수)")
    addBuildTreeArguments(parser)
    parser.add_argument("--repeat", type=int, default=0, help="고른 케이스를 N 번 되풀이한다(--test_repeat)")
    parser.add_argument("--shuffle", nargs="?", const="random", default=None, help="순서를 섞는다 — 씨앗을 주면 그 순서를 다시 만든다")
    parser.add_argument("--list", action="store_true", help="돌리지 않고 어느 실행 파일에 어떤 케이스가 있는지만 찍는다")
    parser.add_argument("--no-build", dest="bBuild", action="store_false", help="시험 빌드(all · AllTests)를 건너뛰고 있는 실행 파일로 돌린다")
    # 모르는 인자는 실행 파일 몫이다. 위치 인자 REMAINDER 로 받으면 패턴 뒤의 --list · --repeat 까지 삼킨다.
    args, listPassthrough = parser.parse_known_args(argv)
    if bool(args.pattern) == args.changed:
        parser.error("패턴 하나 또는 --changed 하나를 준다")

    tree = BuildTree.fromArguments(args)
    if args.bBuild:
        if not tree.bConfigured:
            print(f"[RunTests] {tree.path} 가 구성되지 않았습니다 — 먼저 cmake --preset {tree.name}", file=sys.stderr)
            return 2
        buildCommand = tree.buildCommand()
        print(f"[RunTests] {' '.join(buildCommand)}", flush=True)
        buildResult = runProcess(buildCommand, bCapture=False)
        if not buildResult.bSucceeded:
            print("[RunTests] 시험 빌드가 졌습니다" if buildResult.bLaunched else "[RunTests] cmake 를 찾지 못했습니다 (PATH)", file=sys.stderr)
            return 2
    if args.changed:
        return runChangedInternal(tree, args, [argument for argument in listPassthrough if argument != "--"])
    workingDir, listExecutable = findTestExecutables(tree)
    if not listExecutable:
        print(f"[RunTests] {tree.path} 에 테스트 실행 파일이 없습니다 — 먼저 빌드하세요 (cmake --build --preset {tree.name} --target all AllTests)", file=sys.stderr)
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
        if runProcess(command, cwd=workingDir, bCapture=False).returnCode != 0:
            listFailed.append(executable.name)

    print()
    if listFailed:
        print(f"[RunTests] 실패: {', '.join(listFailed)}")
        return 1
    print(f"[RunTests] 모두 통과 ({len(listSelection)} 실행 파일)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
