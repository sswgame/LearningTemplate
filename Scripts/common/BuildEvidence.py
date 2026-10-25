"""
@file BuildEvidence.py
@brief 검증 증거 파일의 자리 · 키 · 읽기 쓰기 — `full-build`(빌드 증거)와 `integ-gate`(통합 증거)가 같이 쓴다.

증거는 공통 `.git` 아래 폴더에 **빌드 입력 해시**를 이름으로 한 JSON 하나다. 빌드 입력은 커밋 트리에서 빌드 · 테스트가 읽지 않는 파일
(`isBuildInputPath`: 마크다운 · `.claude/` · 빌드가 실행하지 않는 Scripts 폴더 · 파이썬 단위 테스트)을 뺀 것이라, 그런 파일만 바꾼 커밋은
직전 증거가 그대로 맞는다. 같은 입력이면 어느 워크트리 · 어느 커밋에서 지었든 같다. 공통 .git 은 모든 워크트리가 나눠 쓰고 추적되지 않는다.

두 명령이 이 틀을 따로 갖지 않게 여기 한 곳에 둔다(`dev` 끼리는 import 하지 않는다 — `CheckScriptLayers`). 폴더 이름만 증거 종류마다 다르다.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Callable

from .Host import runGit

#: 전체 빌드(`full-build`) 증거 폴더 — 공통 .git 아래.
kBuildEvidenceDirName = "sw-build-evidence"
#: 증거를 요구하는 원격 브랜치.
kProtectedRemoteRefs: tuple[str, ...] = ("refs/heads/main",)
#: git 이 "없음" 을 적는 해시(지우는 push 의 로컬 쪽).
kZeroHash = "0" * 40
#: 빌드 · 테스트가 읽지 않아 증거 키에서 빼는 최상위 폴더.
kNonBuildInputFolders: tuple[str, ...] = (".claude",)
#: 그 아래(와 저장소 최상단)의 마크다운은 빌드 입력이 아니다. 다른 폴더의 .md 는 빌드가 읽을 수 있어 입력으로 둔다
#: (`docs/Config/ConfigReference.json` 처럼 docs 아래 데이터는 테스트가 읽으므로 .md 만 뺀다).
kNonBuildInputMarkdownFolders: tuple[str, ...] = ("docs", "Scripts", "cmake")
#: 빌드가 실행하는 Scripts 폴더(cmake 가 부른다: generate · setup, 그것이 가져오는 common). 그 밖의 Scripts 아래(dev · lint · qa …)와
#: Scripts 바로 아래 파일은 빌드 · AllTests 와 무관한 도구라 입력이 아니다. lint 는 cmake 가 파일 이름 목록으로만 읽는다.
kBuildInputScriptFolders: tuple[str, ...] = ("generate", "setup", "common")
#: 파이썬 단위 테스트(ctest 의 파이썬 묶음)는 AllTests 가 아니라 입력이 아니다. CMakeLists.txt 등 다른 파일은 입력이다.
kNonBuildInputPythonTestFolder = "Test/PythonTest/"


def getEvidenceDirectory(repositoryRoot: Path, dirName: str = kBuildEvidenceDirName) -> Path:
    """증거 폴더 — 모든 워크트리가 나눠 쓰는 공통 .git 아래."""
    result = runGit(["rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=repositoryRoot)
    if not result.bSucceeded:
        raise RuntimeError(f"git 저장소가 아닙니다: {repositoryRoot}\n{result.stderr.strip()}")
    return Path(result.stdout.strip()) / dirName


def readTreeHash(repositoryRoot: Path, revision: str) -> str:
    """커밋의 트리 해시입니다."""
    result = runGit(["rev-parse", f"{revision}^{{tree}}"], cwd=repositoryRoot)
    if not result.bSucceeded:
        raise RuntimeError(f"{revision} 의 트리를 읽지 못했습니다: {result.stderr.strip()}")
    return result.stdout.strip()


def isBuildInputPath(path: str) -> bool:
    """저장소 기준 경로가 빌드 · 테스트의 입력인가. 마크다운(최상단 · docs · Scripts · cmake 아래), `.claude/`, 빌드가 실행하지 않는 Scripts 폴더, 파이썬 단위 테스트는 아니다."""
    listPart = path.split("/")
    if listPart[0] in kNonBuildInputFolders:
        return False
    if listPart[0] == "Scripts" and (len(listPart) < 3 or listPart[1] not in kBuildInputScriptFolders):
        return False
    if path.startswith(kNonBuildInputPythonTestFolder) and path.endswith(".py"):
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
