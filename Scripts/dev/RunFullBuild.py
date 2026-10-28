#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunFullBuild.py
@brief 병합 전 검증 빌드 — 빌드 칸을 잡고 `all AllTests` 를 짓고, 성공하면 그 커밋 트리의 증거를 남깁니다.

    py -3 -m Scripts full-build --owner <이름>                         # build/Ninja-Debug, 빌드 칸(공유 2)을 잡고 짓는다
    py -3 -m Scripts full-build --owner <이름> --below-normal -j 4     # 도우미 — 낮은 우선순위

**왜 전체인가.** 브랜치 검증이 바꾼 영역의 테스트만 지으면, 다른 영역의 테스트가 쓰는 헤더 · 매크로를 바꾼 것이 병합 뒤에야 깨진다
(2026-10-17 integ5). 검증 빌드는 늘 `kTestBuildTargets`(`all AllTests`)다 — 타깃을 고르는 인자를 두지 않는다.

**증거.** 커밋하지 않은 변경이 없을 때만 짓고(증거는 커밋 트리에 붙는다), 빌드가 성공하고 그동안 HEAD 트리가 그대로면
`<공통 .git>/sw-build-evidence/<빌드 입력 해시>.json` 을 쓴다. 키는 트리에서 빌드 · 테스트가 읽지 않는 파일(`isBuildInputPath`: 마크다운과 `.claude/`)을
뺀 **빌드 입력 해시**라서, 문서만 바꾼 커밋은 직전 증거가 그대로 맞고 다시 짓지 않는다. 같은 입력이면 어느 워크트리 · 어느 커밋에서 지었든 같다
(통합 워크트리에서 지은 병합 결과를 main 체크아웃에서 push 해도 맞는다). 공통 .git 은 모든 워크트리가 나눠 쓰고 추적되지 않는다.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.BuildEvidence import (  # noqa: E402
    getEvidenceDirectory, listUncommittedFiles, readBuildInputHash, readTreeHash, writeEvidence)
from common.BuildTree import BuildTree, kTestBuildTargets  # noqa: E402
from common.Host import runGit  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.BuildTree import kDefaultPreset  # noqa: E402

from common.LogDigest import getLogPath, printDigest  # noqa: E402
from common.SharedLock import getSharedSlotCount, kDefaultWaitSeconds, runHolding  # noqa: E402
from common.Worktree import findMainRoot  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402

#: 검증 빌드가 잡는 잠금 자원(공유 칸 — 동시 빌드 한도).
kBuildLockResource = "build"


def runFullBuild(repositoryRoot: Path, args: argparse.Namespace) -> int:
    """커밋 트리를 전체 빌드하고, 성공하면 증거를 씁니다."""
    listDirty = listUncommittedFiles(repositoryRoot)
    if listDirty:
        print(f"[FullBuild] 커밋하지 않은 변경이 있어 짓지 않습니다 — 증거는 커밋 트리에 붙는다: {', '.join(listDirty[:10])}", file=sys.stderr)
        return ExitCode.CheckInvalid
    treeHash = readTreeHash(repositoryRoot, "HEAD")
    inputHash = readBuildInputHash(repositoryRoot, "HEAD")
    tree = BuildTree.fromPreset(args.preset, repositoryRoot)
    if not tree.bConfigured:
        print(f"[FullBuild] 구성되지 않은 빌드 폴더입니다: {tree.path} — `cmake --preset {args.preset}` 를 먼저(vcpkg 잠금을 잡고) 돌립니다",
              file=sys.stderr)
        return ExitCode.CheckInvalid
    lockArgs = argparse.Namespace(resource=kBuildLockResource, owner=args.owner, shared=True, slots=getSharedSlotCount(kBuildLockResource),
                                  purpose=f"full-build {args.preset} {inputHash[:12]}", wait=args.wait, belowNormal=args.belowNormal)
    startTime = time.time()
    # 출력은 로그 파일로 가고 끝나서 요약만 보인다(`common/LogDigest.py`) — `--verbose` 면 지금까지처럼 흘린다. 잠금 동작은 그대로다.
    logPath = None if args.bVerbose else getLogPath(repositoryRoot, f"full-build-{args.preset}")
    returnCode = runHolding(findMainRoot(repositoryRoot), lockArgs, tree.buildCommand(kTestBuildTargets, args.jobs), logPath)
    if logPath is not None and logPath.is_file():
        printDigest(logPath, returnCode, time.time() - startTime)
    if returnCode != 0:
        print(f"[FullBuild] 빌드가 실패했습니다(종료 {returnCode}) — 증거를 남기지 않습니다.", file=sys.stderr)
        return returnCode
    if listUncommittedFiles(repositoryRoot) or readTreeHash(repositoryRoot, "HEAD") != treeHash:
        print("[FullBuild] 빌드하는 동안 트리가 바뀌었습니다 — 지은 것이 지금 커밋과 같다고 말할 수 없어 증거를 남기지 않습니다.", file=sys.stderr)
        return ExitCode.CheckInvalid
    record = {"inputs": inputHash, "tree": treeHash, "commit": runGit(["rev-parse", "HEAD"], cwd=repositoryRoot).stdout.strip(), "preset": args.preset,
              "targets": list(kTestBuildTargets), "worktree": str(repositoryRoot), "owner": args.owner,
              "startTime": startTime, "endTime": time.time()}
    path = writeEvidence(getEvidenceDirectory(repositoryRoot), inputHash, record)
    print(f"[FullBuild] 전체 빌드 성공 — 증거 {path}")
    return ExitCode.Passed


def main(argv: list[str] | None = None) -> int:
    listArgument = list(sys.argv[1:] if argv is None else argv)
    repositoryRoot = getProjectRoot()
    try:
        parser = argparse.ArgumentParser(description="빌드 칸을 잡고 all AllTests 전체 빌드 — 성공하면 그 커밋 트리의 증거를 남긴다")
        parser.add_argument("--owner", required=True, help="소유자 이름(에이전트 · 워크트리 이름) — 빌드 칸 잠금에 적힌다")
        parser.add_argument("--preset", default=kDefaultPreset, help=f"빌드 폴더 build/<프리셋>(기본 {kDefaultPreset})")
        parser.add_argument("-j", "--jobs", type=int, default=None, help="동시 작업 수(기본: 빌드 도구가 정한다)")
        parser.add_argument("--below-normal", dest="belowNormal", action="store_true", help="낮은 우선순위로 짓는다(도우미 — 작업 방식 2 절)")
        parser.add_argument("--wait", type=int, default=kDefaultWaitSeconds, help=f"빌드 칸을 기다릴 최대 초(기본 {kDefaultWaitSeconds})")
        parser.add_argument("--verbose", dest="bVerbose", action="store_true", help="빌드 출력을 요약하지 않고 전부 흘린다(기본: 로그 파일 + 요약)")
        args = parser.parse_args(listArgument)
        return runFullBuild(repositoryRoot, args)
    except RuntimeError as error:
        print(f"[FullBuild] {error}", file=sys.stderr)
        return ExitCode.CheckInvalid


if __name__ == "__main__":
    sys.exit(main())
