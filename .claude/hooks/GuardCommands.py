"""명령 가드 — 저장소 규칙을 어기는 셸 명령을 실행 전에 막는다.

Claude Code PreToolUse 훅이 Bash · PowerShell 도구 호출마다 부른다(.claude/settings.json). 표준 입력으로 훅 JSON 을 받아,
규칙을 어기면 이유를 표준 오류에 쓰고 종료 코드 2 로 끝낸다(호출이 막히고 이유가 모델에 전달된다). 그 밖에는 아무것도 하지 않고 0 으로 끝난다.

- 커밋 훅 우회 금지: `git commit --no-verify`. 커밋 훅(PreCommitLint)은 우회하지 않는다(CLAUDE.md · docs/11_Workflow.md).
- 링크가 든 폴더의 재귀 삭제 금지: 워크트리는 main 의 도구(`Tools/LLVM` 등)를 정션으로 나눠 쓴다. 링크를 끊지 않고 재귀로 지우면
  도구에 따라 링크 너머의 공유 도구까지 지워진다. 워크트리는 `py -3 -m Scripts worktree-remove <이름>` 으로 지운다.

훅 자신이 실패해도 도구 호출을 막지 않도록 모든 예외를 삼키고 0 으로 끝난다. 이것은 "폴백 없이 멈춘다"(도구 · 설정)의
의도된 예외다 — 훅 결함 하나가 세션의 모든 셸 호출을 막으면 고칠 수단까지 잃는다.
"""

from __future__ import annotations

import json
import os
import re
import stat
import sys
from pathlib import Path

kBlockExitCode = 2
kReparseAttribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
kMaxScanEntries = 20000
kMaxScanDepth = 4

# 재귀 삭제 명령. 한 줄 안에 이 낱말이 있고 아래 경로가 가리키는 곳에 링크가 있으면 막는다.
kRecursiveDeletePattern = re.compile(
    r"(?i)(\bRemove-Item\b.*-Recurse|\brm\b\s+-[a-z]*r|\brmdir\b.*\s/s\b|\brd\b.*\s/s\b|\bgit\s+worktree\s+remove\b|\bshutil\.rmtree\b)")
kNoVerifyPattern = re.compile(r"(?i)\bgit\b[^\n;&|]*\bcommit\b[^\n;&|]*--no-verify\b")
kQuotedPattern = re.compile(r"\"[^\"\n]*\"|'[^'\n]*'")
kWindowsPathPattern = re.compile(r"[A-Za-z]:[\\/][^\s\"'|;&<>]*")
kMsysPathPattern = re.compile(r"(?<![\w.])/([a-zA-Z])/([^\s\"'|;&<>]*)")


def readInput() -> dict:
    try:
        return json.loads(sys.stdin.buffer.read().decode("utf-8", errors="replace") or "{}")
    except Exception:
        return {}


def stripQuoted(command: str) -> str:
    """따옴표 안 글(커밋 메시지 따위)을 지운다. 규칙 낱말이 글 안에 있다고 막지 않기 위해서다."""
    return kQuotedPattern.sub(" ", command)


def findPaths(command: str) -> list[str]:
    pathList: list[str] = []
    for match in kWindowsPathPattern.finditer(command):
        pathList.append(match.group(0).replace("/", "\\"))
    for match in kMsysPathPattern.finditer(command):
        pathList.append(match.group(1).upper() + ":\\" + match.group(2).replace("/", "\\"))
    return pathList


def isLink(path: str) -> bool:
    try:
        bHasReparseAttribute = bool(os.lstat(path).st_file_attributes & kReparseAttribute)
        return bHasReparseAttribute or Path(path).is_symlink()
    except Exception:
        return False


def findLinkInside(root: str) -> str:
    """root 아래(링크를 따라가지 않고) 처음 만나는 링크의 경로를 돌려준다. 없으면 빈 문자열."""
    scanned = 0
    stack: list[tuple[str, int]] = [(root, 0)]
    while stack:
        current, depth = stack.pop()
        if depth > kMaxScanDepth:
            continue
        try:
            with os.scandir(current) as it:
                for entry in it:
                    scanned += 1
                    if scanned > kMaxScanEntries:
                        return ""
                    if isLink(entry.path):
                        return entry.path
                    if entry.is_dir(follow_symlinks=False):
                        stack.append((entry.path, depth + 1))
        except Exception:
            continue
    return ""


def checkNoVerify(command: str) -> str:
    if kNoVerifyPattern.search(stripQuoted(command)):
        return "커밋 훅은 우회하지 않는다(`--no-verify` 금지). 훅이 실패하면 원인을 고친다(CLAUDE.md · docs/11_Workflow.md)."
    return ""


def checkRecursiveDelete(command: str) -> str:
    if not kRecursiveDeletePattern.search(stripQuoted(command)):
        return ""
    for path in findPaths(command):
        if not Path(path).is_dir() or isLink(path):
            continue  # 링크 자체를 지우는 것은 괜찮다
        inner = findLinkInside(path)
        if inner:
            return ("재귀 삭제 대상 아래에 링크(정션)가 있다: " + inner + "\n"
                    "링크를 끊지 않고 지우면 main 의 공유 도구까지 지워질 수 있다. 워크트리는 `py -3 -m Scripts worktree-remove <이름>` 으로 지운다. "
                    "손으로 지울 때는 링크를 먼저 링크로서만 끊는다(비재귀 삭제).")
    return ""


def main() -> int:
    try:
        data = readInput()
        if data.get("tool_name") not in ("Bash", "PowerShell"):
            return 0
        command = (data.get("tool_input") or {}).get("command") or ""
        for reason in (checkNoVerify(command), checkRecursiveDelete(command)):
            if reason:
                # 콘솔 인코딩(cp949)이 아니라 UTF-8 바이트로 쓴다 - 모델이 받는 문구가 깨지지 않게 한다.
                sys.stderr.buffer.write(("[GuardCommands] " + reason + "\n").encode("utf-8"))
                sys.stderr.flush()
                return kBlockExitCode
    except Exception:
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
