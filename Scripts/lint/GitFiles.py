"""
Scripts/lint/GitFiles.py

린트 대상 C++ · 셰이더 파일 목록 — staged 와 작업 트리 변경분(untracked 포함).
"""

from __future__ import annotations

from pathlib import Path

from common.Constants import kCppAllExtensions
from common.Git import kStagedFileArgument, listGitFiles
from common.Paths import getProjectRoot


def getStagedCppFiles(root: Path | None = None,
                      extensions: set[str] | None = None) -> list[Path]:
    """
    Git Staged(인덱스) 상태인 C++ 소스 및 헤더 파일 목록을 반환합니다.
    """
    targetExtensions = extensions if extensions is not None else kCppAllExtensions
    return sorted(listGitFiles(root or getProjectRoot(), kStagedFileArgument, targetExtensions))


def getModifiedCppFiles(root: Path | None = None,
                        extensions: set[str] | None = None,
                        includeUntracked: bool = True) -> list[Path]:
    """
    Git 작업 트리에서 수정(Tracked)되었거나 새로 생성된(Untracked) C++ 소스/헤더 파일 목록을 반환합니다.
    """
    targetExtensions = extensions if extensions is not None else kCppAllExtensions
    projectRoot = root or getProjectRoot()

    # 1. 수정된 파일 (Staged + Unstaged)
    fileSet = listGitFiles(projectRoot, ["diff", "--name-only", "HEAD"], targetExtensions)
    # 2. 새로 추가된 파일 (Untracked)
    if includeUntracked:
        fileSet |= listGitFiles(projectRoot, ["ls-files", "--others", "--exclude-standard"], targetExtensions)
    return sorted(fileSet)
