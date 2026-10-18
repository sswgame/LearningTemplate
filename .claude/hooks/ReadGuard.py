"""읽기 가드 — 큰 마크다운 문서와 코드 파일의 통째 읽기를 실행 전에 막는다.

Claude Code PreToolUse 훅이 Read 도구 호출마다 부른다(.claude/settings.json). 표준 입력으로 훅 JSON 을 받아, 저장소 안의 `.md` 가
kMaxUnboundedReadBytes, 코드(.cpp · .h · .py 등)가 kMaxUnboundedCodeBytes 를 넘는데 `limit` 도 `offset` 도 없이 읽으려 하면 이유를 표준 오류에 쓰고 종료 코드 2 로 끝낸다(호출이 막히고
이유가 모델에 전달된다). 그 밖에는 아무것도 하지 않고 0 으로 끝난다.

이유(2026-10-19 토큰 절감): 290KB 짜리 결정 기록을 통째로 읽으면 한 번에 수만 토큰이 나가고, 작업자마다 되풀이된다. "큰 문서는 절만 읽는다"는
규칙은 어겨도 아무것도 막지 못했으므로, 통째 읽기를 규칙이 아니라 호출 형식(limit · offset 이 없는 Read)으로 막는다. 절만 읽는 길은
`py -3 -m Scripts doc <파일> [--section <제목>]` 이다.

훅 자신이 실패해도 도구 호출을 막지 않도록 모든 예외를 삼키고 0 으로 끝난다. GuardCommands.py 와 같은 정책이다 — 훅 결함 하나가 세션의 모든
읽기를 막으면 고칠 수단까지 잃는다("폴백 없이 멈춘다"의 의도된 예외).
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

kBlockExitCode = 2
kMaxUnboundedReadBytes = 30 * 1024  # 이보다 큰 .md 는 limit 또는 offset 없이 읽지 못한다(상수는 여기 한 곳)
kMaxUnboundedCodeBytes = 48 * 1024  # 코드 파일의 한도. Source 3588 개 중 약 2% 만 넘는다(2026-10-19 측정). 먼저 graphify query 로 범위를 좁힌다
kCodeSuffixes = (".cpp", ".h", ".hpp", ".inl", ".py", ".hlsl", ".hlsli", ".cmake")
kProjectRoot = Path(__file__).resolve().parents[2]


def readInput() -> dict:
    try:
        data = json.loads(sys.stdin.buffer.read().decode("utf-8", errors="replace") or "{}")
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


def checkRead(toolInput: dict, root: Path = kProjectRoot, maxBytes: int = kMaxUnboundedReadBytes,
              codeMaxBytes: int = kMaxUnboundedCodeBytes) -> str:
    """막을 이유(한국어)를 돌려준다. 통과면 빈 문자열."""
    if toolInput.get("limit") is not None or toolInput.get("offset") is not None:
        return ""
    filePath = toolInput.get("file_path")
    if not isinstance(filePath, str):
        return ""
    lowered = filePath.lower()
    isDoc = lowered.endswith(".md")
    if not isDoc and not lowered.endswith(kCodeSuffixes):
        return ""
    path = Path(filePath)
    try:
        resolved = path.resolve()
        resolved.relative_to(root.resolve())  # 저장소 밖이면 ValueError
        size = resolved.stat().st_size  # 파일이 없으면 OSError
    except (ValueError, OSError):
        return ""
    if size <= (maxBytes if isDoc else codeMaxBytes):
        return ""
    if not isDoc:
        return (f"{path.name} 은 {size // 1024}KB 라 통째로 읽지 못한다(한도 {codeMaxBytes // 1024}KB, 토큰 절감). 필요한 범위만 읽는다.\n"
                '(a) `graphify query "<질문>"` 으로 파일 · 심볼을 좁히고, `grep -n` 으로 행을 찾아 Read 의 offset · limit 로 그 범위만 읽는다.\n'
                "(b) 고치려는 곳이 정해졌으면 그 앞뒤 100행 안팎만 읽는다.")
    return (f"{path.name} 은 {size // 1024}KB 라 통째로 읽지 못한다(한도 {maxBytes // 1024}KB, 토큰 절감). 절만 읽는다.\n"
            f"(a) `py -3 -m Scripts doc {path.name}` 로 목차(제목 · 행 번호 · 크기)를 보고 `--section <제목 일부>` 로 한 절만 읽는다.\n"
            "(b) 또는 `grep -n '^#'` 로 절을 찾아 Read 의 offset · limit 로 그 범위만 읽는다.")


def main() -> int:
    try:
        data = readInput()
        if data.get("tool_name") != "Read":
            return 0
        reason = checkRead(data.get("tool_input") or {})
        if reason:
            # 콘솔 인코딩(cp949)이 아니라 UTF-8 바이트로 쓴다 - 모델이 받는 문구가 깨지지 않게 한다.
            sys.stderr.buffer.write(("[ReadGuard] " + reason + "\n").encode("utf-8"))
            sys.stderr.flush()
            return kBlockExitCode
    except Exception:
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
