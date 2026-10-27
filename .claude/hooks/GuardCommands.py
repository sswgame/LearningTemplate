"""명령 가드 — 저장소 규칙을 어기는 셸 명령을 실행 전에 막는다.

Claude Code PreToolUse 훅이 Bash · PowerShell 도구 호출마다 부른다(.claude/settings.json). 표준 입력으로 훅 JSON 을 받아,
규칙을 어기면 이유를 표준 오류에 쓰고 종료 코드 2 로 끝낸다(호출이 막히고 이유가 모델에 전달된다). 그 밖에는 아무것도 하지 않고 0 으로 끝난다.

- 커밋 훅 우회 금지: `git commit --no-verify`. 커밋 훅(PreCommitLint)은 우회하지 않는다(CLAUDE.md · docs/11_Workflow.md).
- 큰 마크다운의 셸 통째 읽기 금지(2026-10-19 토큰 절감): `cat` · `Get-Content` · 넓은 `sed -n` 으로 kMaxUnboundedReadBytes 를 넘는 `.md` 를 읽는 것.
  ReadGuard 는 Read 도구만 막아서 셸로 읽으면 지나갔다. 절만 읽는 길은 `py -3 -m Scripts doc <파일> --section <제목>` 이다.
- 인터프리터 heredoc 금지(2026-10-25): `python - <<EOF` 로 파일을 고치는 것. 규칙이 문서에만 있어 작업자가 계속 어겼고, 빈 본문의
  `python -` 가 표준 입력을 기다리며 CPU 를 돌았다. `find /`(디스크 전체 검색)도 같은 이유로 막는다.
- 전경 장기 실행 금지(2026-10-27): `verify-batch` · `full-build` 등을 전경에서 돌리거나 20초 이상 `sleep` 으로 기다리는 것. 메시지는 받는 쪽의
  다음 도구 호출에야 닿아서, 통합 담당이 수 시간 전경에 있는 동안 메인 · 사용자의 지시가 큐에 쌓였다. `run_in_background` 호출은 통과한다.
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
kMaxUnboundedReadBytes = 30 * 1024  # ReadGuard.py 와 같은 값(훅은 서로 import 하지 않는 독립 스크립트라 두 곳이다 - 바꿀 때 함께)
kMaxUnboundedCodeBytes = 48 * 1024  # 코드 파일의 한도. ReadGuard.py 와 같은 값(바꿀 때 함께)
kMaxSedRangeLines = 300  # 이 줄 수 이하의 `sed -n 'a,bp'` 는 절 읽기로 보고 통과시킨다

# 재귀 삭제 명령. 한 줄 안에 이 낱말이 있고 아래 경로가 가리키는 곳에 링크가 있으면 막는다.
kRecursiveDeletePattern = re.compile(
    r"(?i)(\bRemove-Item\b.*-Recurse|\brm\b\s+-[a-z]*r|\brmdir\b.*\s/s\b|\brd\b.*\s/s\b|\bgit\s+worktree\s+remove\b|\bshutil\.rmtree\b)")
kNoVerifyPattern = re.compile(r"(?i)\bgit\b[^\n;&|]*\bcommit\b[^\n;&|]*--no-verify\b")
kInterpreterHeredocPattern = re.compile(
    r"(?i)(?:\b(?:python3?|py)(?:\s+-3)?(?:\s+-)?\s*<<|\b(?:cat|tee)\b[^\n|;&<]*>>?\s*[^\s<>|;&]+\s*<<)")
kShellWrapperPattern = re.compile(r"(?i)(?:\b(?:bash|sh|zsh|dash|pwsh|powershell(?:\.exe)?)\b[^\n|;&]*\s-(?:\w*c|Command)\b|\beval\b|\bcmd(?:\.exe)?\s+/c\b)")
kLongRunPattern = re.compile(r"(?i)-m\s+Scripts\s+(?:verify-batch|full-build|soak|load\s+(?:suite|run)|perf-compare)\b")
kQuickFlagPattern = re.compile(r"(?i)\s(?:--dry-run|--list|--check|--help|-h)\b")
kSleepPattern =re.compile(r"(?i)\bsleep\s+(\d+)\b")
kMaxForegroundSleepSeconds = 20  # 이보다 짧은 sleep 은 재시도 간격 정도라 통과시킨다
kRootFindPattern =re.compile(r"(?i)(?:^|[;&|('\"]|&&)\s*find\s+/(?:\s|$)")
kQuotedPattern = re.compile(r"\"[^\"\n]*\"|'[^'\n]*'")
kWindowsPathPattern = re.compile(r"[A-Za-z]:[\\/][^\s\"'|;&<>]*")
kMsysPathPattern = re.compile(r"(?<![\w.])/([a-zA-Z])/([^\s\"'|;&<>]*)")
kShellReadPattern = re.compile(r"(?i)(?:^|[;&]|&&)\s*(?:cat|Get-Content|gc|type)\s+[^|;&\n]*?(?P<path>[^\s\"'|;&<>]+\.(?:md|cpp|h|hpp|inl|py|hlsl|hlsli|cmake))\b(?!\s*\|)")
kSedRangePattern = re.compile(r"\bsed\s+-n\s+['\"]?(\d+),(\d+)p")
kSedReadPattern = re.compile(r"\bsed\s+-n\s+[^|;&\n]*?(?P<path>[^\s\"'|;&<>]+\.(?:md|cpp|h|hpp|inl|py|hlsl|hlsli|cmake))\b")


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


def checkInterpreterHeredoc(command: str) -> str:
    """파이썬 표준 입력 heredoc 으로 파일을 고치려 하면 막을 이유를 돌려준다(EditingTraps). 본문이 비거나 파이프가 끊기면 인터프리터가 CPU 를 돌며 남는다."""
    # 셸을 다시 부르는 명령(`bash -c "..."` · `eval '...'`)은 heredoc 이 따옴표 안에 숨으므로 원문 전체를 본다.
    # 그 밖에는 따옴표 안 글(커밋 메시지 따위)을 지워서 본다.
    scanned = command if kShellWrapperPattern.search(stripQuoted(command)) else stripQuoted(command)
    if kInterpreterHeredocPattern.search(scanned):
        return ("`python - <<EOF` · `py -3 - <<EOF` 같은 인터프리터 heredoc 은 금지다(docs/agents/EditingTraps.md). "
                "파일 편집은 Edit 도구로 하고, 스크립트가 필요하면 파일로 써서 `py -3 <파일>` 로 돌린다.")
    return ""


def checkForegroundLongRun(command: str, bBackground: bool) -> str:
    """수 분 넘게 걸리는 검증 · 빌드를 전경에서 돌리거나 `sleep` 으로 기다리면 막을 이유를 돌려준다. 백그라운드 호출은 통과한다."""
    if bBackground:
        return ""
    bare = stripQuoted(command)
    if kLongRunPattern.search(bare) and kQuickFlagPattern.search(bare):
        return ""  # 목록 · 점검 · 미리 보기는 바로 끝난다
    if kLongRunPattern.search(bare) or any(int(seconds) >= kMaxForegroundSleepSeconds for seconds in kSleepPattern.findall(bare)):
        return ("수 분 넘는 검증 · 빌드는 `run_in_background: true` 로 띄우고 끝 알림을 기다린다. 전경에서 돌리거나 `sleep` 폴링으로 기다리면 "
                "그동안 도구 호출이 없어 메인 · 사용자의 메시지가 닿지 못한다(docs/12_Agents.md 2절).")
    return ""


def checkRootFind(command: str) -> str:
    scanned = command if kShellWrapperPattern.search(stripQuoted(command)) else stripQuoted(command)
    if kRootFindPattern.search(scanned):
        return "`find /` 는 디스크 전체를 훑어 CPU 를 오래 쓴다. 저장소 안이면 Grep · Glob 도구나 `find <폴더>` 로 범위를 좁힌다."
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


def resolveRepositoryFile(token: str, root: Path) -> Path | None:
    """저장소 안의 실제 파일이면 그 경로, 아니면 None. 상대 경로는 root 기준이다."""
    candidate = Path(token.replace("\\", "/"))
    for candidateText in findPaths(token) or [str(root / candidate)]:
        try:
            resolved = Path(candidateText).resolve()
            resolved.relative_to(root.resolve())
            if resolved.is_file():
                return resolved
        except (ValueError, OSError):
            continue
    return None


def checkShellBigDocRead(command: str, root: Path | None = None, maxBytes: int = kMaxUnboundedReadBytes) -> str:
    """셸로 큰 .md 를 통째(또는 넓은 범위로) 읽으려 하면 막을 이유를, 아니면 빈 문자열을 돌려준다."""
    root = root or Path(__file__).resolve().parents[2]
    bare = stripQuoted(command)
    token = ""
    match = kShellReadPattern.search(bare)
    if match:
        token = match.group("path")
    else:
        sedMatch = kSedReadPattern.search(bare)
        if sedMatch:
            rangeMatch = kSedRangePattern.search(command)
            if rangeMatch and int(rangeMatch.group(2)) - int(rangeMatch.group(1)) <= kMaxSedRangeLines:
                return ""
            token = sedMatch.group("path")
    if not token:
        return ""
    resolved = resolveRepositoryFile(token, root)
    if resolved is None:
        return ""
    isDoc = resolved.suffix.lower() == ".md"
    if resolved.stat().st_size <= (maxBytes if isDoc else kMaxUnboundedCodeBytes):
        return ""
    if not isDoc:
        return (f"{resolved.name} 은 {resolved.stat().st_size // 1024}KB 라 셸로 통째 읽지 못한다(한도 {kMaxUnboundedCodeBytes // 1024}KB, 토큰 절감).\n"
                '`graphify query "<질문>"` 으로 범위를 좁히고, `grep -n` 으로 행을 찾아 Read 의 offset · limit 나 '
                f"`sed -n 'a,bp'`({kMaxSedRangeLines}줄 이하)로 그 범위만 읽는다.")
    return (f"{resolved.name} 은 {resolved.stat().st_size // 1024}KB 라 셸로 통째 읽지 못한다(한도 {maxBytes // 1024}KB, 토큰 절감). 절만 읽는다.\n"
            f"(a) `py -3 -m Scripts doc {resolved.name}` 로 목차를 보고 `--section <제목 일부>` 로 한 절만 읽는다.\n"
            f"(b) 또는 `sed -n 'a,bp'` 로 {kMaxSedRangeLines}줄 이하 범위만 읽는다.")


def main() -> int:
    try:
        data = readInput()
        if data.get("tool_name") not in ("Bash", "PowerShell"):
            return 0
        toolInput = data.get("tool_input") or {}
        command = toolInput.get("command") or ""
        for reason in (checkNoVerify(command), checkInterpreterHeredoc(command), checkRootFind(command), checkRecursiveDelete(command),
                       checkShellBigDocRead(command), checkForegroundLongRun(command, bool(toolInput.get("run_in_background")))):
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
