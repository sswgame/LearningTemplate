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
from common import ChangedTests  # noqa: E402
from common.LogDigest import runDigested  # noqa: E402
from common.BuildTree import addBuildTreeArguments, BuildTree  # noqa: E402
from common.Host import runGit  # noqa: E402
from common.Parallel import getCpuCount, mapConcurrent  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.Process import ProcessResult, runProcess  # noqa: E402

#: `--test_list` 가 고른 케이스를 찍는 줄(`  Suite.Case`).
_kListedCaseRe = re.compile(r"^  ([A-Z]\w*Test)\.(\w+)\s*$")


class RunTestsError(RuntimeError):
    """부른 프로세스(실행 파일의 `--test_list` · git)가 졌다 — 결과를 빈 목록으로 읽지 않고 멈춘다(`main` 이 찍고 2 를 돌려준다)."""


def requireSucceededInternal(result: ProcessResult, what: str) -> ProcessResult:
    """result 가 성공이면 그대로, 아니면 출력 끝을 담아 RunTestsError."""
    if not result.bSucceeded:
        raise RunTestsError(f"{what} 가 졌다(종료 코드 {result.returnCode}):\n{result.output[-2000:]}")
    return result


def findTestExecutables(tree: BuildTree) -> tuple[Path, list[Path]]:
    """(작업 폴더 Bin, 테스트 실행 파일들). 실행 파일은 TestBin 에 있다."""
    for candidateDir in (tree.testBinDir, tree.binDir):
        listExecutable = sorted(path for path in candidateDir.glob("*Test*") if path.is_file() and path.suffix in ("", ".exe"))
        if listExecutable:
            return tree.binDir, listExecutable
    return tree.binDir, []


def listSelectedCases(executable: Path, workingDir: Path, pattern: str) -> list[str]:
    """그 실행 파일에서 패턴이 고르는 케이스 이름들(`Suite.Case`). `--test_list` 는 고른 것이 없어도 0 이라, 지면 진짜 실패다(기동 실패 · 크래시)."""
    result = requireSucceededInternal(runProcess([executable, "--test_list", f"--test_filter={pattern}"], cwd=workingDir),
                                      f"`{executable.name} --test_list`")
    return [f"{match.group(1)}.{match.group(2)}" for line in result.stdout.splitlines() if (match := _kListedCaseRe.match(line))]


def listChangedFiles(root: Path, base: str | None) -> list[str] | None:
    """기준과 갈라진 곳부터 지금 작업 트리까지 바뀐 파일(커밋 · 스테이징 · 작업 중 · 추적 안 하는 새 파일). 기준을 못 찾으면 None.
    기준을 주지 않으면 main 이 아닌 브랜치는 `main`, main 은 `origin/main`(아직 내보내지 않은 것)이다."""
    if base is None:
        branch = requireSucceededInternal(runGit(["rev-parse", "--abbrev-ref", "HEAD"], cwd=root), "git rev-parse").stdout.strip()
        base = "origin/main" if branch == "main" else "main"
    mergeBase = runGit(["merge-base", base, "HEAD"], cwd=root)
    if not mergeBase.bSucceeded:
        return None
    setPath = set(requireSucceededInternal(runGit(["diff", "--name-only", mergeBase.stdout.strip()], cwd=root), "git diff").stdout.splitlines())
    setPath |= set(requireSucceededInternal(runGit(["ls-files", "--others", "--exclude-standard"], cwd=root), "git ls-files").stdout.splitlines())
    return sorted(path for path in setPath if path)


def selectCtestEntries(tree: BuildTree, selection: ChangedTests.TestSelection, bGpu: bool) -> list[str]:
    """고른 실행 파일 · 라벨에 해당하는 ctest 항목 이름. 린트는 늘 든다."""
    listing = runProcess(["ctest", "--test-dir", str(tree.path), "-N", "--show-only=json-v1"])
    if not listing.bSucceeded:
        raise ChangedTests.ChangedTestsError([f"`ctest --test-dir {tree.path} -N` 가 졌다 — 빌드 폴더를 구성하고 다시:\n{listing.output[-2000:]}"])
    data = json.loads(listing.stdout)
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
    setTestExecutable = set(policy["executables"])
    # 해석하지 못하면 전체 · 일부로 물러서지 않고 까닭을 보이며 멈춘다(common/ChangedTests.py 머리말).
    try:
        bNeedsGraph = any(ChangedTests.classifyPath(path) == "source" for path in listChanged)
        graph = ChangedTests.readBuildGraph(tree.path, root) if bNeedsGraph else None
        selection = ChangedTests.selectTests(listChanged, root, setTestExecutable, graph)
        listEntry = selectCtestEntries(tree, selection, args.bGpu)
    except ChangedTests.ChangedTestsError as error:
        print(f"[RunTests] 바뀐 파일 {len(listChanged)} 개를 테스트로 옮기지 못했습니다:", file=sys.stderr)
        for reason in error.listReason:
            print(f"    {reason}", file=sys.stderr)
        return 2
    print(f"[RunTests] 바뀐 파일 {len(listChanged)} 개 → " + ("전체" if selection.bFull else
          f"실행 파일 {sorted(selection.setExecutable)} · 스위트 {dict((key, sorted(value)) for key, value in selection.mapExecutableSuite.items())}"
          f"{' · 파이썬' if selection.bPython else ''} (+ 린트)"))
    for reason in selection.listReason[:10]:
        print(f"    전체인 까닭: {reason}")

    bFailed = False
    if listEntry:
        # `-j` 를 주지 않으면(0) 이 PC 의 스레드 수다 — 값이 없을 때의 대체가 아니라 옵션의 뜻이다(도우미는 -j 2 를 준다).
        jobCount = args.jobs or getCpuCount()
        regex = "^(" + "|".join(re.escape(name) for name in listEntry) + ")$"
        command = ["ctest", "--test-dir", str(tree.path), "-R", regex, "-j", str(jobCount), "--output-on-failure", "-FS", "SWTestBinaries"]
        print(f"[RunTests] ctest 항목 {len(listEntry)} 개(-j {jobCount})", flush=True)
        bFailed = runDigested(command, root, "ctest", bVerbose=args.bVerbose).returnCode != 0

    workingDir, listExecutable = findTestExecutables(tree)
    mapPath = {path.stem: path for path in listExecutable}
    for executable, setSuite in sorted(selection.mapExecutableSuite.items()):
        if executable not in mapPath:
            print(f"[RunTests] 고른 실행 파일 {executable} 이 {tree.path} 에 없습니다(이 구성에서 짓지 않는 실행 파일인지 본다)", file=sys.stderr)
            bFailed = True
            continue
        hostArgument = "--host_suites=all" if args.bGpu else "--host_suites=exclude"
        command = [str(mapPath[executable]), "--test_filter=" + ":".join(f"{suite}.*" for suite in sorted(setSuite)), hostArgument,
                   "--allow_empty_suite", *listExtra]
        print(f"\n[RunTests] ({workingDir}) {' '.join(command)}", flush=True)
        bFailed = runDigested(command, root, executable, bVerbose=args.bVerbose, cwd=workingDir).returnCode != 0 or bFailed
    print("\n[RunTests] " + ("실패가 있습니다" if bFailed else "모두 통과"))
    return 1 if bFailed else 0


def runPatternInternal(tree: BuildTree, args: argparse.Namespace, listPassthrough: list[str]) -> int:
    """패턴 — 그 케이스가 사는 실행 파일마다 직접 돌린다."""
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
        if runDigested(command, getProjectRoot(), executable.stem, bVerbose=args.bVerbose, cwd=workingDir).returnCode != 0:
            listFailed.append(executable.name)

    print()
    if listFailed:
        print(f"[RunTests] 실패: {', '.join(listFailed)}")
        return 1
    print(f"[RunTests] 모두 통과 ({len(listSelection)} 실행 파일)")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="스위트 · 케이스 이름으로 테스트를 그 케이스가 사는 실행 파일 · 폴더에서 돌린다")
    parser.add_argument("pattern", nargs="?", help="--test_filter 패턴(예: SceneTest.* · \"A.*,B.Case\" · \"*\")")
    parser.add_argument("--changed", action="store_true", help="패턴 대신 바뀐 파일이 닿는 테스트만 돌린다(모르면 전체)")
    parser.add_argument("--base", default=None, help="--changed 의 기준(기본: main 브랜치면 origin/main, 아니면 main)")
    parser.add_argument("--gpu", dest="bGpu", action="store_true", help="--changed 에 hostgpu 항목 · 호스트 스위트까지 넣는다")
    parser.add_argument("-j", "--jobs", type=int, default=0, help="--changed 의 ctest 동시 실행 수(기본: 코어 수)")
    addBuildTreeArguments(parser)
    parser.add_argument("--repeat", type=int, default=0, help="고른 케이스를 N 번 되풀이한다(--test_repeat)")
    parser.add_argument("--shuffle", nargs="?", const="random", default=None, help="순서를 섞는다 — 씨앗을 주면 그 순서를 다시 만든다")
    parser.add_argument("--list", action="store_true", help="돌리지 않고 어느 실행 파일에 어떤 케이스가 있는지만 찍는다")
    parser.add_argument("--verbose", dest="bVerbose", action="store_true", help="빌드 · 테스트 출력을 요약하지 않고 전부 흘린다(기본: 로그 파일 + 요약)")
    parser.add_argument("--no-build", dest="bBuild", action="store_false", help="테스트 빌드(all · AllTests)를 건너뛰고 있는 실행 파일로 돌린다")
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
        buildResult = runDigested(buildCommand, getProjectRoot(), "test-build", bVerbose=args.bVerbose)
        if not buildResult.bSucceeded:
            print("[RunTests] 테스트 빌드가 졌습니다" if buildResult.bLaunched else "[RunTests] cmake 를 찾지 못했습니다 (PATH)", file=sys.stderr)
            return 2
    try:
        if args.changed:
            return runChangedInternal(tree, args, [argument for argument in listPassthrough if argument != "--"])
        return runPatternInternal(tree, args, listPassthrough)
    except RunTestsError as error:
        print(f"[RunTests] {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
