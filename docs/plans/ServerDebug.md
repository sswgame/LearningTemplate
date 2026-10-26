# 서버 디버깅 계획 — 에디터에서 서버 · 멀티플레이를 손쉽게 보는 길

> 상태: 구현중 · 선행: 없음 · 영역: network · 옛번호: D7-3 · 진행: 9/15

S1 ~ S4 · S6 · S7 의 바닥은 들어갔고 남은 것은 각 단위 절에 있습니다.
방향 결정(여러 프로세스 먼저, 읽기 전용 디버그 채널, Multiplayer 창 하나)은 [결정 기록](../09_Decisions.md) 5-10 에 있습니다.

## 왜 필요한가

서버 버그는 클라이언트 버그보다 보기 어렵습니다. 프로세스 여럿을 손으로 띄워 포트를 맞추고, 서버 중단점에서 클라이언트가 시간 초과로 끊기고, 로그가 프로세스마다 갈라지고, 서버가 본 모습을 클라이언트 화면에서 볼 길이 없고, 나쁜 회선은 재현하기 어렵습니다.
상용 엔진의 답은 에디터 안의 재생 모드다(언리얼 PIE Net Mode · Net Emulation, 유니티 Multiplayer Play Mode · Network Simulator).
이 저장소에는 회선 흉내(`NetEmulation`), 한 프로세스 가상 서버(`NetSimHarness`), 전용 서버의 `pause` · `step`, `GameNetSession`, 멀티 프로세스 재생, 실행 중 회선 조절, 트래픽 그래프가 이미 있습니다.

## 전용 창 — Multiplayer 창

단위 대부분이 "지금 접속 중인 세션"을 함께 보므로 창 하나에 탭으로 모읍니다(언리얼 Network Insights · 유니티 Multiplayer Play Mode 창의 자리).

| 탭 | 내용 | 단위 |
|---|---|---|
| Session | Net Mode · 플레이어 수, 프로세스 목록, 서버 일시정지 · 한 틱 진행, 프로세스별 로그 필터 — 들어감 | S1 · S2 |
| Conditions | 프리셋 · 클라이언트별 슬라이더 · 끊기 · 재접속 · 스파이크 — 들어감 | S4 |
| Inspector | 전용 서버의 하이어라키 · 컴포넌트 값 · 원격 개발 콘솔, 주소로 붙기 — 들어감(`EditorRemoteInspector`, `netinspector.scenario.xml`) | S3 |
| Profiler | 바이트 · 패킷 · 재전송 · RTT · 손실의 시간축 그래프, 메시지 종류별 비중 — 들어감 | S5 |
| Sync | 틱별 해시 이력 두 벌과 처음 어긋난 틱 — 들어감(`EditorStateHashCompare`, `netsync.scenario.xml`). 되감기가 남음 | S7 |
| In process | 한 프로세스 가상 서버(S8) | S8 |

- 창 위쪽의 대상 고르기가 모든 탭에 같은 대상을 준다. 세션이 없으면 Inspector 탭의 "주소로 붙기"로 실행 중인 서버에 붙는다.
- S6(권위 겹쳐 보기)은 화면 위에 겹쳐야 하므로 창이 아니라 게임 뷰 툴바의 보기 단추다.
- 탭은 `Source/Editor/Panels/Multiplayer/Multiplayer<이름>Tab.cpp` 하나에 `SW_EDITOR_MULTIPLAYER_TAB` 한 줄로 단다(Editor README "멀티 프로세스 재생"). 확장 모듈도 같은 줄로 게임별 상태 보기를 더한다.

## 단위 (위가 아래의 바닥)

### S5 네트워크 프로파일러 — 남은 것

Profiler 탭과 `net.stats` 질의, 종류별 셈(`NetMessageKindTallyTable`, 줄의 `kinds=` · `sentKinds=`), 트레이스의 메시지 흐름(`NetFlowLog`, 모든 채널, 같은 id 가 합친 트레이스에서 화살표로 이어짐)은 들어갔습니다. 선 형식에 칸을 더하지 않고 양쪽이 같은 번호를 따로 계산하는 것이 설계 결정입니다. 링은 Core 층 규칙(네트워크는 진단을 include 하지 못한다) 때문에 Network 층에 있습니다.
오브젝트별 복제 빈도: 키트가 `GameNetSession::setReplicaStats( 이름, 갱신 수, 바이트 )` 로 누적 셈을 올리면 `net.stats` 의 `replicas=` 로 실리고(이름 `[a-z0-9_.-]`, 32 개까지), Profiler 탭이 초당 표로 보인다(`EditorNetProfilerUtil::computeReplicaRates`). 한 줄에는 바이트가 큰 8 개만 싣되 앞 4 칸은 지난 보고 뒤 늘어난 바이트 순, 나머지는 누적 순으로 채운다(오래 큰 이름이 새로 활발해진 이름을 가리지 않게).

- 눈으로 보는 확인: 두 프로세스 합친 트레이스의 화살표(`netplay.scenario.xml` + `-gv_traceSaveOnExit`). 확인 대기: 매니페스트 id `net-stats-replicas-tests` · `profiler-replica-rate-tests` · `game-net-session-replica-stats-tests`.
- 한 줄 답의 상한은 Core 의 `DebugReplyLine::kMaxBytes`(3500) 하나다(`Core/Network/DebugReplyLine.h`, 2026-10-26 `wt/linecap` · `wt/netstats`, 빌드 전). 줄일 수 있는 꼬리가 있는 줄(`net.stats`)은 `DebugReplyLine::makeCapped`(`NetStatsReport::makeLine` 이 유일한 길 — 넘으면 뒤의 항목을 빼고 ` truncated=<원래 길이>` 를 붙이고 읽는 쪽이 `_truncatedLineBytes` 로 되읽음), 길이가 정해진 줄(`net.emulation stats`)은 `makeFixed` 가 만들고, 줄일 수 없는 줄(`state.objectline` · `state.hashline`)은 `fits` 로 재서 넘으면 오류로 멈춘다(S7). Sync 탭 질의(이력 · 오브젝트 해시)는 시간 상한(`EditorSyncPreferences._clientReplyTimeoutSeconds`, 기본 10 초)을 넘으면 대기를 끝내고 오류 글을 보인다. 확인 대기: 매니페스트 id `net-stats-replicas-tests`(새 테스트 `DebugReplyLineTest.*` · `OverlongLineIsCappedAndMarkedTruncated`).
- 복제 키트(`ReplicationServer` · 파괴 복제 · MMO)가 틱마다 자기 합을 올리는 한 줄씩(셈이 있는 `DestructionReplicationStats` 부터).
- 탭이 보이지 않을 때도 기록하기: 지금은 탭이 보일 때와 `netplay.profile` 동안만 묻는다. 늘 묻게 하면 자식 로그에 2 Hz 줄이 쌓인다.
- 부하 봇(`Tools/OnlineLoadBot`)을 붙여 숫자가 오르는 시나리오.

### S6 권위 겹쳐 보기 — 남은 것

`AuthorityGhost`(`Source/Engine/Scene`)와 게임 화면 겹치기(`-gv_debugDrawGameView=1`, `DebugDrawCanvasUtil`, 게임 뷰 툴바 벌레 단추)는 들어갔습니다. 클라이언트가 `-gv_authorityGhostPort=N -gv_authorityGhostToken=T` 로 디버그 채널에 붙어 `scene.poses` 를 4 Hz 로 읽고 서버 위치 고스트 · 어긋난 거리 선을 그립니다. 개발 명령 `net.ghost` 가 차이를 글로 알립니다(ServerTest `AuthorityGhostDeltaMatchesHowFarTheServerMovedAnObject`).

- 눈으로 보는 확인: 클라이언트 App 을 `-gv_authorityGhostPort … -gv_debugDrawGameView=1 -gv_screenshot=…` 로 띄워 캡처에 고스트가 보이는지 네 백엔드로 본다.
- 서버가 실제로 가진 위치를 읽는다(복제 키트가 마지막으로 알린 자세가 아니다). 복제 · 보간 지연 숫자는 키트가 마지막 수신 자세를 `NetStatsReport` 처럼 싣는 창구를 정해야 한다.
- 회전 · 히트박스(랙 보상 기록) 고스트: 지금은 위치만.

### S7 어긋남 찾기 — 남은 것

`SceneStateHash` · `StateHashHistory`(`Source/Engine/Scene`): 서버는 `-gv_stateHashHistory=N` 이면 틱마다 씬 해시를 남기고 `state.hashes <from> <count>` 가 읽어 줍니다. `findFirstDivergence` 가 처음 어긋난 틱을 찾습니다. Sync 탭은 세션 서버 · 다른 서버 · 저장한 이력 파일을 A · B 칸에 견줍니다(`parseReport` 는 형식이 다른 줄을 거절).
클라이언트 쪽 기록: 전용 서버가 틱마다 `GameNetSession::sendServerTick` 으로 틱 번호를 보내고, 클라이언트가 씬 프레임 뒤 `recordActiveScene` 한다. 클라이언트 칸은 자식 표준 입력에 `state.hashline` 을 묻고 로그의 한 줄(`kReportLinePrefix`)을 거둔다.
어긋난 오브젝트 짚기: 이력에 오브젝트별 해시를 쌓지 않고(틱 × 오브젝트라 크다) 어긋난 틱 근처에서 두 프로세스의 현재 값을 견준다. `state.objects`(서버, 여러 줄)와 `state.objectline`(클라이언트용 한 줄)이 `tick <n>` 머리를 달고, `compareObjects` 가 틱이 없거나 다르면 거절한다(해시 이력과 오브젝트 답이 다른 시각이라는 어긋남을 형식으로 막는다). Sync 탭의 "Which objects differ" 단추와 `netplay.sync objects server|client` · `netsyncobjects.scenario.xml` 이 있다.
한 줄 답의 상한: POSIX 파이프 읽기의 줄 버퍼(`fgets` 4096)가 가장 작아 `kMaxReportLineBytes`(3500)를 쓰기 · 읽기가 같이 본다(넘으면 오류 id `State.ReportLineTooLong`로 멈추고 읽는 쪽도 거절). 이력은 90 샘플까지, 오브젝트가 많아 `state.objectline` 이 넘으면 클라이언트 칸은 쓸 수 없다.
확인 대기: 매니페스트 id `state-object-hash-tests` · `sync-tab-object-diff-tests` · `sync-tab-object-diff-scenario`.

- 키트별 해시: 파괴 해시 비교 · 롤백 세션 상태 바이트를 같은 이력에 다른 이름으로 더한다(지금은 씬 트랜스폼뿐).
- 탭은 클라이언트 질의가 오류 응답을 받으면 답을 기다린 채 남는다(시간 초과 없음). 창의 단추를 실제로 누르는 입력(EditorClick)은 Multiplayer 창을 여는 시나리오가 없어 개발 명령으로 갈음했다.
- 되감기: 의도 기록(`.swintent`)으로 그 틱까지 서버를 다시 돌려 한 틱씩 진행.

### S8 한 프로세스 가상 서버를 에디터에서

`NetSimHarness` 를 에디터 재생에 잇습니다. 서버 월드 + 클라이언트 월드 N 이 한 프로세스에서 돌고 월드마다 화면이 나옵니다. 결정적이고 한 디버거에서 모두 중단점을 걸 수 있다(언리얼 기본 PIE). 바닥은 다중 월드 단계 1 ~ 4.

들어간 것(2026-10-22): 씬 공급자(`NetSimConfig::_pSceneSource`, 에디터용 `NetSimPreviewSceneSource`), `EditorNetSimSession` 과 Multiplayer 창 **In process** 탭(`GF_EditorPlugIn_NetSimulation`, 월드마다 미리보기 뷰포트를 나란히), 입력 초점(`setInputFocus` — 한 번에 한 월드, 게임 세션은 참일 때만 입력을 읽는 계약), 본보기 게임 `NetSimSampleGame`, 카메라 창구(`describePreviewFraming`), 에디터 자동화 레이블 · 탐침, 시나리오 `engine/automation/editor/netsim.scenario.xml`(네 백엔드 Debug 통과, DX12 캡처는 눈으로 봄).
설계 결정: 본보기 게임은 복제 키트를 쓰지 않고 게임 메시지 둘(입력 · 아바타 목록)만 쓴다 — 에디터 확장은 자기 키트만 include 할 수 있어(`CheckGameFrameworkLayers`) 하니스 키트와 복제 키트를 함께 쓰면 층 위반이다. 복제 키트 위의 실제 게임 구현은 게임 모듈의 일이다.

남은 것:
- 게임 모듈이 `INetSimGame` 을 구현한 실제 게임은 아직 없다. Shooter3D 의 `ShooterNetPawnComponent` 는 전역 `GameNetSession` 을 찾으므로 월드마다 세션을 받게 바꾸는 일이 먼저다.
- 캡처를 눈으로 본 것은 DX12 뿐이다. 클라이언트 폰 카메라를 따르는 보기는 아직 고정 오빗이고, 월드 뷰포트 크기에 따른 거리 조정이 없어 아바타가 작게 보인다.
- DX12 는 세 번 중 한 번 `[RHI] [D3D12] 온라인 디스크립터 블록이 바닥났습니다 (64×512)` 로 진다(미리보기 렌더러가 여럿인 에디터의 디스크립터 블록 부족, 렌더러 담당의 일).

## 운영 단위 (실제 운용에 도움이 되는 것 — 디버깅 단위 S1 ~ S5 뒤에 순서를 정한다)

디버깅과 달리 서버가 이미 떠 있는 뒤의 일입니다. 조각이 흩어져 있어 서버 실행 파일에 잇는 일이 많습니다.

| 단위 | 내용 | 바닥 · 연결 |
|---|---|---|
| O1 사고 기록 | 서버 크래시 보고(스택 · 마지막 로그 · 접속 수 · 틱 시간을 한 폴더에 — `bugit` 모양), 마지막 N 틱의 입력 · 메시지 종류별 바이트 · 틱 시간을 담는 **프레임 이력 링 버퍼**(크래시 · 틱 초과 때 덤프), 느린 틱 자동 캡처는 Dev 서버에 있다(`-gv_traceSlowTickMs=N`; Shipping 은 지표 `server_tick_seconds` 만, 결정 기록 5-10). 서버용 동의는 운영 설정으로 | `OperationsAndDeployment`(옛 H-3, [Candidates-network](Candidates-network.md)), `FrameProfiler` |
| O2 로그 | 서버 로그 JSON lines 출력 장치, 감사 줄의 추적 id 칸 | `OperationsAndDeployment`(옛 H-3) |
| O3 종료 · 드레인 | 새 접속을 막고 진행 중인 매치 · 거래를 끝낸 뒤 내리기, 종료 신호에서 상태 저장 + 끊김 사유를 클라이언트에 알림 | `Server` 종료 신호 처리 |
| O4 관리 채널 | 재시작 없이 설정 값 바꾸기 · 접속자 목록 · 내보내기 · 제재 · 점검 모드 · 공지. **S3 의 디버그 채널과 같은 채널 · 토큰 · 루프백 규칙을 쓴다**(중복을 만들지 않는다). 설정 파일을 다시 읽는 항목과 못 읽는 항목의 표 | `GF_Admin` · 원격 설정 · 기능 플래그 |
| O5 부하 기준선 | Release 서버 한 대에 봇 1000 · 5000 으로 수용 연결 수 · 틱 p99 · 메시지 p99 를 측정해 기준선을 남긴다. 틱 사용률이 설정 비율(예 70 %)을 넘으면 `/metrics` 와 로그에 경고, 대시보드 예시(JSON) | `Tools/OnlineLoadBot`, `/metrics` |
| O6 배포 | 컨테이너 파일 · 쿠버네티스 매니페스트 예시(`/readyz`, 신호 → 드레인), Agones 할당 연결, 빌드 판 협상(프로토콜 판이 다르면 접속 전에 "업데이트 필요"), 비밀(TLS · 서명 키)을 환경 변수 · 비밀 저장소에서 읽는 길과 교체 절차 | `OperationsAndDeployment`(옛 H-3) |
| O7 운영에서 개발로 | 운영 서버가 기록한 `.swintent` 를 에디터 Sync 탭(S7)에서 열어 같은 틱을 재생 | S7, 의도 기록 |

순서(운영 가치 ÷ 비용): O1 → O2 + O3 → O5 → O4 → O6 → O7. O1 의 링 버퍼는 비용이 거의 없고 사후 분석에 가장 값싼 도구다. 확인은 서버 프로세스를 띄워 크래시 · 종료 신호 · 느린 틱을 일으키는 자동 시나리오로 한다.

## 순서

S6 · S7 의 남은 것 → S8 의 남은 것 → S5 의 남은 것. 나머지는 S1 의 세션 위에서 확인한다(`engine/automation/editor/netplay.scenario.xml` 이 바닥). S3 · S5 는 디버그 HUD 의 섹션 레지스트리와 같은 원천을 쓴다.

## 지키는 것

디버그 채널 · 원격 명령의 Dev 전용 · 루프백 · 토큰 규칙과 서버 코드가 에디터에 의존하지 않는 규칙은 결정 기록 5-10 에 있습니다. 확인은 자동 시나리오로 합니다.
