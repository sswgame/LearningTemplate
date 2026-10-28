# 서버 디버깅 계획 — 에디터에서 서버 · 멀티플레이를 손쉽게 보는 길

> 상태: 구현중 · 선행: 없음 · 영역: network · 옛번호: D7-3 · 진행: 9/15

2026-10-28: 기능별 테스트 · 시나리오(`Test/` · `Resource/**/automation/`)를 새 프레임워크를 만들려고 전부 지웠다. 이 문서의 테스트 · 시나리오 · `ctest` · `AppTestUtil` 언급은 옛 구조를 가리키며, 새 틀이 정해지면 다시 쓴다.

S1 ~ S4 · S6 · S7 의 바닥은 들어갔고 남은 것은 각 단위 절에 있습니다.

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
| Sync | 틱별 해시 이력 두 벌과 처음 어긋난 틱 — 들어감(`EditorStateHashCompare`, `netsync.scenario.xml`). 키트별 줄(`destruction` · `rollback`)을 고를 수 있음. 되감기가 남음 | S7 |
| In process | 한 프로세스 가상 서버(S8) | S8 |

- 창 위쪽의 대상 고르기가 모든 탭에 같은 대상을 준다. 세션이 없으면 Inspector 탭의 "주소로 붙기"로 실행 중인 서버에 붙는다.
- S6(권위 겹쳐 보기)은 화면 위에 겹쳐야 하므로 창이 아니라 게임 뷰 툴바의 보기 단추다.
- 탭은 `Source/Editor/Panels/Multiplayer/Multiplayer<이름>Tab.cpp` 하나에 `SW_EDITOR_MULTIPLAYER_TAB` 한 줄로 단다(Editor README "멀티 프로세스 재생"). 확장 모듈도 같은 줄로 게임별 상태 보기를 더한다.

## 단위 (위가 아래의 바닥)

### S5 네트워크 프로파일러 — 남은 것

Profiler 탭과 `net.stats` 질의, 종류별 셈(`NetMessageKindTallyTable`, 줄의 `kinds=` · `sentKinds=`), 트레이스의 메시지 흐름(`NetFlowLog`, 모든 채널, 같은 id 가 합친 트레이스에서 화살표로 이어짐)은 들어갔습니다. 선 형식에 칸을 더하지 않고 양쪽이 같은 번호를 따로 계산하는 것이 설계 결정입니다. 링은 Core 층 규칙(네트워크는 진단을 include 하지 못한다) 때문에 Network 층에 있습니다.
오브젝트별 복제 빈도: 키트가 `GameNetSession::setReplicaStats( 이름, 갱신 수, 바이트 )` 로 누적 셈을 올리면 `net.stats` 의 `replicas=` 로 실리고(이름 `[a-z0-9_.-]`, 32 개까지), Profiler 탭이 초당 표로 보인다(`EditorNetProfilerUtil::computeReplicaRates`). 한 줄에는 바이트가 큰 8 개만 싣되 앞 4 칸은 지난 보고 뒤 늘어난 바이트 순, 나머지는 누적 순으로 채운다(오래 큰 이름이 새로 활발해진 이름을 가리지 않게).

- 눈으로 보는 확인: 두 프로세스 합친 트레이스의 화살표(`netplay.scenario.xml` + `-gv_traceSaveOnExit`). 확인 대기:.
- 한 줄 답의 상한은 Core 의 `DebugReplyLine::kMaxBytes`(3500) 하나다(`Core/Network/DebugReplyLine.h`, 2026-10-26 `wt/linecap` · `wt/netstats`, 빌드 전). 줄일 수 있는 꼬리가 있는 줄(`net.stats`)은 `DebugReplyLine::makeCapped`(`NetStatsReport::makeLine` 이 유일한 길 — 넘으면 뒤의 항목을 빼고 ` truncated=<원래 길이>` 를 붙이고 읽는 쪽이 `_truncatedLineBytes` 로 되읽음), 길이가 정해진 줄(`net.emulation stats`)은 `makeFixed` 가 만들고, 줄일 수 없는 줄(`state.objectline` · `state.hashline`)은 `fits` 로 재서 넘으면 오류로 멈춘다(S7). Sync 탭 질의(이력 · 오브젝트 해시)는 시간 상한(`EditorSyncPreferences._clientReplyTimeoutSeconds`, 기본 10 초)을 넘으면 대기를 끝내고 오류 글을 보인다. .
- 키트별 복제 통계(들어감, 빌드 전): 파괴 복제(`DestructionNetSessionComponent`, 이름 `destruction` — 사건 · 자세 · 멈춤 · 스냅숏 · 해시 메시지 수와 보낸 바이트)와 페어드 애니메이션(`PairedAnimationNetSessionComponent`, 이름 `paired_animation` — 바이트는 세지 않아 0)이 틱마다 `setReplicaStats` 로 자기 합을 올린다. 클라이언트는 보낸 양이 없어 서버 쪽 값이 주로 오른다. `ReplicationServer` 와 MMO 복제는 키트에 셈 구조가 없어 올리지 않았다(셈을 먼저 만들어야 한다).
- 탭이 보이지 않을 때도 기록하기(들어감, 빌드 전): `IMultiplayerTab::updateWhileHidden` 을 창이 열려 있고 그 탭이 고른 탭이 아닐 때 부르고, Profiler 탭의 "Record while hidden" 체크(기본 꺼짐 — 자식 로그에 2 Hz 줄이 쌓인다)가 켜져 있으면 `net.stats` 를 묻는다. 창을 닫으면 기록하지 않는다. 번역 키 `EditorMultiplayer.ProfilerRecordHidden` 은 `editor.strings.xml` 에만 더했다(번역 파일의 sourceHash 는 글 수집 도구가 채운다 — 다음 수집에서 `ko` 를 채운다).
- 부하 봇을 붙인 시나리오는 만들지 않았다: 봇(`Tools/OnlineLoadBot`)은 온라인 서버(TCP)를 치는데 Profiler 는 게임 세션(UDP)의 `net.stats` 를 보므로 봇을 붙여도 숫자가 오르지 않는다고 판단했다(코드로 확인한 것은 봇의 인자 `--server=` · `--local-server` 뿐이다). 숫자가 오르는 확인은 `net.traffic` 이 하는 `netprofiler.scenario.xml` 이 맡는다. 봇이 "online kit 'matchmaking' could not initialize" 로 서던 것은 봇 `Main.cpp` 가 `ServerApp` 처럼 호스트 범위 + `Engine`(DedicatedServer)을 세워 고쳤다(`wt/loadbot`, 빌드 전). 빌드 뒤의 확인은 `Scripts/qa/Load.py` 의 loadbot 구동기(`--local-server`)가 한다.

### S6 권위 겹쳐 보기 — 남은 것

`AuthorityGhost`(`Source/Engine/Scene`)와 게임 화면 겹치기(`-gv_debugDrawGameView=1`, `DebugDrawCanvasUtil`, 게임 뷰 툴바 벌레 단추)는 들어갔습니다. 클라이언트가 `-gv_authorityGhostPort=N -gv_authorityGhostToken=T` 로 디버그 채널에 붙어 `scene.poses` 를 4 Hz 로 읽고 서버 위치 고스트 · 어긋난 거리 선을 그립니다. 개발 명령 `net.ghost` 가 차이를 글로 알립니다(ServerTest `AuthorityGhostDeltaMatchesHowFarTheServerMovedAnObject`).

- 눈으로 보는 확인: 클라이언트 App 을 `-gv_authorityGhostPort … -gv_debugDrawGameView=1 -gv_screenshot=…` 로 띄워 캡처에 고스트가 보이는지 네 백엔드로 본다.
- 서버가 실제로 가진 위치: `scene.poses` 가 서버 씬의 월드 트랜스폼을 직접 읽으므로(복제 키트가 마지막으로 알린 자세가 아니다) 이미 그렇다. 복제 · 보간 지연 숫자는 키트가 마지막 수신 자세를 `NetStatsReport` 처럼 싣는 창구를 정해야 하고, 아직 없다.
- 회전 · 히트박스(들어감, 빌드 전): `scene.poses` 줄이 `name x y z [rot qx qy qz qw] [box cx cy cz hx hy hz]` 로 늘었고(옛 형식도 읽는다 — `parsePoses` 가 뒤에서부터 떼어 낸다, 쓰기는 `writePoses`), `AuthorityDelta` 에 회전 각도 차 · 상자 중심 거리가 더해졌다. 고스트는 서버의 상자를 서버 회전으로 돌려 그리고 앞쪽(+Z) 화살표를 보이며, 위치가 같아도 각도가 어긋나면 선 · 글(`… deg`)을 낸다. 히트박스는 오브젝트가 선언한 월드 상자(`getWorldBox`)이다 — 엔진이 키트의 랙 보상 몸(`LagRecord`, 위치 + 반지름)을 알 수 없어 그 몸 자체는 그리지 못한다(키트가 같은 줄 형식으로 몸을 싣는 창구가 필요하다). 새 테스트 `AuthorityGhostTest.RoundTripsRotationAndHitBoxAndComputesAngle` 가 형식 · 각도 계산을 덮는다(아직 돌리지 않았다 — 통합의 서버 테스트 묶음이 돌린다).

### S7 어긋남 찾기 — 남은 것

`SceneStateHash` · `StateHashHistory`(`Source/Engine/Scene`): 서버는 `-gv_stateHashHistory=N` 이면 틱마다 씬 해시를 남기고 `state.hashes <from> <count>` 가 읽어 줍니다. `findFirstDivergence` 가 처음 어긋난 틱을 찾습니다. Sync 탭은 세션 서버 · 다른 서버 · 저장한 이력 파일을 A · B 칸에 견줍니다(`parseReport` 는 형식이 다른 줄을 거절).
클라이언트 쪽 기록: 전용 서버가 틱마다 `GameNetSession::sendServerTick` 으로 틱 번호를 보내고, 클라이언트가 씬 프레임 뒤 `recordActiveScene` 한다. 클라이언트 칸은 자식 표준 입력에 `state.hashline` 을 묻고 로그의 한 줄(`kReportLinePrefix`)을 거둔다.
어긋난 오브젝트 짚기: 이력에 오브젝트별 해시를 쌓지 않고(틱 × 오브젝트라 크다) 어긋난 틱 근처에서 두 프로세스의 현재 값을 견준다. `state.objects`(서버, 여러 줄)와 `state.objectline`(클라이언트용 한 줄)이 `tick <n>` 머리를 달고, `compareObjects` 가 틱이 없거나 다르면 거절한다(해시 이력과 오브젝트 답이 다른 시각이라는 어긋남을 형식으로 막는다). Sync 탭의 "Which objects differ" 단추와 `netplay.sync objects server|client` · `netsyncobjects.scenario.xml` 이 있다.
한 줄 답의 상한: POSIX 파이프 읽기의 줄 버퍼(`fgets` 4096)가 가장 작아 `kMaxReportLineBytes`(3500)를 쓰기 · 읽기가 같이 본다(넘으면 오류 id `State.ReportLineTooLong`로 멈추고 읽는 쪽도 거절). 이력은 90 샘플까지, 오브젝트가 많아 `state.objectline` 이 넘으면 클라이언트 칸은 쓸 수 없다.
Sync 탭 시나리오(`netsyncobjects`)는 통합 S 의 `app-scenarios-release` 통과로 덮여 항목을 지웠다.

- 키트별 해시(들어감, 빌드 전): 이력에 이름 붙은 줄을 더한다 — 키트가 `IStateHashProvider` 를 구현해 `StateHashHistory::registerChannel( 이름, &제공자 )` 로 등록하면(구독이 풀려도 이력은 남고 같은 이름으로 이어 쓴다, 8 줄까지) 이력이 틱을 남길 때(`recordActiveScene` → `recordChannels`) 같은 호스트 틱에 제공자를 부른다. 제공자가 틱 번호를 바꿀 수 있다. `destruction` 줄은 `DestructionNetSession`(파괴 구조 해시를 번호 순으로 접음, 컴포넌트가 이력이 켜져 있으면 등록)이, `rollback` 줄은 `RollbackSession`(확정 프레임 시작 전 상태 바이트의 해시, 틱 = 프레임 번호 — 게임이 등록하며 쓰는 게임은 아직 없다)이 낸다. `state.hashes <from> <count> [줄]` · `state.hashline <count> [줄]` · `state.hashnames` 가 읽고, Sync 탭의 줄 이름 칸과 `netplay.sync channel <이름>` 이 읽을 줄을 고른다(서버 · 클라이언트 칸이 같은 이름을 쓰고 비교 규칙은 그대로다). 오브젝트별 짚기(`state.objects`)는 씬 줄만이다. 새 테스트 `StateHashReportLineTest.KitChannelsRecordPerHostTickAndKeepHistoryAfterTheSubscriptionEnds` · `NetLockstepTest.RollbackPredictsRewindsAndConvergesOnBothSides` 의 롤백 줄 확인이 덮는다(아직 돌리지 않았다).
- 탭은 클라이언트 질의가 오류 응답을 받으면 답을 기다린 채 남는다(시간 초과 없음). 창의 단추를 실제로 누르는 입력(EditorClick)은 Multiplayer 창을 여는 시나리오가 없어 개발 명령으로 갈음했다.
- 되감기(못 함): 의도 기록(`.swintent`)으로 그 틱까지 서버를 다시 돌려 한 틱씩 진행. 재생 쪽 바닥(`IntentPlayback` · `intent.play` · `step N`)은 있으나, 새 서버를 처음부터 멈춘 채로 띄우는 길이 없어(`ServerApp` 에 `-gv_` 시작 일시정지 인자가 없다 — 명령은 기동 뒤 표준 입력으로 가므로 그 사이 틱이 돈다) 목표 틱에서 정확히 멈출 수 없다. 서버 코드(`Source/Server`)에 그 인자를 더하고, 에디터가 `EditorNetPlaySession` 으로 재생 서버 하나를 따로 띄워 `intent.play` → `step` 하는 순서가 다음 일이다.

### S8 한 프로세스 가상 서버를 에디터에서

`NetSimHarness` 를 에디터 재생에 잇습니다. 서버 월드 + 클라이언트 월드 N 이 한 프로세스에서 돌고 월드마다 화면이 나옵니다. 결정적이고 한 디버거에서 모두 중단점을 걸 수 있다(언리얼 기본 PIE). 바닥은 다중 월드 단계 1 ~ 4.

들어간 것(2026-10-22): 씬 공급자(`NetSimConfig::_pSceneSource`, 에디터용 `NetSimPreviewSceneSource`), `EditorNetSimSession` 과 Multiplayer 창 **In process** 탭(`GF_EditorPlugIn_NetSimulation`, 월드마다 미리보기 뷰포트를 나란히), 입력 초점(`setInputFocus` — 한 번에 한 월드, 게임 세션은 참일 때만 입력을 읽는 계약), 본보기 게임 `NetSimSampleGame`, 카메라 창구(`describePreviewFraming`), 에디터 자동화 레이블 · 탐침, 시나리오 `engine/automation/editor/netsim.scenario.xml`(네 백엔드 Debug 통과, DX12 캡처는 눈으로 봄).
설계 결정: 본보기 게임은 복제 키트를 쓰지 않고 게임 메시지 둘(입력 · 아바타 목록)만 쓴다 — 에디터 확장은 자기 키트만 include 할 수 있어(`CheckGameFrameworkLayers`) 하니스 키트와 복제 키트를 함께 쓰면 층 위반이다. 복제 키트 위의 실제 게임 구현은 게임 모듈의 일이다.

남은 것:
- 게임 모듈이 `INetSimGame` 을 구현한 실제 게임은 아직 없다(2026-10-28 에 시도하지 않았다 — 눈으로 · 빌드로 확인할 수 없는 큰 개편이라 막은 곳만 적는다). Shooter3D 의 `ShooterNetPawnComponent` 는 `onBeginPlay`(89 줄 근처)와 `stepServer`(189 줄 근처)에서 전역 `game::findSubsystem<GameNetSession>()` 으로 호스트(`getPeers`) · 라우터(`getRouter`) · 역할(`getRole`)을 받는다. 월드마다 하나씩 받게 하려면 이 셋을 한 값(예: 호스트 · 라우터 · 역할을 묶은 "넷 링크")으로 묶어 컴포넌트가 자기 오브젝트 매니저에서 찾게 하고, 전역 세션 쪽과 `NetSimWorld` 쪽이 각자 그 값을 매니저에 올리는 순서가 맞다(전역 폴백을 두지 않는다). 그 뒤 게임 모듈이 월드마다 아레나 씬 · 폰 스폰 · 입력 초점 읽기를 하는 `INetSimGame` 을 구현하고 자기 모듈에서 `NetSimGameRegistry::registerGame` 한다(본보기 `NetSimSampleGame` 과 같은 구조).
- 캡처를 눈으로 본 것은 DX12 뿐이다. 클라이언트 폰 카메라를 따르는 보기는 아직 고정 오빗이고, 월드 뷰포트 크기에 따른 거리 조정이 없어 아바타가 작게 보인다.

## 운영 단위 (실제 운용에 도움이 되는 것 — 디버깅 단위 S1 ~ S5 뒤에 순서를 정한다)

디버깅과 달리 서버가 이미 떠 있는 뒤의 일입니다. 조각이 흩어져 있어 서버 실행 파일에 잇는 일이 많습니다.

| 단위 | 내용 | 바닥 · 연결 |
|---|---|---|
| O1 사고 기록 | 서버 크래시 보고(스택 · 마지막 로그 · 접속 수 · 틱 시간을 한 폴더에 — `bugit` 모양), 마지막 N 틱의 입력 · 메시지 종류별 바이트 · 틱 시간을 담는 **프레임 이력 링 버퍼**(크래시 · 틱 초과 때 덤프), 느린 틱 자동 캡처는 Dev 서버에 있다(`-gv_traceSlowTickMs=N`; Shipping 은 지표 `server_tick_seconds` 만). 서버용 동의는 운영 설정으로 | `OperationsAndDeployment`(옛 H-3, [Candidates-network](Candidates-network.md)), `FrameProfiler` |
| O2 로그 | 서버 로그 JSON lines 출력 장치, 감사 줄의 추적 id 칸 | `OperationsAndDeployment`(옛 H-3) |
| O3 종료 · 드레인 | 새 접속을 막고 진행 중인 매치 · 거래를 끝낸 뒤 내리기, 종료 신호에서 상태 저장 + 끊김 사유를 클라이언트에 알림 | `Server` 종료 신호 처리 |
| O4 관리 채널 | 재시작 없이 설정 값 바꾸기 · 접속자 목록 · 내보내기 · 제재 · 점검 모드 · 공지. **S3 의 디버그 채널과 같은 채널 · 토큰 · 루프백 규칙을 쓴다**(중복을 만들지 않는다). 설정 파일을 다시 읽는 항목과 못 읽는 항목의 표 | `GF_Admin` · 원격 설정 · 기능 플래그 |
| O5 부하 기준선 | Release 서버 한 대에 봇 1000 · 5000 으로 수용 연결 수 · 틱 p99 · 메시지 p99 를 측정해 기준선을 남긴다. 틱 사용률이 설정 비율(예 70 %)을 넘으면 `/metrics` 와 로그에 경고, 대시보드 예시(JSON) | `Tools/OnlineLoadBot`, `/metrics` |
| O6 배포 | 컨테이너 파일 · 쿠버네티스 매니페스트 예시(`/readyz`, 신호 → 드레인), Agones 할당 연결, 빌드 판 협상(프로토콜 판이 다르면 접속 전에 "업데이트 필요"), 비밀(TLS · 서명 키)을 환경 변수 · 비밀 저장소에서 읽는 길과 교체 절차 | `OperationsAndDeployment`(옛 H-3) |
| O7 운영에서 개발로 | 운영 서버가 기록한 `.swintent` 를 에디터 Sync 탭(S7)에서 열어 같은 틱을 재생 | S7, 의도 기록 |

순서(운영 가치 ÷ 비용): O1 → O2 + O3 → O5 → O4 → O6 → O7. O1 의 링 버퍼는 비용이 거의 없고 사후 분석에 가장 값싼 도구다. 확인은 서버 프로세스를 띄워 크래시 · 종료 신호 · 느린 틱을 일으키는 자동 시나리오로 한다.

### 운영 단위 진행 (2026-10-28 — 코드와 테스트는 썼고 빌드 · 테스트는 돌리지 않았다)

정한 순서는 위와 같고 O4 · O7 은 아래 이유로 남겼다(O4 는 S3 디버그 채널 위에 얹는 큰 일이고 O7 은 S7 에 기댄다).

| 단위 | 한 것 | 남은 것 |
|---|---|---|
| O1 | `ServerFrameHistory`(최근 N 틱의 틱 시간 · 접속 수 · 밀린 틱 링 버퍼, 설정 `_frameHistoryTicks`)와 느린 틱 덤프(`_slowTickDumpMs` → `Saved/ServerReports/frames_<세션>_<틱>_slow_tick.txt`, 덤프 순간의 메시지 종류별 누적 셈 한 줄 포함, 10 초 간격 제한). 크래시 보고에 `ServerTick` 문맥(마지막 틱 시간 · 접속 수)을 초마다 갱신해 붙인다. 크래시 묶기는 이미 있었다 | 크래시 폴더 전체를 한 폴더로 묶는 `bugit` 모양(스택 · 마지막 로그 · 틱 이력을 같은 폴더에)과 서버용 보내기 동의(운영 설정) — 서버용 보고 경로가 먼저 필요. 틱 이력의 크래시 시점 덤프(죽어 가는 프로세스에서 파일을 쓰지 않으려고 문맥 한 줄로 갈음했다) |
| O2 | `JSONLinesLogOutput`(`ILogSink::enableJSONLinesOutput`, 설정 `_bJSONLogs`). 칸: `time` · `ts` · `level` · `tag` · `caller` · `tick` · `trace` · `acct` · `id` · `msg` · `file` · `line`. 켜면 콘솔 장치는 꺼진다(`ConsoleLogOutput::setMuted`) | 감사 줄의 전용 추적 id 칸은 `trace`(로그 문맥의 요청 추적 id)로 이미 실린다 — 감사 줄이 문맥 안에서 쓰이는지 확인이 남았다. OpenTelemetry 내보내기 |
| O3 | 종료 신호(`Terminate` · `ServiceStop`) → 즉시 `/readyz` 503 → 접속(서비스 연결 수)이 0 이 되거나 `_drainWaitSeconds` 까지 틱을 계속 돌린 뒤 정상 종료. 콘솔 · Ctrl+C · 테스트 틱 상한은 기다리지 않는다 | 게임 UDP 접속은 연결 수에 안 든다(서비스 연결만 센다). 진행 중인 매치 · 거래의 끝을 키트에 묻는 길과 끊김 사유를 클라이언트에 알리는 일은 키트의 종료 API 가 먼저다 |
| O5 | 틱 사용률(`server_tick_utilization_ratio` 지표, 설정 `_tickLoadWarnPercent` 넘으면 1 분에 한 번 경고), 대시보드 예시 `deploy/dashboards/server-overview.json` | 봇 1000 · 5000 측정과 기준선(측정 항목 — `Candidates-network.md` 의 매니페스트 초안이 있다) |
| O6 | `deploy/docker/Dockerfile`, `deploy/kubernetes/server.yaml`(`/readyz` · `/healthz` 검사, 종료 유예, 비밀은 `secretKeyRef`), `deploy/README.md`(종료 · 드레인, 비밀 교체 절차) | Agones 연결(클라우드 계정 · SDK 가 필요해 하지 않았다), 빌드 판 협상, 서명 키 이중 키 기간 |
| O4 · O7 | 하지 않음 | O4: S3 디버그 채널의 Shipping 판 · 인증을 먼저 정한 뒤. O7: S7 의 Sync 탭이 서야 한다(다른 작업자 몫) |

## 순서

S6 · S7 의 남은 것 → S8 의 남은 것 → S5 의 남은 것. 나머지는 S1 의 세션 위에서 확인한다(`engine/automation/editor/netplay.scenario.xml` 이 바닥). S3 · S5 는 디버그 HUD 의 섹션 레지스트리와 같은 원천을 쓴다.

## 지키는 것

디버그 채널 · 원격 명령은 Dev 전용 · 루프백 · 토큰을 요구하고, 서버 코드는 에디터에 의존하지 않는다. 확인은 자동 시나리오로 합니다.
