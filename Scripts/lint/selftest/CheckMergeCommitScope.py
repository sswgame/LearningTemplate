#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
병합 커밋에서 커밋 훅이 파일 단위로 검사할 파일을 **빠뜨리지 않고** 줄이는지 검사합니다.

`PreCommitLint` 는 병합 중이면 파일 단위 게이트 · 픽서 · clang-format 에 "staged 내용이 어느 부모와도 다른 파일" 만 넘긴다
(`PreCommitLint` 머리말). 이 줄이기가 틀리면 둘 중 하나다 — 새 내용(충돌 해결 · 자동 병합)이 빠져 **검사 없이 커밋되거나**,
부모와 같은 파일이 남아 병합 커밋이 다시 수십 분 걸린다. 둘 다 조용하다. 그래서 임시 git 저장소에 병합을 실제로 만들어 본다:

- 한쪽 부모에서만 바뀐 파일 · 한쪽에서만 더한 파일 → 빠진다.
- 두 쪽이 다른 자리를 고쳐 자동 병합된 파일 · 충돌을 손으로 푼 파일 · 병합하며 새로 더한 파일 → 남는다.
- 병합 중이 아니면 staged 전체가 그대로다.

  python Scripts/lint/selftest/CheckMergeCommitScope.py [--root <repo>] [--verbose]
"""
from __future__ import annotations

import argparse
import os
import shutil
import stat
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — PreCommitLint
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

from common import ProcessResult, getAllStagedFiles, getMergeHeadRevisions, resolveGitExecutable, runProcess  # noqa: E402
from PreCommitLint import selectFileScopedStagedInternal  # noqa: E402

#: CMake 등록 정보 (`Scripts/lint/LintCatalog.py`). 영어인 이유는 ninja 가 찍는 줄이기 때문이다.
kLintBuildComment = "Checking that a merge commit's hook still checks every file with new content..."
kLintTimeoutSeconds = 60

#: 열 줄짜리 본문 — 두 부모가 서로 다른 줄을 고치면 git 이 자동 병합한다.
_kBaseText = "".join(f"line {index}\n" for index in range(10))


def runGitInternal(repoRoot: Path, *listArgument: str, bCheck: bool = True) -> ProcessResult:
    """사용자 · 전역 설정과 무관하게 같은 결과를 내도록 신원 · 줄끝 · 서명을 고정해서 git 을 부릅니다."""
    command = [resolveGitExecutable() or "git", "-c", "user.name=lint-selftest", "-c", "user.email=lint@selftest",
               "-c", "core.autocrlf=false", "-c", "commit.gpgsign=false", *listArgument]
    result = runProcess(command, cwd=repoRoot)
    if bCheck and result.returnCode != 0:
        raise RuntimeError(f"git {' '.join(listArgument)} 실패: {result.stderr.strip()}")
    return result


def writeFileInternal(repoRoot: Path, relPath: str, text: str) -> None:
    path = repoRoot / relPath
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode("utf-8"))


def replaceLineInternal(text: str, lineIndex: int, newLine: str) -> str:
    listLine = text.splitlines(keepends=True)
    listLine[lineIndex] = newLine + "\n"
    return "".join(listLine)


def buildMergeInternal(repoRoot: Path) -> None:
    """main 과 side 두 갈래를 만들고 side 를 main 에 `--no-commit` 으로 병합해, 커밋 직전 상태로 둡니다."""
    runGitInternal(repoRoot, "init", "-q", "-b", "main")
    for name in ("OursOnly", "TheirsOnly", "AutoMerged", "Conflict", "Untouched"):
        writeFileInternal(repoRoot, f"Source/{name}.cpp", _kBaseText)
    runGitInternal(repoRoot, "add", "-A")
    runGitInternal(repoRoot, "commit", "-q", "-m", "base")

    runGitInternal(repoRoot, "checkout", "-q", "-b", "side")
    writeFileInternal(repoRoot, "Source/TheirsOnly.cpp", replaceLineInternal(_kBaseText, 2, "theirs"))
    writeFileInternal(repoRoot, "Source/AutoMerged.cpp", replaceLineInternal(_kBaseText, 8, "theirs"))
    writeFileInternal(repoRoot, "Source/Conflict.cpp", replaceLineInternal(_kBaseText, 5, "theirs"))
    writeFileInternal(repoRoot, "Source/TheirsAdded.cpp", "added on side\n")
    runGitInternal(repoRoot, "add", "-A")
    runGitInternal(repoRoot, "commit", "-q", "-m", "side")

    runGitInternal(repoRoot, "checkout", "-q", "main")
    writeFileInternal(repoRoot, "Source/OursOnly.cpp", replaceLineInternal(_kBaseText, 2, "ours"))
    writeFileInternal(repoRoot, "Source/AutoMerged.cpp", replaceLineInternal(_kBaseText, 1, "ours"))
    writeFileInternal(repoRoot, "Source/Conflict.cpp", replaceLineInternal(_kBaseText, 5, "ours"))
    runGitInternal(repoRoot, "add", "-A")
    runGitInternal(repoRoot, "commit", "-q", "-m", "main")

    # 충돌이 나므로 종료 코드는 0 이 아니다 — 손으로 풀고 staged 로 올린다.
    runGitInternal(repoRoot, "merge", "--no-commit", "--no-ff", "side", bCheck=False)
    writeFileInternal(repoRoot, "Source/Conflict.cpp", replaceLineInternal(_kBaseText, 5, "resolved"))
    writeFileInternal(repoRoot, "Source/AddedInMerge.cpp", "added while merging\n")
    runGitInternal(repoRoot, "add", "-A")


def removeTreeInternal(root: Path) -> None:
    """임시 저장소를 지웁니다. git 객체 파일은 읽기 전용이라 윈도우에서는 쓰기 속성을 먼저 줘야 지워진다."""
    for current, _, listFileName in os.walk(root):
        for fileName in listFileName:
            os.chmod(os.path.join(current, fileName), stat.S_IWRITE)
    shutil.rmtree(root, ignore_errors=True)


def namesInternal(listPath: list[Path]) -> set[str]:
    return {path.name for path in listPath}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="병합 커밋 검사 범위 음성 테스트")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트 (이 검사는 쓰지 않는다 — CTest 가 준다)")
    parser.add_argument("--verbose", action="store_true", help="고른 파일을 모두 출력")
    args = parser.parse_args(argv)

    errors: list[str] = []
    tempRoot = Path(tempfile.mkdtemp(prefix="sw_merge_scope_"))
    try:
        plainRoot = tempRoot.resolve() / "plain"
        plainRoot.mkdir()
        runGitInternal(plainRoot, "init", "-q", "-b", "probe")
        writeFileInternal(plainRoot, "Source/Plain.cpp", "plain\n")
        runGitInternal(plainRoot, "add", "-A")
        if getMergeHeadRevisions(plainRoot):
            errors.append("병합 중이 아닌데 MERGE_HEAD 를 찾았습니다")
        if namesInternal(selectFileScopedStagedInternal(plainRoot, getAllStagedFiles(plainRoot))) != {"Plain.cpp"}:
            errors.append("병합 중이 아닐 때 staged 전체를 넘기지 않습니다")

        repoRoot = tempRoot.resolve() / "merge"
        repoRoot.mkdir()
        buildMergeInternal(repoRoot)
        listMergeHead = getMergeHeadRevisions(repoRoot)
        if len(listMergeHead) != 1:
            errors.append(f"병합 중인데 MERGE_HEAD 를 {len(listMergeHead)}개 읽었습니다(1개여야 한다)")

        listStaged = getAllStagedFiles(repoRoot)
        setStaged = namesInternal(listStaged)
        setScoped = namesInternal(selectFileScopedStagedInternal(repoRoot, listStaged))
        if args.verbose:
            print(f"  staged : {sorted(setStaged)}")
            print(f"  scoped : {sorted(setScoped)}")

        setExpectedStaged = {"TheirsOnly.cpp", "TheirsAdded.cpp", "AutoMerged.cpp", "Conflict.cpp", "AddedInMerge.cpp"}
        if setStaged != setExpectedStaged:
            errors.append(f"시험 병합의 staged 목록이 예상과 다릅니다: {sorted(setStaged)} (시험 자체가 틀렸다)")
        for name in ("AutoMerged.cpp", "Conflict.cpp", "AddedInMerge.cpp"):
            if name not in setScoped:
                errors.append(f"{name}: 병합으로 내용이 새로 생겼는데 검사 대상에서 빠졌습니다 — 검사 없이 커밋된다")
        for name in ("TheirsOnly.cpp", "TheirsAdded.cpp"):
            if name in setScoped:
                errors.append(f"{name}: 한쪽 부모와 내용이 같은데 다시 검사합니다 — 병합 커밋이 느려진다")
    except RuntimeError as exception:
        print(f"[CheckMergeCommitScope] 시험 저장소를 만들 수 없습니다: {exception}", file=sys.stderr)
        return 2
    finally:
        removeTreeInternal(tempRoot)

    if errors:
        print(f"[CheckMergeCommitScope] 문제 {len(errors)}건", file=sys.stderr)
        for error in errors:
            print(f"  {error}")
        return 1

    print("[CheckMergeCommitScope] OK (병합 커밋: 새 내용 3개 남김 · 부모와 같은 2개 뺌, 병합 아님: 전체)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
