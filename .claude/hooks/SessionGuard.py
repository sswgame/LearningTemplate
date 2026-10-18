"""세션 가드 — 작업 규칙(docs/11_Workflow.md)을 어기거나 한도에 닿았을 때 사용자와 모델에게 알린다.

Claude Code 훅이 부른다(.claude/settings.json). 표준 입력으로 훅 JSON 을 받아,
알릴 것이 있을 때만 JSON 을 출력한다. 알릴 것이 없으면 아무것도 출력하지 않아 토큰을 쓰지 않는다.

- SessionStart     : 인계 문서(docs/plans/NEXT.md)가 있으면 먼저 읽으라고, ponytail 스킬이 있으면 호출하라고 알린다. 남은 워크트리 수와, main CI 가 실패 중인지(잡과 연속 실패 수)도 본다.
- UserPromptSubmit : 대화 기록 크기 · 압축 횟수(새 세션 권장, 하드 한도는 단계마다 한 번 프롬프트 차단), 동시 빌드 수, 규칙과 부딪히는 요청 낱말을 본다.
- PreCompact       : 컨텍스트 한도에 닿았다고 사용자에게 알린다.
- StopFailure      : 사용량 · 요청 한도 같은 API 실패로 멈췄다고 알린다.

훅이 실패해도 세션을 막지 않도록 모든 예외를 삼키고 0 으로 끝난다. 이것은 "폴백 없이 멈춘다"(도구 · 설정)의
의도된 예외다 — 알림 훅의 결함으로 세션이 멈추면 고칠 수단까지 잃는다.
"""

import json
import os
import subprocess
import sys
import time

kTranscriptWarnBytes = 2 * 1024 * 1024    # 대화 기록이 이만큼 크면 묶음 경계에서 새 세션을 권한다
kTranscriptStrongBytes = 3 * 1024 * 1024 # 이만큼이면 지금 인계 문서를 쓰고 새 세션으로 가라고 권한다
kHardLimitStepsBytes = ( 4 * 1024 * 1024, 8 * 1024 * 1024, 16 * 1024 * 1024 )  # 단계마다 한 번 프롬프트를 막는다(비용이 세션 길이에 비례 — 경고만으로는 무시됐다)
kHardLimitCompactions = 2                 # 압축이 이만큼이면 최소 1단계
kHardBlockDirName = "sw-session-guard"    # 공통 .git 아래. 세션 아이디별 <id>.json 에 마지막으로 막은 단계를 남긴다
kExitBlockPrompt = 2                      # UserPromptSubmit 에서 종료 코드 2 는 프롬프트를 지우고 표준 오류를 사용자에게 보인다
kMaxConcurrentBuilds = 2                 # docs/11_Workflow.md: 동시에 빌드하는 에이전트는 최대 둘
kMaxWorktrees = 4                         # 메인 + 통합 + 도우미 둘을 넘으면 정리할 때
kHandoffPath = os.path.join( "docs", "plans", "NEXT.md" )
kRoadmapPath = os.path.join( "docs", "plans", "Roadmap.md" )
kPonytailPath =os.path.join( ".claude", "skills", "ponytail", "SKILL.md" )
kCiSummaryTimeoutSeconds = 10             # 훅 한도(15 s, settings.json) 안에서 끝낸다
kCiFailingExitCode = 4                    # Scripts/dev/ListCiJobs.py 의 kExitCiFailing — CI 가 죽어도 보이지 않던 것(C-41 1단계)을 세션 시작에 알린다

# 사용자 요청 중 정해 둔 규칙과 부딪히는 표현 → 알릴 말. 정확한 판단은 모델이 하고, 여기서는 단서만 준다.
kRuleConflictList = [
    ( ( "제안서", ), "새 일은 제안서 단계 없이 바로 구현한다(docs/11_Workflow.md). 제안서가 꼭 필요한지 사용자에게 확인할 것." ),
    ( ( "전 게임", "전체 매트릭스", "모든 백엔드 × 모든 게임" ), "묶음 끝 검증은 네 백엔드 × 대표 셋이 기준이다. 전체 매트릭스는 엔진 런타임 전체를 바꿀 때만 — 비용(시간)을 사용자에게 알릴 것." ),
    ( ( "동시에 빌드", "다 같이 빌드", "에이전트 여러", "에이전트를 많이" ), "동시에 빌드하는 에이전트는 최대 둘이다(CPU 100 % · 캐시 미스). 넘기려면 사용자에게 속도 저하를 알릴 것." ),
    ( ( "CI 기다", "CI 확인하고", "CI 끝나면" ), "CI 는 기다리지 않는다 — 실패는 사용자가 알려 준다(docs/11_Workflow.md)." ),
    ( ( "WSL", ), "WSL 리눅스 검증은 사용자가 요청할 때만 한다." ),
    ( ( "별칭", "alias", "옛 형식" ), "별칭 · 옛 형식 리더는 두지 않는다(데이터를 다시 쓴다)." ),
]


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


def countBuildProcesses():
    """돌고 있는 ninja 수. 빌드 하나가 ninja 하나다."""
    try:
        if os.name == "nt":
            text = subprocess.run( [ "tasklist", "/FI", "IMAGENAME eq ninja.exe", "/FO", "CSV", "/NH" ],
                                   capture_output = True, text = True, timeout = 5 ).stdout
            return sum( 1 for line in text.splitlines() if line.lower().startswith( '"ninja.exe"' ) )
        text = subprocess.run( [ "pgrep", "-x", "ninja" ], capture_output = True, text = True, timeout = 5 ).stdout
        return len( text.split() )
    except Exception:
        return 0


def countWorktrees( root ):
    try:
        text = subprocess.run( [ "git", "-C", root, "worktree", "list", "--porcelain" ],
                               capture_output = True, text = True, timeout = 5 ).stdout
        return sum( 1 for line in text.splitlines() if line.startswith( "worktree " ) )
    except Exception:
        return 0


def inspectTranscript( path ):
    """(바이트 크기, 압축 횟수)."""
    try:
        size = os.path.getsize( path )
        compactCount = 0
        with open( path, "rb" ) as f:
            for line in f:
                if b'"compact_boundary"' in line:
                    compactCount += 1
        return size, compactCount
    except Exception:
        return 0, 0


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


def hardLimitStage( size, compactCount ):
    """넘은 단계 수(0 = 한도 아래). 압축이 한도 이상이면 최소 1단계."""
    stage = sum( 1 for step in kHardLimitStepsBytes if size >= step )
    return max( stage, 1 ) if compactCount >= kHardLimitCompactions else stage


def markerPath( markerDir, sessionId ):
    """세션 아이디가 파일 이름으로 안전하지 않으면 빈 글(차단하지 않는다)."""
    if not sessionId or not all( c.isalnum() or c in "-_" for c in sessionId ):
        return ""
    return os.path.join( markerDir, sessionId + ".json" )


def fileMtime( path ):
    try:
        return os.path.getmtime( path )
    except OSError:
        return 0


def readMarker( markerDir, sessionId ):
    """표시 내용(dict). 아이디가 없거나 · 표시가 없거나 · 손상됐으면 None."""
    path = markerPath( markerDir, sessionId )
    if not path or not os.path.exists( path ):
        return None
    try:
        with open( path, "r", encoding = "utf-8" ) as f:
            marker = json.load( f )
        int( marker[ "stage" ] )
        return marker
    except Exception:
        return None


def isHandoffPending( markerDir, sessionId, handoffPath ):
    """차단한 뒤 인계 문서가 아직 갱신되지 않았으면 True. 프롬프트는 더 막지 않고 모델에게 지시만 낸다."""
    marker = readMarker( markerDir, sessionId )
    return marker is not None and fileMtime( handoffPath ) <= marker.get( "handoffMtime", 0 )


def handoffInstruction():
    return ( f"한도: 세션이 길어 차단했다. 먼저 {kHandoffPath} 의 지금 상태에 (a) 진행 중인 일과 브랜치 · 워크트리, (b) 남은 일과 첫 일, "
             "(c) 사용자 결정 대기, (d) 병합 · 푸시하지 못한 변경, (e) 로드맵의 [병렬 가능] 표시를 확인해 다음 세션이 병렬로 띄울 묶음을 적고(끝난 항목은 지운다), 사용자에게 새 세션을 권한 뒤에 요청을 이어라." )


def shouldBlockPrompt( markerDir, sessionId, size, compactCount, handoffPath ):
    """이 세션에서 막은 적 없는 더 높은 단계에 닿았으면 표시를 남기고 그 단계(1~)를, 아니면 0 을 돌려준다.
    아이디가 없거나 · 표시가 손상됐거나 · 표시를 못 쓰면 0 — 잠그지 않는다."""
    stage = hardLimitStage( size, compactCount )
    path = markerPath( markerDir, sessionId )
    if stage == 0 or not path:
        return 0
    marker = readMarker( markerDir, sessionId )
    if marker is None and os.path.exists( path ):
        return 0    # 손상
    if marker is not None and stage <= int( marker[ "stage" ] ):
        return 0
    try:
        os.makedirs( markerDir, exist_ok = True )
        with open( path, "w", encoding = "utf-8" ) as f:
            json.dump( { "stage": stage, "size": size, "compactions": compactCount,
                         "blockedAt": time.time(), "handoffMtime": fileMtime( handoffPath ) }, f )
    except Exception:
        return 0
    return stage


def findMarkerDir( root ):
    try:
        text = subprocess.run( [ "git", "-C", root, "rev-parse", "--path-format=absolute", "--git-common-dir" ],
                               capture_output = True, text = True, timeout = 5 ).stdout.strip()
        return os.path.join( text, kHardBlockDirName ) if text else ""
    except Exception:
        return ""


def blockMessage( size, compactCount, stage ):
    return ( f"[세션 가드] 세션이 너무 깁니다({size / ( 1024 * 1024 ):.0f} MB, 압축 {compactCount} 회, {stage}/{len( kHardLimitStepsBytes )} 단계). "
             f"{kHandoffPath} 에 상태를 적고 새 세션을 시작하세요. 그래도 이어 가려면 같은 입력을 한 번 더 보내세요(이번 입력은 지워졌으니 다시 보내야 합니다)." )


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
        notes.append( "로드맵에서 [병렬 가능] 표시된 일: " + " / ".join( listParallel ) + " — 파일 소유가 겹치지 않으면 한 번에 병렬로 띄울 것(작업자 규칙)." )
    failingCi = readFailingCi( root )
    if failingCi:
        notes.append( failingCi )
    worktrees = countWorktrees( root )
    if worktrees > kMaxWorktrees:
        notes.append( f"워크트리가 {worktrees} 개 남아 있다(기준 {kMaxWorktrees}). 끝난 것은 Scripts/dev 정리 스크립트로 지울 것." )
    if notes:
        emit( "SessionStart", userMessage = "[세션 가드] " + " / ".join( notes ), modelContext = "\n".join( notes ) )


def onUserPromptSubmit( data, root ):
    userNotes = []
    modelNotes = []

    size, compactCount = inspectTranscript( data.get( "transcript_path", "" ) )
    mb = size / ( 1024 * 1024 )
    markerDir = findMarkerDir( root ) if hardLimitStage( size, compactCount ) else ""
    sessionId = data.get( "session_id", "" )
    handoffPath = os.path.join( root, kHandoffPath )
    stage = shouldBlockPrompt( markerDir, sessionId, size, compactCount, handoffPath ) if markerDir else 0
    if stage:
        sys.stderr.buffer.write( blockMessage( size, compactCount, stage ).encode( "utf-8" ) )
        sys.exit( kExitBlockPrompt )
    if markerDir and isHandoffPending( markerDir, sessionId, handoffPath ):
        modelNotes.append( handoffInstruction() )
    if size >= kTranscriptStrongBytes or compactCount >= 2:
        userNotes.append( f"대화가 매우 깁니다({mb:.0f} MB, 압축 {compactCount} 회). 인계 문서({kHandoffPath})를 쓰고 새 세션을 시작하는 편이 토큰이 덜 듭니다." )
        modelNotes.append( "한도: 대화가 매우 길다. 지금 하던 일을 끊기 좋은 곳에서 멈추고, docs/plans/NEXT.md 에 인계를 쓴 뒤 사용자에게 새 세션을 권할 것." )
    elif size >= kTranscriptWarnBytes or compactCount >= 1:
        userNotes.append( f"대화가 길어졌습니다({mb:.0f} MB, 압축 {compactCount} 회). 이번 묶음이 끝나면 새 세션을 권합니다." )
        modelNotes.append( "한도: 대화가 길다. 이번 묶음이 끝나면 인계 문서를 쓰고 사용자에게 새 세션을 권할 것." )

    builds = countBuildProcesses()
    if builds > kMaxConcurrentBuilds:
        userNotes.append( f"빌드가 {builds} 개 동시에 돌고 있습니다(기준 {kMaxConcurrentBuilds})." )
        modelNotes.append( f"규칙: 동시 빌드 {builds} 개 > {kMaxConcurrentBuilds}. 새 빌드를 띄우지 말고 사용자에게 알릴 것." )

    prompt = data.get( "prompt", "" ) or ""
    for keywordList, note in kRuleConflictList:
        if any( keyword in prompt for keyword in keywordList ):
            modelNotes.append( "규칙 단서: " + note + " 실제로 부딪히면 따르기 전에 사용자에게 한 줄로 알릴 것." )

    emit( "UserPromptSubmit",
          userMessage = ( "[세션 가드] " + " ".join( userNotes ) ) if userNotes else "",
          modelContext = "\n".join( modelNotes ) )


def onPreCompact( data, root ):
    emit( "PreCompact", userMessage = "[세션 가드] 컨텍스트 한도에 닿아 압축합니다. 묶음 경계라면 인계 문서를 쓰고 새 세션을 시작하는 편이 낫습니다." )


def onStopFailure( data, root ):
    reason = data.get( "error" ) or data.get( "reason" ) or data.get( "stop_reason" ) or ""
    emit( "StopFailure", userMessage = f"[세션 가드] API 오류로 멈췄습니다{(' (' + str( reason ) + ')') if reason else ''}. 사용량 한도라면 리셋 뒤 이어 가고, 길어진 세션이면 새 세션에서 docs/plans/NEXT.md 로 이어 가세요." )


def main():
    data = readInput()
    root = data.get( "cwd" ) or os.getcwd()
    handlers = {
        "SessionStart": onSessionStart,
        "UserPromptSubmit": onUserPromptSubmit,
        "PreCompact": onPreCompact,
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
