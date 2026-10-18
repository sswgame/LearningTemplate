#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunFullBuild.py
@brief 병합 전 검증 빌드 — 빌드 칸을 잡고 `all AllTests` 를 짓고, 성공하면 그 커밋 트리의 증거를 남깁니다. main 으로의 push 는 증거를 요구합니다.

    py -3 -m Scripts full-build --owner <이름>                         # build/Ninja-Debug, 빌드 칸(공유 2)을 잡고 짓는다
    py -3 -m Scripts full-build --owner <이름> --below-normal -j 4     # 도우미 — 낮은 우선순위
    py -3 -m Scripts full-build check-push < <pre-push 입력>           # pre-push 훅이 부른다 — main 으로 가는 커밋의 트리에 증거가 있는가

**왜 전체인가.** 브랜치 검증이 바꾼 영역의 테스트만 지으면, 다른 영역의 테스트가 쓰는 헤더 · 매크로를 바꾼 것이 병합 뒤에야 깨진다
(2026-10-17 integ5). 검증 빌드는 늘 `kTestBuildTargets`(`all AllTests`)다 — 타깃을 고르는 인자를 두지 않는다.

**증거.** 커밋하지 않은 변경이 없을 때만 짓고(증거는 커밋 트리에 붙는다), 빌드가 성공하고 그동안 HEAD 트리가 그대로면
`<공통 .git>/sw-build-evidence/<빌드 입력 해시>.json` 을 쓴다. 키는 트리에서 빌드 · 테스트가 읽지 않는 파일(`isBuildInputPath`: 마크다운과 `.claude/`)을
뺀 **빌드 입력 해시**라서, 문서만 바꾼 커밋은 직전 증거가 그대로 맞고 다시 짓지 않는다. 같은 입력이면 어느 워크트리 · 어느 커밋에서 지었든 같다
(통합 워크트리에서 지은 병합 결과를 main 체크아웃에서 push 해도 맞는다). 공통 .git 은 모든 워크트리가 나눠 쓰고 추적되지 않는다.

**막는 자리.** `Scripts/setup/InstallGitHooks.py` 가 설치하는 pre-push 훅이 `check-push` 를 부른다. `refs/heads/main` 으로 가는 커밋의
트리에 증거가 없으면 push 를 막는다. 브랜치 push 는 막지 않는다(도우미는 push 하지 않는다 — 작업 방식 4 절).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import time
from pathlib import Path
from typing import Callable

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import BuildTree, getProjectRoot, kTestBuildTargets, runGit  # noqa: E402
from common.BuildTree import kDefaultPreset  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent))
from MakeWorktree import findMainRoot  # noqa: E402
from RunSharedLock import getSharedSlotCount, kDefaultWaitSeconds, runHolding  # noqa: E402

#: 공통 .git 아래 증거 폴더.
kEvidenceDirName = "sw-build-evidence"
#: 검증 빌드가 잡는 잠금 자원(공유 칸 — 동시 빌드 한도).
kBuildLockResource = "build"
#: 증거를 요구하는 원격 브랜치.
kProtectedRemoteRefs: tuple[str, ...] = ("refs/heads/main",)
#: git 이 "없음" 을 적는 해시(지우는 push 의 로컬 쪽).
kZeroHash = "0" * 40
#: 빌드 · 테스트가 읽지 않아 증거 키에서 빼는 최상위 폴더.
kNonBuildInputFolders: tuple[str, ...] = (".claude",)
#: 그 아래(와 저장소 최상단)의 마크다운은 빌드 입력이 아니다. 다른 폴더의 .md 는 빌드가 읽을 수 있어 입력으로 둔다
#: (`docs/Config/ConfigReference.json` 처럼 docs 아래 데이터는 테스트가 읽으므로 .md 만 뺀다).
kNonBuildInputMarkdownFolders: tuple[str, ...] = ("docs", "Scripts", "cmake")


def getEvidenceDirectory(repositoryRoot: Path) -> Path:
    """증거 폴더 — 모든 워크트리가 나눠 쓰는 공통 .git 아래."""
    result = runGit(["rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=repositoryRoot)
    if not result.bSucceeded:
        raise RuntimeError(f"git 저장소가 아닙니다: {repositoryRoot}\n{result.stderr.strip()}")
    return Path(result.stdout.strip()) / kEvidenceDirName


def readTreeHash(repositoryRoot: Path, revision: str) -> str:
    """커밋의 트리 해시입니다."""
    result = runGit(["rev-parse", f"{revision}^{{tree}}"], cwd=repositoryRoot)
    if not result.bSucceeded:
        raise RuntimeError(f"{revision} 의 트리를 읽지 못했습니다: {result.stderr.strip()}")
    return result.stdout.strip()


def isBuildInputPath(path: str) -> bool:
    """저장소 기준 경로가 빌드 · 테스트의 입력인가. 마크다운(최상단 · docs · Scripts · cmake 아래)과 `.claude/` 는 아니다."""
    listPart = path.split("/")
    if listPart[0] in kNonBuildInputFolders:
        return False
    if path.endswith(".md") and (len(listPart) == 1 or listPart[0] in kNonBuildInputMarkdownFolders):
        return False
    return True


def hashBuildInputs(lsTreeOutput: str) -> str:
    """`git ls-tree -r -z` 출력에서 빌드 입력 항목만 모아 해시한다. 항목은 `<모드> <종류> <해시>\t<경로>` 라 파일 내용이 해시에 들어간다."""
    listKept = [entry for entry in lsTreeOutput.split("\0") if entry and isBuildInputPath(entry.partition("\t")[2])]
    return hashlib.sha1("\0".join(listKept).encode("utf-8")).hexdigest()


def readBuildInputHash(repositoryRoot: Path, revision: str) -> str:
    """커밋의 빌드 입력 해시 — 증거 파일의 키입니다."""
    result = runGit(["ls-tree", "-r", "-z", revision], cwd=repositoryRoot)
    if not result.bSucceeded:
        raise RuntimeError(f"{revision} 의 파일 목록을 읽지 못했습니다: {result.stderr.strip()}")
    return hashBuildInputs(result.stdout)


def listUncommittedFiles(repositoryRoot: Path) -> list[str]:
    """추적하는 파일 가운데 커밋하지 않은 변경(staged 포함). 추적하지 않는 파일은 빌드 입력이 아니라 세지 않는다."""
    result = runGit(["status", "--porcelain", "--untracked-files=no"], cwd=repositoryRoot)
    if not result.bSucceeded:
        raise RuntimeError(f"git status 가 실패했습니다: {result.stderr.strip()}")
    return [line[3:] for line in result.stdout.splitlines() if line.strip()]


def getEvidencePath(evidenceDirectory: Path, inputHash: str) -> Path:
    return evidenceDirectory / f"{inputHash}.json"


def writeEvidence(evidenceDirectory: Path, inputHash: str, record: dict) -> Path:
    """빌드 입력 하나의 증거를 씁니다."""
    evidenceDirectory.mkdir(parents=True, exist_ok=True)
    path = getEvidencePath(evidenceDirectory, inputHash)
    path.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    return path


def listPushedTreesNeedingEvidence(pushInput: str, readTree: Callable[[str], str]) -> list[tuple[str, str]]:
    """
    pre-push 입력(`<로컬 ref> <로컬 해시> <원격 ref> <원격 해시>` 줄들)에서 증거가 필요한 (원격 ref, 증거 키).
    보호한 원격 ref 로 가는 줄만, 지우는 push(로컬 해시 0)는 뺀다. `readTree` 는 커밋 해시 → 증거 키(빌드 입력 해시).
    """
    listNeeded: list[tuple[str, str]] = []
    for line in pushInput.splitlines():
        parts = line.split()
        if len(parts) != 4:
            continue
        _, localHash, remoteRef, _ = parts
        if remoteRef not in kProtectedRemoteRefs or localHash == kZeroHash:
            continue
        listNeeded.append((remoteRef, readTree(localHash)))
    return listNeeded


def checkPush(repositoryRoot: Path, pushInput: str) -> int:
    """pre-push — main 으로 가는 커밋의 빌드 입력에 전체 빌드 증거가 없으면 1."""
    evidenceDirectory = getEvidenceDirectory(repositoryRoot)
    listMissing = [(ref, tree) for ref, tree in listPushedTreesNeedingEvidence(pushInput, lambda commit: readBuildInputHash(repositoryRoot, commit))
                   if not getEvidencePath(evidenceDirectory, tree).is_file()]
    for ref, tree in listMissing:
        print(f"[FullBuild] {ref} 로 가는 빌드 입력 {tree[:12]} 에 전체 빌드(all AllTests) 증거가 없습니다(마크다운 · .claude 만 바뀐 커밋은 직전 증거를 쓴다) — "
              f"그 커밋에서 `py -3 -m Scripts full-build --owner <이름>` 을 돌린 뒤 push 합니다.", file=sys.stderr)
    return 1 if listMissing else 0


def runFullBuild(repositoryRoot: Path, args: argparse.Namespace) -> int:
    """커밋 트리를 전체 빌드하고, 성공하면 증거를 씁니다."""
    listDirty = listUncommittedFiles(repositoryRoot)
    if listDirty:
        print(f"[FullBuild] 커밋하지 않은 변경이 있어 짓지 않습니다 — 증거는 커밋 트리에 붙는다: {', '.join(listDirty[:10])}", file=sys.stderr)
        return 2
    treeHash = readTreeHash(repositoryRoot, "HEAD")
    inputHash = readBuildInputHash(repositoryRoot, "HEAD")
    tree = BuildTree.fromPreset(args.preset, repositoryRoot)
    if not tree.bConfigured:
        print(f"[FullBuild] 구성되지 않은 빌드 폴더입니다: {tree.path} — `cmake --preset {args.preset}` 를 먼저(vcpkg 잠금을 잡고) 돌립니다",
              file=sys.stderr)
        return 2
    lockArgs = argparse.Namespace(resource=kBuildLockResource, owner=args.owner, shared=True, slots=getSharedSlotCount(kBuildLockResource),
                                  purpose=f"full-build {args.preset} {inputHash[:12]}", wait=args.wait, belowNormal=args.belowNormal)
    startTime = time.time()
    returnCode = runHolding(findMainRoot(repositoryRoot), lockArgs, tree.buildCommand(kTestBuildTargets, args.jobs))
    if returnCode != 0:
        print(f"[FullBuild] 빌드가 실패했습니다(종료 {returnCode}) — 증거를 남기지 않습니다.", file=sys.stderr)
        return returnCode
    if listUncommittedFiles(repositoryRoot) or readTreeHash(repositoryRoot, "HEAD") != treeHash:
        print("[FullBuild] 빌드하는 동안 트리가 바뀌었습니다 — 지은 것이 지금 커밋과 같다고 말할 수 없어 증거를 남기지 않습니다.", file=sys.stderr)
        return 2
    record = {"inputs": inputHash, "tree": treeHash, "commit": runGit(["rev-parse", "HEAD"], cwd=repositoryRoot).stdout.strip(), "preset": args.preset,
              "targets": list(kTestBuildTargets), "worktree": str(repositoryRoot), "owner": args.owner,
              "startTime": startTime, "endTime": time.time()}
    path = writeEvidence(getEvidenceDirectory(repositoryRoot), inputHash, record)
    print(f"[FullBuild] 전체 빌드 성공 — 증거 {path}")
    return 0


def main(argv: list[str] | None = None) -> int:
    listArgument = list(sys.argv[1:] if argv is None else argv)
    repositoryRoot = getProjectRoot()
    try:
        if listArgument[:1] == ["check-push"]:
            argparse.ArgumentParser(description="pre-push 훅 — 표준 입력의 push 줄을 본다").parse_args(listArgument[1:])
            return checkPush(repositoryRoot, sys.stdin.read())
        parser = argparse.ArgumentParser(description="빌드 칸을 잡고 all AllTests 전체 빌드 — 성공하면 그 커밋 트리의 증거를 남긴다")
        parser.add_argument("--owner", required=True, help="소유자 이름(에이전트 · 워크트리 이름) — 빌드 칸 잠금에 적힌다")
        parser.add_argument("--preset", default=kDefaultPreset, help=f"빌드 폴더 build/<프리셋>(기본 {kDefaultPreset})")
        parser.add_argument("-j", "--jobs", type=int, default=None, help="동시 작업 수(기본: 빌드 도구가 정한다)")
        parser.add_argument("--below-normal", dest="belowNormal", action="store_true", help="낮은 우선순위로 짓는다(도우미 — 작업 방식 2 절)")
        parser.add_argument("--wait", type=int, default=kDefaultWaitSeconds, help=f"빌드 칸을 기다릴 최대 초(기본 {kDefaultWaitSeconds})")
        args = parser.parse_args(listArgument)
        return runFullBuild(repositoryRoot, args)
    except RuntimeError as error:
        print(f"[FullBuild] {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
