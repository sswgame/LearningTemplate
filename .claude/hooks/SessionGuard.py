"""세션 가드 — 세션 시작과 API 실패를 사용자와 모델에게 알린다.

Claude Code 훅이 부른다(.claude/settings.json). 표준 입력으로 훅 JSON 을 받아,
알릴 것이 있을 때만 JSON 을 출력한다. 알릴 것이 없으면 아무것도 출력하지 않아 토큰을 쓰지 않는다.

- SessionStart     : 인계 문서(docs/plans/NEXT.md)가 있으면 먼저 읽으라고, ponytail 스킬이 있으면 호출하라고 알린다. 남은 워크트리 수와, main CI 가 실패 중인지(잡과 연속 실패 수)도 본다.
- StopFailure      : 사용량 · 요청 한도 같은 API 실패로 멈췄다고 알린다.

훅이 실패해도 세션을 막지 않도록 모든 예외를 삼키고 0 으로 끝난다. 이것은 "폴백 없이 멈춘다"(도구 · 설정)의
의도된 예외다 — 알림 훅의 결함으로 세션이 멈추면 고칠 수단까지 잃는다.
"""

import json
import os
import subprocess
import sys

kMaxWorktrees = 4                         # 메인 + 통합 + 도우미 둘을 넘으면 정리할 때
kHandoffPath = os.path.join( "docs", "plans", "NEXT.md" )
kRoadmapPath = os.path.join( "docs", "plans", "Roadmap.md" )
kPonytailPath =os.path.join( ".claude", "skills", "ponytail", "SKILL.md" )
kCiSummaryTimeoutSeconds = 10             # 훅 한도(15 s, settings.json) 안에서 끝낸다
kCiFailingExitCode = 4                    # Scripts/common/ExitCode.py 의 CiFailing —CI 가 죽어도 보이지 않던 것(C-41 1단계)을 세션 시작에 알린다

def readInput():
    try:
        return json.loads( sys.stdin.buffer.read().decode( "utf-8", errors = "replace" ) or "{}" )
    except Exception:
        return {}


def emit( eventName, userMessage = "", modelContext = "" ):
    if not userMessage and not modelContext:
        return
    out = {}
    if userMessage:
        out[ "systemMessage" ] = userMessage
    if modelContext and eventName in ( "SessionStart", "UserPromptSubmit" ):
        out[ "hookSpecificOutput" ] = { "hookEventName": eventName, "additionalContext": modelContext }
    sys.stdout.buffer.write( json.dumps( out, ensure_ascii = False ).encode( "utf-8" ) )


def countWorktrees( root ):
    try:
        text = subprocess.run( [ "git", "-C", root, "worktree", "list", "--porcelain" ],
                               capture_output = True, text = True, timeout = 5 ).stdout
        return sum( 1 for line in text.splitlines() if line.startswith( "worktree " ) )
    except Exception:
        return 0


def readFailingCi( root ):
    """main CI 의 가장 새로 끝난 실행이 실패면 그 한 줄, 아니면 빈 글. 네트워크 · 한도 · 도구 오류도 빈 글이다(알림 훅은 세션을 막지 않는다)."""
    try:
        result = subprocess.run( [ sys.executable, "-m", "Scripts", "ci-jobs", "--summary" ], cwd = root,
                                 capture_output = True, timeout = kCiSummaryTimeoutSeconds )
        if result.returncode != kCiFailingExitCode:
            return ""
        return result.stdout.decode( "utf-8", errors = "replace" ).strip()
    except Exception:
        return ""


def readParallelItems( root ):
    """로드맵 `## 지금 순서` 에서 `[병렬 가능]` 표시가 붙은 줄의 이름(` — ` 앞)을 모은다. 표시가 정본이라 손으로 옮겨 적지 않는다."""
    try:
        with open( os.path.join( root, kRoadmapPath ), encoding = "utf-8" ) as file:
            listLine = file.read().splitlines()
    except OSError:
        return []
    bInOrder = False
    listItem = []
    for line in listLine:
        if line.startswith( "## " ):
            bInOrder = line.startswith( "## 지금 순서" )
        elif bInOrder and "[병렬 가능]" in line:
            listItem.append( line.strip().lstrip( "-* " ).split( " — " )[ 0 ][ :40 ] )
    return listItem


def onSessionStart( data, root ):
    notes = []
    if os.path.isfile( os.path.join( root, kHandoffPath ) ):
        notes.append( f"인계 문서 {kHandoffPath} 가 있다 — 일을 시작하기 전에 읽고, 끝난 항목은 지울 것." )
    if os.path.isfile( os.path.join( root, kPonytailPath ) ):
        notes.append( "ponytail 스킬을 지금 Skill 로 호출할 것(모든 작업에 켠다는 CLAUDE.md 규칙, 자동으로 켜지지 않는다)." )
    listParallel = readParallelItems( root )
    if listParallel:
        notes.append( f"로드맵에 [병렬 가능] 일이 {len( listParallel )} 개 있다(`plans` 로 확인). 맡기기 전에 /worker-brief 0번(놀고 있는 같은 레벨 작업자 재사용 먼저)을 따를 것." )
    failingCi = readFailingCi( root )
    if failingCi:
        notes.append( failingCi )
    worktrees = countWorktrees( root )
    if worktrees > kMaxWorktrees:
        notes.append( f"워크트리가 {worktrees} 개 남아 있다(기준 {kMaxWorktrees}). 끝난 것은 Scripts/dev 정리 스크립트로 지울 것." )
    if notes:
        emit( "SessionStart", userMessage = "[세션 가드] " + " / ".join( notes ), modelContext = "\n".join( notes ) )


def onStopFailure( data, root ):
    reason = data.get( "error" ) or data.get( "reason" ) or data.get( "stop_reason" ) or ""
    emit( "StopFailure", userMessage = f"[세션 가드] API 오류로 멈췄습니다{(' (' + str( reason ) + ')') if reason else ''}. 사용량 한도라면 리셋 뒤 이어 가고, 길어진 세션이면 새 세션에서 docs/plans/NEXT.md 로 이어 가세요." )


def main():
    data = readInput()
    root = data.get( "cwd" ) or os.getcwd()
    handlers = {
        "SessionStart": onSessionStart,
        "StopFailure": onStopFailure,
    }
    handler = handlers.get( data.get( "hook_event_name", "" ) )
    if handler:
        handler( data, root )


if __name__ == "__main__":
    try:
        main()
    except Exception:
        pass
    sys.exit( 0 )
