# 서버 디버깅 계획 — 에디터에서 서버 · 멀티플레이를 손쉽게 보는 길

> 상태: 구현중 · 선행: 없음 · 영역: network · 옛번호: D7-3 · 진행: 9/15

S3(디버그 채널 · 씬 검사 명령 · Inspector 탭)와 S1 의 게임 포트 샤딩은 끝났고, S6(권위 고스트 · 게임 화면 겹치기) · S7(틱별 해시 이력 · 클라이언트 쪽 기록)은 들어갔습니다. 남은 것은 각 단위 절에 있습니다.
방향 결정(여러 프로세스 먼저, 읽기 전용 디버그 채널, Multiplayer 창 하나)은 [결정 기록](../09_Decisions.md) 5-10 에 있습니다.

## 왜 필요한가

서버 버그는 클라이언트 버그보다 보기 어렵습니다. 같은 일을 겪는 흐름이 대체로 다음과 같습니다.

- 서버 프로세스, 클라이언트 프로세스 여럿을 손으로 띄우고 포트와 인자를 맞춥니다.
- 서버에서 중단점에 걸리면 클라이언트가 시간 초과로 끊깁니다. 디버깅하려면 연결이 살아 있어야 하는데, 멈추면 끊깁니다.
- 로그가 프로세스마다 갈라져 있어 한 사건의 앞뒤를 맞추기 어렵습니다.
- "서버에서는 어떻게 보였나"를 클라이언트 화면에서 볼 길이 없습니다.
- 회선이 나쁜 상황은 재현하기 어렵습니다.

상용 엔진의 답은 에디터 안의 재생 모드입니다. 언리얼은 PIE 의 Net Mode(Standalone · Listen · Client) · 플레이어 수 · Net Emulation 프리셋으로, 유니티는 Multiplayer Play Mode 의 가상 플레이어와 Network Simulator 로 푼다.
이 저장소에는 회선 흉내(`NetEmulation`), 한 프로세스 가상 서버(`NetSimHarness`), 전용 서버의 `pause` · `step`, 게임마다의 `GameNetSession`, 멀티 프로세스 재생(Multiplayer 창 Session 탭), 실행 중 회선 조절(Conditions 탭), 트래픽 그래프(Profiler 탭)가 이미 있습니다.

## 전용 창 — Multiplayer 창

단위 대부분이 "지금 접속 중인 세션"을 함께 봅니다. 단위마다 창을 따로 두면 어느 서버를 보는지가 갈라지므로, **Multiplayer 창 하나**에 탭으로 모읍니다(언리얼 Network Insights · 유니티 Multiplayer Play Mode 창 + Network Simulator 의 자리).

| 탭 | 내용 | 단위 |
|---|---|---|
| Session | Net Mode · 플레이어 수, 프로세스 목록(상태 · 종료 코드 · 마지막 로그), 서버 일시정지 · 한 틱 진행, 프로세스별 로그 필터 — 들어감 | S1 · S2 |
| Conditions | 프리셋 · 클라이언트별 슬라이더 · 끊기 · 재접속 · 스파이크 — 들어감 | S4 |
| Inspector | 전용 서버의 하이어라키 · 컴포넌트 값 · 원격 개발 콘솔(이름 자동완성), 주소로 붙기 — 들어감(`EditorRemoteInspector`, 시나리오 `netinspector.scenario.xml`) | S3 |
| Profiler | 바이트 · 패킷 · 재전송 · RTT · 손실의 시간축 그래프, 받은 메시지 종류별 비중 — 들어감 | S5 |
| Sync | 틱별 해시 이력 두 벌(세션 서버 · 다른 서버 · 저장한 파일)과 처음 어긋난 틱 — 들어감(`EditorStateHashCompare`, 시나리오 `netsync.scenario.xml`). 되감기가 남음 | S7 |

- 창 위쪽의 **대상 고르기**(서버 · 클라이언트 N)가 모든 탭에 같은 대상을 준다(들어감). 에디터가 띄운 세션이 없을 때는 Inspector 탭의 "주소로 붙기"로 실행 중인 서버에 붙는다(들어감).
- S6(권위 겹쳐 보기)은 창이 아니라 **게임 뷰 툴바의 보기 단추**다 — 화면 위에 겹쳐야 하므로.
- 탭은 `Source/Editor/Panels/Multiplayer/Multiplayer<이름>Tab.cpp` 하나에 `SW_EDITOR_MULTIPLAYER_TAB` 한 줄로 단다(들어감 — 사용법은 Editor README "멀티 프로세스 재생"). 확장 모듈도 같은 줄로 게임별 상태 보기(예: MMO 존 상태)를 더한다.

## 단위 (위가 아래의 바닥)

### S5 네트워크 프로파일러 — 남은 것

Profiler 탭(시간축 그래프 · 메시지 종류별 비중, `net.stats` 질의)은 들어갔습니다. 종류별 셈은 받은 쪽(`NetMessageRouter`)과 보낸 쪽(`NetHost::copySentKindTally` — 큐에 넣은 것)이 같은 표(`NetMessageKindTallyTable`)를 쓰고, 보고 줄은 `kinds=` · `sentKinds=` 둘을 싣습니다(탭의 "Sent").

- 트레이스의 메시지 흐름(2026-10-22, `wt/planleftA`, 빌드 미확인): **모든 채널**의 메시지가 보낸 곳 · 받은 곳에서 `NetFlowLog::record`(`Core/Network/Connection`, 프로세스 링 4096 칸)에 찍힌다.
  저장(`writeTraceFile`)이 그 구간의 흐름을 "Net messages" 줄에 1 us 구간 + 흐름 이벤트(`s`, `f` + `bp:e`)로 쓴다. 합친 트레이스(S1)에서 같은 id 가 화살표로 이어진다.
  링은 타임라인이 기록하는 동안만 남긴다(`ProfilerTimeline::setRecording` 이 `NetFlowLog::addRecorder` · `removeRecorder`). 링이 Network 층에 있는 까닭은 Core 층 규칙(네트워크는 진단을 include 하지 못한다)이다.
  id 는 양쪽이 따로 계산한다(`NetFlowLog::makeFlowID` — 연결 토큰 · 보낸 쪽 · 채널 · 번호). 찍는 곳은 `NetConnection::recordFlow` 하나이고, 연결 신원은 `NetHost` 가 연결이 맺어질 때 `setFlowIdentity` 로 준다.
  번호는 채널마다 양쪽이 같게 아는 값이다 — 신뢰 순서는 넣은 · 꺼낸 순번(넣을 때 · 꺼낼 때 찍는다), 신뢰 순서 없음은 선의 신뢰 ID(처음 보낼 때 · 받아들일 때), 비신뢰 · 순서만은 (패킷 번호 << 8 | 패킷 안 자리)(패킷을 쓸 때 · 읽을 때).
  그래서 선 형식에 칸을 더하지 않았다(Dev 와 Shipping 의 선 형식이 갈라지지 않는다).
  확인은 CoreTest `NetworkTest.EveryChannelPairsItsSendAndReceiveFlows`(네 채널 · 두 방향) · `ProfilerTimelineTest.NetFlowIDMatchesOnBothEndsAndSeparatesDirections` · `NetFlowsAreKeptOnlyWhileATimelineRecords`, EngineTest `NetFlowsAreWrittenAsBoundFlowEvents` 이다.
  남은 것: 실제 두 프로세스 합친 트레이스에서 화살표를 눈으로 보는 확인(`netplay.scenario.xml` + `-gv_traceSaveOnExit`).
- 스냅숏 크기 · 오브젝트별 복제 빈도(2026-10-24, `wt/plannet`, 빌드 전): 창구를 정했다 — 키트가 `GameNetSession::setReplicaStats( 이름, 갱신 수, 바이트 )` 로 누적 셈을 올리면 `net.stats` 줄의 `replicas=<이름>:<수>:<바이트>,…`(바이트가 큰 8 개)로 실린다. 이름은 `[a-z0-9_.-]` 만이고 올릴 이름은 32 개까지다(형식 밖 · 초과는 경고를 남기고 올리지 않는다). 확인 대기: 매니페스트 id `net-stats-replicas-tests`.
  Profiler 탭이 `replicas=` 를 두 보고의 차로 초당 갱신 · 바이트 · 비율 표로 보인다(`EditorNetProfilerUtil::computeReplicaRates`, 2026-10-25 `wt/serverdbg`, 빌드 전. 기준선 없는 이름 · 줄어든 값은 0. 2026-10-25 `wt/serverdbg2`: 누적 차 계산을 `EditorNetProfilerUtil::computeRate` 하나로 합쳤고, `GameNetSession::setReplicaStats` 는 올린 값을 그대로 싣는다는 약속(줄어든 값 · 상위 8 밖으로 밀렸다 돌아온 이름은 표에서 한 번 0)을 머리말에 적고 `GameNetSessionTest` 로 못 박았다). 확인 대기: 매니페스트 id `profiler-replica-rate-tests` · `game-net-session-replica-stats-tests`.
  상한 고르는 기준(2026-10-25 `wt/serverdbg3`, 빌드 전): 누적 바이트만 보면 오래 큰 이름이 8 칸을 채워 새로 활발해진 작은 이름이 표에 오르지 못했다. `GameNetSession::makeStatsReport` 가 앞 절반(4 칸)을 "지난 보고 뒤 늘어난 바이트"(표의 초당 바이트와 같은 기준) 순으로, 나머지를 누적 바이트 순으로 채운다. 지난 보고 때 값은 이름마다 세션이 들고(`ReplicaStat::_reportedByteCount`) 보고마다 앞으로 옮기므로 `makeStatsReport` 는 const 가 아니다(묻는 쪽은 Profiler 탭 하나). `GameNetSessionTest.ReplicaStatsCapKeepsRecentlyActiveSmallNames` 로 못 박았다(같은 매니페스트 항목의 `ReplicaStats*` 필터).
  한 줄 상한(2026-10-25 `wt/serverdbg4`): `net.stats` 줄(`kinds=` · `sentKinds=` · `replicas=`)은 아직 한 줄 상한(`SceneStateHash::kMaxReportLineBytes`, S7)을 쓰지 않는다 — 길이를 재지 않았고 쓰는 쪽에서 넘음을 막지 않는다(같은 결함 후보). 후속으로 `NetStatsReport` 쓰기도 같은 상수로 멈추게 한다.
  남은 것: 복제 키트(`ReplicationServer` · 파괴 복제 · MMO)가 틱마다 자기 합을 올리는 한 줄씩(셈이 이미 있는 `DestructionReplicationStats` 부터) — 키트 폴더라 이번에는 건드리지 않았다.
- 탭이 보이지 않을 때도 기록하기: 지금은 탭이 보일 때와 `netplay.profile` 동안만 묻는다. 세션 프레임 단계에서 늘 묻게 하면 창을 닫아도 남지만 자식 로그에 2 Hz 줄이 늘 쌓인다.
- 부하 봇(`Tools/OnlineLoadBot`)을 붙여 숫자가 오르는 시나리오(지금 확인은 `net.traffic` 부하의 `netprofiler.scenario.xml`).

### S6 권위 겹쳐 보기 — 남은 것

`AuthorityGhost`(`Source/Engine/Scene`)는 들어갔습니다. 클라이언트가 `-gv_authorityGhostPort=N -gv_authorityGhostToken=T` 로 서버의 디버그 채널에 붙어 `scene.poses` 를 4 Hz 로 읽고,
같은 이름의 오브젝트와 월드 위치 차이를 구해 서버 위치에 고스트 상자 · 어긋난 오브젝트에 선과 거리 글을 디버그 드로우 큐에 올립니다. 개발 명령 `net.ghost` 가 같은 차이를 글로 알립니다.
확인은 ServerTest `AuthorityGhostDeltaMatchesHowFarTheServerMovedAnObject`(서버에서 옮긴 거리 = 보고된 차이)와 실행 중인 App 의 `net.ghost`(차이 6.299 m 가 옮긴 거리와 맞음)입니다.

게임 화면 겹치기(2026-10-22): `-gv_debugDrawGameView=1` 이면 큐를 주 출력의 캔버스(UI 패스)에 얇은 사각형 · 글리프로 싣는다(`DebugDrawCanvasUtil` — 새 패스 · 셰이더 없음, 깊이 테스트 없음).
App 창 · 에디터 게임 뷰 · 스크린샷(`-gv_screenshot`)에 담긴다. 게임 뷰 툴바의 벌레 단추가 켜고 끄며 멀티 프로세스 재생 자식에게도 콘솔 줄로 보낸다. 구 펼치기는 씬 뷰와 같은 `DebugDrawQueue::appendVisibleSegments` 다.

- 눈으로 보는 확인: 클라이언트 App 을 `-gv_authorityGhostPort … -gv_debugDrawGameView=1 -gv_screenshot=…` 로 띄워 고스트 상자 · 선 · 거리 글이 캡처에 보이는지 네 백엔드로 본다(빌드 · 실행 전이다).
- 서버가 **실제로 가진** 위치를 읽는다(복제 키트가 마지막으로 알린 자세가 아니다). 복제 지연 · 보간 지연 숫자를 따로 보이려면 키트가 마지막 수신 자세를 `NetStatsReport` 처럼 싣는 창구를 정해야 한다.
- 회전 · 히트박스(랙 보상 기록) 고스트: 지금은 위치만.

### S7 어긋남 찾기 — 남은 것

`SceneStateHash` · `StateHashHistory`(`Source/Engine/Scene`)는 들어갔습니다. 서버는 `-gv_stateHashHistory=N` 이면 틱마다 씬 해시(이름 순으로 접은 이름 · 활성 · 월드 위치)를 남기고,
개발 명령 `state.hashes <from> <count>` 가 읽어 줍니다. `StateHashHistory::findFirstDivergence` 가 두 이력에서 처음 어긋난 틱을 찾습니다.
확인은 ServerTest `StateHashHistoryFindsTheFirstDivergentTick`(두 서버 중 하나의 오브젝트를 옮기면 처음 어긋난 틱이 옮긴 다음 틱으로 나옴)입니다.
Sync 탭(2026-10-21)은 세션이 띄운 전용 서버(`-gv_stateHashHistory=3600` 을 늘 준다) · 주소로 붙은 다른 서버 · 저장한 이력 파일을 A · B 칸에 담아 견줍니다. `parseReport` 는 형식이 다른 줄을 건너뛰지 않고 거절합니다.

클라이언트 쪽 기록(2026-10-22): 이력을 남기는 전용 서버가 틱마다 `GameNetSession::sendServerTick` 으로 틱 번호를 보내고(엔진 영역 `GameNetSessionMessage::kServerTick`, `NetWireVersion::kCore` 7),
App 클라이언트는 `-gv_stateHashHistory=N` 이면 씬이 프레임을 마친 뒤 받은 번호로 `StateHashHistory::recordActiveScene` 한다. 멀티 프로세스 재생은 클라이언트에도 이력 인자를 준다. 번호는 메시지가 닿은 때의 것이라 복제 지연이 있는 키트에서는 서버가 멈춘 동안이나 결정적 키트에서 두 이력이 맞는다.

Sync 탭의 클라이언트 원천(2026-10-22): 칸마다 세션 클라이언트 단추가 그 프로세스의 표준 입력에 `state.hashline 512` 를 묻고, 세션이 자식 로그에서 이력 한 줄(`StateHashHistory::kReportLinePrefix`)을 거둬 칸에 담는다.

- 키트별 해시: 파괴 해시 비교 · 롤백 세션 상태 바이트를 같은 이력에 다른 이름으로 더한다(지금은 씬 트랜스폼뿐).
- 어긋난 오브젝트 짚기(2026-10-24, `wt/plannet`, 빌드 전): 이력에 오브젝트별 해시를 쌓지 않고(틱 × 오브젝트라 크다) 어긋난 틱 근처에서 두 프로세스의 현재 값을 견준다. 개발 명령 `state.objects`(`<16자리 해시> <이름>` 줄)와 `SceneStateHash::collectObjects` · `parseObjectReport` · `findDifferingObjects` 가 들어갔다. 서버를 `pause` 한 채(결정적 키트는 같은 틱에서 멈춘 두 프로세스) 두 답을 견주면 다른 오브젝트 이름이 나온다. 확인 대기: 매니페스트 id `state-object-hash-tests`. Sync 탭의 "Which objects differ" 단추가 채널에서 읽은 두 칸의 서버에 `state.objects` 를 다시 물어 다른 오브젝트 이름을 목록으로 보인다(`EditorStateHashCompare::compareObjectsFromChannels`, 2026-10-25 `wt/serverdbg`, 빌드 전). 확인 대기: 매니페스트 id `sync-tab-object-diff-tests`. 2026-10-25 `wt/serverdbg2`: `state.objects` 답이 첫 줄에 이력의 최근 틱(`tick <n>`, `SceneStateHash::writeTickedObjectReport`)을 달고, `compareObjectReports` 가 틱이 없거나 두 서버의 틱이 다르면 거절한다(탭은 견준 틱을 보인다) — 해시 이력과 오브젝트 답이 다른 시각이라는 어긋남을 형식으로 막는다. 2026-10-25 `wt/serverdbg3`(빌드 전): 클라이언트 칸도 이어졌다. 표준 입력의 답은 로그 한 줄로만 나가 `state.objects` 같은 여러 줄 답을 거두는 길이 없어서(`EditorNetPlaySession::update` 가 줄 단위로 머리를 찾는다), 같은 값(틱 + 오브젝트별 해시)을 한 줄로 쓰는 `state.objectline`(`SceneStateHash::writeObjectReportLine` · `tryParseObjectReportLine` — 공백 · 쉼표 · 콜론 · `%` 는 `%XX`)을 더했고, 세션이 자식 로그에서 그 줄을 거둬 프로세스마다 마지막 것을 보관한다(`_listObjectHash` · `_objectHashTick`). Sync 탭은 칸이 클라이언트에서 읽힌 것이면(`EditorStateHashCompare::setClientOrigin`) 그 프로세스에 `state.objectline` 을 묻고 답을 기다려, 채널 칸의 답과 함께 `EditorStateHashCompare::compareObjects` 하나(틱 머리 검증 포함)로 견준다. 확인 대기: 매니페스트 id `state-object-hash-tests` · `sync-tab-object-diff-tests`(필터가 새 테스트를 덮는다). 2026-10-25 `wt/serverdbg4`(빌드 전): (1) 한 줄 답의 길이 상한 — 한 줄 답은 자식 로그 한 줄로 나가 부모 파이프 읽기를 거친다. 실제 상한은 POSIX 파이프 읽기의 줄 버퍼(`PosixProcess` `fgets` 4096 → 4095 바이트, 넘으면 한 줄이 둘로 쪼개진다)가 가장 작고, 로그 줄 버퍼(`AsyncLogSink` 8192 바이트, 머리 · 꼬리 포함, 넘으면 조용히 잘린다)와 Windows 읽기(상한 없이 이어 붙임)는 그보다 넉넉하다. 그래서 `SceneStateHash::kMaxReportLineBytes`(3500, 로그 머리 몫을 뺀 값) 하나를 `writeObjectReportLine` · `StateHashHistory::writeReportLine`(이제 `[[nodiscard]] bool`)과 두 `tryParse*` 가 같이 본다. 쓰는 쪽은 넘으면 오류 로그(id `State.ReportLineTooLong`)와 함께 false 로 멈추고(줄은 비운다), `state.objectline` · `state.hashline` 은 이를 실패 응답으로 낸다. 읽는 쪽도 넘는 줄은 거절한다. 이력은 `StateHashHistory::kMaxReportLineSamples`(90, 샘플 최대 38 바이트)까지만 싣고 `state.hashline` 은 더 많이 물으면 거절하며, Sync 탭의 클라이언트 질의는 512 → 90 틱이 되었다(짧은 창). 오브젝트 수가 많아 `state.objectline` 이 넘으면 클라이언트 칸은 쓸 수 없다(`state.objects` 는 여러 줄이라 서버 칸만). 확인 대기: 매니페스트 id `state-object-hash-tests`(새 테스트 `ALineOverTheLimitIsAnErrorOnBothSides` 를 필터가 덮는다). (2) 단추 시나리오 — 개발 명령 `netplay.sync objects server|client` 와 탐침 `Editor.NetPlaySyncObjectsCompared` · `Editor.NetPlaySyncDifferingObjects` 로 Sync 탭의 "Which objects differ" 와 같은 비교를 하는 `engine/automation/editor/netsyncobjects.scenario.xml`(서버를 `pause` 한 채 서버 칸 · 클라이언트 칸 모두). 확인 대기: 매니페스트 id `sync-tab-object-diff-scenario`. 남은 것: 창의 단추를 실제로 누르는 입력(EditorClick)은 Multiplayer 창을 여는 시나리오가 없어 다른 netplay 시나리오처럼 개발 명령으로 갈음했다. 탭은 클라이언트 질의가 오류 응답을 받으면 답을 기다린 채 남는다(답 수가 안 늘어난다) — 시간 초과가 없다. 2026-10-26 `wt/netstats`(빌드 전): 두 후보를 닫았다. (1) `NetStatsReport::makeLine` 이 줄을 만드는 유일한 길이라 거기서 `kMaxLineBytes`(3500, 같은 파일의 상수)를 강제한다 — 길이가 열린 칸은 복제 셈(이름 길이 제한 없음)뿐이라 넘으면 뒤의 항목을 빼고 줄 끝에 ` truncated=<원래 길이>` 를 붙이며, 읽는 쪽은 `_truncatedLineBytes` 로 되읽는다. (2) Sync 탭 질의(이력 · 오브젝트 해시)에 시간 상한을 두고 넘으면 대기를 끝내고 탭에 오류 글을 보인다. 값은 환경설정 섹션 `EditorSyncPreferences._clientReplyTimeoutSeconds`(기본 10 초, Editor/Multiplayer Sync). 확인 대기: 매니페스트 id `net-stats-replicas-tests`(새 테스트 `OverlongLineIsCappedAndMarkedTruncated`).
- 되감기: 의도 기록(`.swintent`)으로 그 틱까지 서버를 다시 돌려 한 틱씩 진행.

### S8 한 프로세스 가상 서버를 에디터에서

`NetSimHarness` 를 에디터 재생에 잇습니다. 서버 월드 + 클라이언트 월드 N 이 한 프로세스에서 돌고 월드마다 화면이 나옵니다. 결정적이고 한 디버거에서 서버 · 클라이언트에 모두 중단점을 걸 수 있다(언리얼 기본 PIE).
바닥은 다중 월드의 단계 1 ~ 4(미리보기 씬 · 씬마다 렌더러 · 호스트 씬 뷰 · 미리보기 뷰포트)다.

2026-10-22(`wt/planleftA`, 빌드 · 화면 미확인) 세 조각이 들어갔다.

1. 씬 공급자: `NetSimConfig::_pSceneSource`(`INetSimSceneSource`)를 주면 월드가 빌린 씬에서 돌고 `stop` 이 돌려준다. 못 빌리면 그 월드는 만들어지지 않는다(`initialize` false · `addClient` −1). 비우면 하니스가 씬을 만들어 가진다(화면 없는 테스트).
   에디터용 공급자는 `NetSimPreviewSceneSource`(키트) — `SceneManager` 미리보기 씬, 정책은 틱 없음(하니스가 틱한다) · 물리 켬 · 오디오 끔 · 플레이 없음. 확인: EngineTest `NetSimHarnessTest.WorldsRunOnScenesLentBySceneSource`.
2. `EditorNetSimSession` 과 Multiplayer 창의 **In process** 탭(새 에디터 확장 `GF_EditorPlugIn_NetSimulation`): 게임은 `NetSimGameRegistry` 에 올라온 것(키트 세션을 구현한 게임)만 고른다. 프레임 시간을 모아 고정 스텝으로 `step` 하고(한 프레임 4 스텝까지),
   월드마다 미리보기 뷰포트(`EditorPreviewViewport::attachScene` — 남의 씬을 붙여 그린다, 씬은 내리지 않는다)를 나란히 그린다. 탭이 세션을 가지므로 탭이 보일 때만 진행한다.
3. 입력 초점: `NetSimHarness::setInputFocus` · `NetSimWorld::hasInputFocus` — 한 번에 한 월드뿐이고, 탭에서 월드 칸을 클릭하면 옮겨 간다. 게임 세션은 이 값이 참일 때만 입력을 읽는다(계약). 확인: `NetSimHarnessTest.InputFocusBelongsToOneWorldAtATime`.

2026-10-22(`wt/inputdata`, Debug 빌드 · 테스트 · 시나리오 확인) 네 조각이 더 들어갔다.

4. 본보기 게임 `NetSimSampleGame`(`PlugIn/Editor/NetSimulation/Sample/`, 같은 확장 `GF_EditorPlugIn_NetSimulation` 이 목록에 등록한다): 서버 권위 아바타다. 게임 영역 메시지(`NetMessageRange::kGame`) 둘 — 클라이언트 → 서버 이동 입력, 서버 → 클라이언트 아바타 목록(둘 다 비신뢰, 낡은 틱은 받는 쪽이 버린다)이고, 복제 키트를 쓰지 않아 이 확장은 하니스 키트만 안다.
   연결마다 상자 하나(엔티티 id = 연결 id + 1)를 서버가 입력으로 움직이고, 클라이언트 월드는 받은 위치로 다가가며 그린다. 입력은 입력 초점이 있는 월드만 읽고 나머지는 0 이동을 보낸다(안 보내면 서버가 마지막 입력을 계속 쓴다).
   게임이 `INetSimGame` 을 구현하는 모양의 본보기다. 목록에 올리는 일은 모듈의 생성자 · 소멸자 쌍이 한다.
   설계 메모(2026-10-22): 처음에는 복제 키트(`ReplicationServer` · `ReplicationClient`) 위에 짰으나 에디터 확장은 자기 키트만 include 할 수 있어(`CheckGameFrameworkLayers`) 하니스 키트와 복제 키트를 함께 쓰면 층 위반이었고, 복제 키트를 쓰는 게임에만 확장이 켜지는 부작용도 있었다. 복제 키트 위의 실제 게임 구현은 게임 모듈의 일이다(아래 남은 것).
   테스트 `NetSimSampleGameTest.*` 넷(초점이 있는 클라이언트의 아바타만 움직이고 두 클라이언트 월드가 따라옴 · 벽에서 멈춤 · 클라이언트가 떠나면 아바타가 사라짐 · 등록 · 카메라).
5. 카메라 창구: `INetSimGame::describePreviewFraming`(`NetSimPreviewFraming` — 오빗 카메라와 바닥 크기)을 게임이 알리면 `EditorNetSimSession::findViewport` 가 그 카메라로 잡고 조명 환경(방향광 · 하늘 · 바닥)을 월드 씬에 더한다. 안 알리면 기본 카메라 그대로이고 환경을 더하지 않는다. 클라이언트 폰 카메라를 따르는 보기는 아직 아니다(고정 오빗).
6. 에디터 자동화: 탭의 단추 · 월드 뷰포트와 Multiplayer 창의 탭 제목에 자동화 레이블(`multiplayer.tab.<id>` · `multiplayer.inprocess.start|stop|world<N>`), 탐침 `Editor.NetSimRunning` · `NetSimTick` · `NetSimWorldCount` · `NetSimInputFocus` · `NetSimViewportCount` · `NetSimObjectX|Z`(target = 월드 번호, key = 오브젝트 이름 — 게임을 가리지 않는다).
7. 시나리오 `engine/automation/editor/netsim.scenario.xml`: 시작(클라이언트 둘 + 서버), 세 월드가 같은 아바타를 같은 위치에 그림, 클라이언트 1 에서 D 를 누르면 아바타 1 만 오른쪽으로 가고 두 클라이언트 월드가 같은 값을 보임(`sameAs`), 클라이언트 2 뷰포트를 눌러 초점을 옮기고 W 로 아바타 2 만 앞으로, 멈춤. **네 백엔드(DX12 · Vulkan · DX11 · OpenGL) Debug 에서 통과**했고 DX12 의 캡처 셋(`Saved/Automation/editor.netsim/netsim/`)을 눈으로 봤다 — 세 월드가 모두 바닥 · 하늘 · 상자 둘이고 이동 뒤 같은 위치다. **DX12 는 세 번 중 한 번 `[RHI] [D3D12] 온라인 디스크립터 블록이 바닥났습니다 (64×512)` 오류 줄로 졌다**(같은 줄이 previewsky 디퍼드 DX12 에서도 난다 — 미리보기 렌더러가 여럿인 에디터의 DX12 디스크립터 블록 부족, 렌더러 담당의 일이다).

남은 것:
- 게임 모듈이 `INetSimGame` 을 구현한 실제 게임은 아직 없다(본보기뿐). Shooter3D 의 네트워크 몸 이동(`ShooterNetPawnComponent`)은 전역 `GameNetSession` 을 찾으므로, 월드마다 세션을 받게 바꾸는 일이 먼저다.
- 캡처를 눈으로 본 것은 DX12 뿐이다(다른 세 백엔드는 시나리오 통과만 확인).
- 아바타 상자가 작게 보인다(아레나 전체가 보이는 거리) — 월드 뷰포트 크기에 따른 거리 조정은 하지 않았다.

## 운영 단위 (실제 운용에 도움이 되는 것 — 디버깅 단위 S1 ~ S5 뒤에 순서를 정한다)

디버깅과 달리 서버가 이미 떠 있는 뒤의 일입니다. 조각이 흩어져 있어 서버 실행 파일에 잇는 일이 많습니다.

| 단위 | 내용 | 바닥 · 연결 |
|---|---|---|
| O1 사고 기록 | 서버 크래시 보고(스택 · 마지막 로그 · 접속 수 · 틱 시간을 한 폴더에 — `bugit` 모양), 마지막 N 틱의 입력 · 메시지 종류별 바이트 · 틱 시간을 담는 **프레임 이력 링 버퍼**(크래시 · 틱 초과 때 덤프), 느린 틱 자동 캡처는 Dev 구성 서버에 있다(`-gv_traceSlowTickMs=N` — 그 틱 앞뒤를 트레이스로; Shipping 서버는 지표 `server_tick_seconds` 만, 결정 기록 5-10). 서버용 동의는 운영 설정으로 | `OperationsAndDeployment`(옛 H-3, [Candidates-network](Candidates-network.md)), `FrameProfiler` |
| O2 로그 | 서버 로그 JSON lines 출력 장치, 감사 줄의 추적 id 칸 | `OperationsAndDeployment`(옛 H-3, [Candidates-network](Candidates-network.md)) |
| O3 종료 · 드레인 | 새 접속을 막고 진행 중인 매치 · 거래를 끝낸 뒤 내리기, 종료 신호에서 상태 저장 + 끊김 사유를 클라이언트에 알림 | `Server` 종료 신호 처리 |
| O4 관리 채널 | 재시작 없이 설정 값 바꾸기 · 접속자 목록 · 내보내기 · 제재 · 점검 모드 · 공지. **S3 의 디버그 채널과 같은 채널 · 토큰 · 루프백 규칙을 쓴다**(중복을 만들지 않는다). 설정 파일을 다시 읽는 항목과 못 읽는 항목의 표 | `GF_Admin` · 원격 설정 · 기능 플래그 |
| O5 부하 기준선 | Release 서버 한 대에 봇 1000 · 5000 으로 수용 연결 수 · 틱 p99 · 메시지 p99 를 측정해서 기준선을 남긴다. 틱 사용률이 설정 비율(예 70 %)을 넘으면 `/metrics` 와 로그에 경고, 대시보드 예시(JSON) | `Tools/OnlineLoadBot`, `/metrics` |
| O6 배포 | 컨테이너 파일 · 쿠버네티스 매니페스트 예시(`/readyz` 로 상태 확인, 신호 → 드레인), Agones 할당 연결, 빌드 판 협상(프로토콜 판이 다르면 접속 전에 "업데이트 필요"), 비밀(TLS · 서명 키)을 환경 변수 · 비밀 저장소에서 읽는 길과 교체 절차 | `OperationsAndDeployment`(옛 H-3, [Candidates-network](Candidates-network.md)) |
| O7 운영에서 개발로 | 운영 서버가 기록한 `.swintent` 를 에디터 Sync 탭(S7)에서 열어 같은 틱을 재생 — 운영 사고를 개발에서 재현하는 가장 직접적인 길 | S7, 의도 기록 |

순서(운영 가치 ÷ 비용): O1 → O2 + O3 → O5 → O4 → O6 → O7. O1 의 링 버퍼는 비용이 거의 없고 사후 분석에 가장 값싼 도구다. 단위마다 확인은 자동 시나리오(서버 프로세스를 띄워 크래시 · 종료 신호 · 느린 틱을 일으키는 테스트)로 한다.

## 순서

S6 · S7 의 남은 것(눈 확인 · Sync 탭 클라이언트 원천 · 키트별 해시) → S8 의 남은 것(S2 중단점 친화 · S1 멀티 프로세스 재생 · S4 회선 조절 · S5 프로파일러 탭은 끝났다 — S5 의 남은 것은 그 절에). 나머지는 S1 의 세션 위에서 확인한다 — 에디터 시나리오 `engine/automation/editor/netplay.scenario.xml` 이 바닥이다. S3 · S5 는 디버그 HUD 의 섹션 레지스트리와 같은 원천을 쓴다.

## 지키는 것

디버그 채널 · 원격 명령의 Dev 전용 · 루프백 · 토큰 규칙과 서버 코드가 에디터에 의존하지 않는 규칙은 결정 기록 5-10 에 있습니다. 단위마다 확인은 자동 시나리오로 합니다(사람 손 확인을 돌리지 않는다).
