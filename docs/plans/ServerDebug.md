# 서버 디버깅 계획 — 에디터에서 서버 · 멀티플레이를 손쉽게 보는 길

> 상태: 구현중 · 선행: 없음 · 영역: network · 옛번호: D7-3 · 진행: 4/15

S3(디버그 채널 · 씬 검사 명령 · Inspector 탭)는 끝났고, S6 · S7 의 프로세스 쪽(권위 고스트 · 틱별 해시 이력)은 들어갔습니다. 남은 것은 각 단위 절에 있습니다.
방향 결정(여러 프로세스 먼저, 읽기 전용 디버그 채널, Multiplayer 창 하나)은 [결정 기록](../09_Decisions.md) 5-10 에 있습니다.
S1 에서 남은 것은 아래 "남은 것 — S1" 절에 있습니다.

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

### S1 세션 — 남은 것

게임 포트 샤딩: 전용 서버는 이제 서버 설정의 게임 포트를 늘 열지만(결정 기록 5-10) 소켓 하나뿐이라 `ServerConfig::_gamePortShardCount` 2 이상은 오류로 열지 않습니다(샤드마다 NetHost · 로그인 토큰의 샤드 포트).
App · `GameNetSession`(`Engine/Net` — 엔진 수명이라 게임 모듈 리로드를 넘어 연결이 남는다) · 엔진 몫이라 2026-10-17 네트워크 작업(서버 · Core 네트워크 · 온라인 파일 소유)에서 남겼습니다. 같은 일이 `UdpSocketLayerScaling`(후보 문서 [Candidates-network](Candidates-network.md))의 포트 샤딩입니다. 운영 단위 O1 ~ O7 은 `OperationsAndDeployment`(같은 후보 문서, 옛 H-3)에 있습니다.

### S5 네트워크 프로파일러 — 남은 것

Profiler 탭(시간축 그래프 · 받은 메시지 종류별 비중, `net.stats` 질의)은 들어갔습니다. 사용법은 Editor README "멀티 프로세스 재생" 에 있습니다.

- 트레이스에 메시지 흐름을 남긴다: 보낼 때 · 받을 때 같은 메시지 id 로 흐름 이벤트(`s` · `f`)를 타임라인에 기록하면, 합친 트레이스(S1)에서 뷰어가 프로세스 사이 화살표를 그린다.
  합치기는 흐름 이벤트를 그대로 옮긴다 — 남은 것은 타임라인에 흐름 사건 칸을 두고 송수신 지점에서 찍는 일이다.
- 스냅숏 크기 · 오브젝트별 복제 빈도: 공통 원천이 없다(복제 키트마다 따로 센다 — 예 `DestructionReplicationStats`). 키트가 셈을 `NetStatsReport` 에 싣는 창구를 정한 뒤 그래프를 더한다.
- 보낸 쪽 메시지 종류별 셈: 지금 비중은 받은 쪽(`NetMessageRouter` 의 종류별 셈)이다. 보내는 길(`NetHost::sendMessage`)에 셈을 두면 한 프로세스만 보고도 올림 · 내림을 나눠 볼 수 있다.
- 탭이 보이지 않을 때도 기록하기: 지금은 탭이 보일 때와 `netplay.profile` 동안만 묻는다. 세션 프레임 단계에서 늘 묻게 하면 창을 닫아도 남지만 자식 로그에 2 Hz 줄이 늘 쌓인다.
- 부하 봇(`Tools/OnlineLoadBot`)을 붙여 숫자가 오르는 시나리오(지금 확인은 `net.traffic` 부하의 `netprofiler.scenario.xml`).

### S6 권위 겹쳐 보기 — 남은 것

`AuthorityGhost`(`Source/Engine/Scene`)는 들어갔습니다. 클라이언트가 `-gv_authorityGhostPort=N -gv_authorityGhostToken=T` 로 서버의 디버그 채널에 붙어 `scene.poses` 를 4 Hz 로 읽고,
같은 이름의 오브젝트와 월드 위치 차이를 구해 서버 위치에 고스트 상자 · 어긋난 오브젝트에 선과 거리 글을 디버그 드로우 큐에 올립니다. 개발 명령 `net.ghost` 가 같은 차이를 글로 알립니다.
확인은 ServerTest `AuthorityGhostDeltaMatchesHowFarTheServerMovedAnObject`(서버에서 옮긴 거리 = 보고된 차이)와 실행 중인 App 의 `net.ghost`(차이 6.299 m 가 옮긴 거리와 맞음)입니다.

- 디버그 드로우 큐는 **에디터 씬 뷰만 그립니다**(`EditorVisualizerGeometry`) — 플레이어 App 의 게임 화면과 에디터 게임 뷰는 큐를 그리지 않는다. 게임 뷰 위에 겹치려면 게임 뷰에도 큐를 그리는 길이 필요하다.
- 고스트가 실제로 그려지는지 눈으로 보는 확인: 에디터 씬 뷰 오버레이는 시나리오 스크린샷(렌더 결과)에 잡히지 않는다. 오버레이를 캡처하는 길이나 큐 항목 수 탐침이 필요하다.
- 게임 뷰 툴바의 보기 단추: 지금은 실행 인자만 있다.
- 서버가 **실제로 가진** 위치를 읽는다(복제 키트가 마지막으로 알린 자세가 아니다). 복제 지연 · 보간 지연 숫자를 따로 보이려면 키트가 마지막 수신 자세를 `NetStatsReport` 처럼 싣는 창구를 정해야 한다.
- 회전 · 히트박스(랙 보상 기록) 고스트: 지금은 위치만.

### S7 어긋남 찾기 — 남은 것

`SceneStateHash` · `StateHashHistory`(`Source/Engine/Scene`)는 들어갔습니다. 서버는 `-gv_stateHashHistory=N` 이면 틱마다 씬 해시(이름 순으로 접은 이름 · 활성 · 월드 위치)를 남기고,
개발 명령 `state.hashes <from> <count>` 가 읽어 줍니다. `StateHashHistory::findFirstDivergence` 가 두 이력에서 처음 어긋난 틱을 찾습니다.
확인은 ServerTest `StateHashHistoryFindsTheFirstDivergentTick`(두 서버 중 하나의 오브젝트를 옮기면 처음 어긋난 틱이 옮긴 다음 틱으로 나옴)입니다.
Sync 탭(2026-10-21)은 세션이 띄운 전용 서버(`-gv_stateHashHistory=3600` 을 늘 준다) · 주소로 붙은 다른 서버 · 저장한 이력 파일을 A · B 칸에 담아 견줍니다. `parseReport` 는 형식이 다른 줄을 건너뛰지 않고 거절합니다.

- 클라이언트 쪽 기록: 기록은 `ServerApp` 에만 있다. 클라이언트(App) 가 서버 틱 번호에 맞춰 같은 해시를 남기는 길(`GameNetSession` 의 틱 번호)이 있어야 서버와 클라이언트를 비교한다.
- 키트별 해시: 파괴 해시 비교 · 롤백 세션 상태 바이트를 같은 이력에 다른 이름으로 더한다(지금은 씬 트랜스폼뿐).
- 어긋난 오브젝트 짚기: 지금은 틱만 나온다. 해시를 오브젝트별로 나눠 보관하거나, 어긋난 틱에서 `object.dump` 두 벌을 비교하는 길이 필요하다.
- 되감기: 의도 기록(`.swintent`)으로 그 틱까지 서버를 다시 돌려 한 틱씩 진행.

### S8 한 프로세스 가상 서버를 에디터에서 (장기)

`NetSimHarness` 를 에디터 재생에 잇습니다. 서버 월드 + 클라이언트 월드 N 이 한 프로세스에서 돌고 에디터 탭마다 클라이언트 화면이 나옵니다.

- 결정적이고 한 디버거에서 서버 · 클라이언트에 모두 중단점을 걸 수 있다(언리얼 기본 PIE).
- 선행 조건: **다중 월드 · 월드별 렌더**(EditorMultiWorld). 지금 엔진은 활성 씬 하나만 틱하고 그린다.
- 이유: S1 로 거의 모든 일이 되므로, 다중 월드가 다른 이유로 들어온 뒤에 얹는다.

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

S6 · S7 의 남은 것(게임 뷰 겹치기 · 클라이언트 쪽 해시 기록) → S8(S2 중단점 친화 · S1 멀티 프로세스 재생 · S4 회선 조절 · S5 프로파일러 탭은 끝났다 — S5 의 남은 것은 그 절에). 나머지는 S1 의 세션 위에서 확인한다 — 에디터 시나리오 `engine/automation/editor/netplay.scenario.xml` 이 바닥이다. S3 · S5 는 디버그 HUD 의 섹션 레지스트리와 같은 원천을 쓴다.

## 지키는 것

디버그 채널 · 원격 명령의 Dev 전용 · 루프백 · 토큰 규칙과 서버 코드가 에디터에 의존하지 않는 규칙은 결정 기록 5-10 에 있습니다. 단위마다 확인은 자동 시나리오로 합니다(사람 손 확인을 돌리지 않는다).
