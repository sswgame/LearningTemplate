"""
커밋 훅의 파일 단위 검사 범위 — 병합 커밋이면 어느 부모와도 내용이 다른 staged 파일만 넘긴다.

훅(`PreCommitLint.py`)이 쓰는 판정이다.
"""

from __future__ import annotations

from pathlib import Path

from common.Host import getMergeHeadRevisions, listFilesUnlikeEveryParent


def selectFileScopedStaged(projectRoot: Path, listStaged: list[Path]) -> list[Path]:
    """
    파일 단위 검사에 넘길 staged 파일. 병합 중이 아니면 staged 전체, 병합 중이면 어느 부모와도 내용이 다른 파일만(`PreCommitLint.py` 머리말).
    """
    listMergeHead = getMergeHeadRevisions(projectRoot)
    if not listMergeHead:
        return list(listStaged)

    listScoped = listFilesUnlikeEveryParent(listStaged, ["HEAD", *listMergeHead], projectRoot)
    print(f"[PreCommitLint] 병합 커밋: staged {len(listStaged)}개 중 {len(listScoped)}개만 파일 단위로 검사합니다 "
          f"(나머지 {len(listStaged) - len(listScoped)}개는 한쪽 부모와 내용이 같아 그 부모 커밋 때 검사됐습니다 — "
          f"트리 전체 게이트는 그대로 돈다).")
    return listScoped
