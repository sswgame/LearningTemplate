# 서버 디버깅 계획 — 에디터에서 서버 · 멀티플레이를 손쉽게 보는 길

> 상태: 구현중 · 선행: 없음 · 영역: network · 진행: 9/15

서버 버그는 클라이언트 버그보다 보기 어렵다. 프로세스 여럿을 손으로 띄워 포트를 맞추고, 서버 중단점에서 클라이언트가 시간 초과로 끊기고, 로그가 프로세스마다 갈라지고, 서버가 본 모습을 클라이언트 화면에서 볼 길이 없고, 나쁜 회선은 재현하기 어렵다. 상용 엔진의 답은 에디터 안의 재생 모드다(언리얼 PIE Net Mode · Net Emulation, 유니티 Multiplayer Play Mode · Network Simulator). 이 저장소에는 회선 흉내, 한 프로세스 가상 서버, 전용 서버의 `pause` · `step`, 멀티 프로세스 재생, 실행 중 회선 조절, 트래픽 그래프가 이미 있고, 그 위에 **Multiplayer 창**(탭 모음)과 운영 도구를 얹는다. 디버깅 단위(S1 ~ S8)는 바닥까지 들어갔고, 운영 단위(O1 ~ O7)는 코드를 썼으나 빌드 확인 전이다.

## 진행

| 단위 | 내용 | 상태 |
|---|---|---|
| S1 | Session 탭 — Net Mode · 플레이어 수, 프로세스 목록, 프로세스별 로그 필터 | 끝 |
| S2 | 서버 일시정지 · 한 틱 진행 | 끝 |
| S3 | Inspector 탭 — 전용 서버의 하이어라키 · 컴포넌트 값 · 원격 개발 콘솔, 주소로 붙기(`EditorRemoteInspector`) | 끝 |
| S4 | Conditions 탭 — 프리셋 · 클라이언트별 슬라이더 · 끊기 · 재접속 · 스파이크 | 끝 |
| S5 | Profiler 탭 — 바이트 · 패킷 · 재전송 · RTT · 손실 시간축, 메시지 종류별 비중, 복제 빈도 | 끝(남은 것은 아래) |
| S6 | 권위 겹쳐 보기(서버 위치 고스트) | 끝(눈 확인 · 일부 빌드 전) |
| S7 | Sync 탭 — 틱별 해시 이력 두 벌과 처음 어긋난 틱 | 끝(되감기 코드 끝, 빌드 전) |
| S8 | In process 탭 — 한 프로세스 가상 서버 | 끝(실제 게임 구현은 남음) |
| O1 | 사고 기록 — 프레임 이력 링 버퍼 · 느린 틱 덤프 | 코드 끝, 빌드 전. 크래시 폴더 묶기 · 서버용 동의 남음 |
| O2 | 서버 로그 JSON lines | 끝(빌드 전) |
| O3 | 종료 · 드레인 | 코드 끝, 빌드 전. 키트 종료 API 남음 |
| O4 | 관리 채널(재시작 없는 설정 · 제재 · 점검) | 남음(S3 디버그 채널의 Shipping 판 · 인증을 먼저 정한 뒤) |
| O5 | 부하 기준선 | 코드 끝, 빌드 전. 봇 1000 · 5000 측정 남음 |
| O6 | 배포(컨테이너 · 쿠버네티스 예시) | 일부(Agones · 빌드 판 협상 · 이중 키 기간 남음) |
| O7 | 운영에서 개발로 — 운영 서버의 의도 기록(`.swintent`)을 Sync 탭에서 재생 | 코드 끝, 빌드 전(S7 되감기와 같은 길) |

**첫 일:** 빌드한 뒤 S5~S7 의 빌드 전 항목을 확인한다. 순서는 S6 · S7 남은 것 → S8 남은 것 → S5 남은 것이고, 운영 단위는 O1 → O2 + O3 → O5 → O4 → O6 → O7(운영 가치 ÷ 비용).

## Multiplayer 창

단위 대부분이 "지금 접속 중인 세션"을 함께 보므로 창 하나에 탭으로 모았다(언리얼 Network Insights · 유니티 Multiplayer Play Mode 창의 자리).

- 창 위쪽의 대상 고르기가 모든 탭에 같은 대상을 준다. 세션이 없으면 Inspector 탭의 "주소로 붙기"로 실행 중인 서버에 붙는다.
- S6 은 화면 위에 겹쳐야 하므로 창이 아니라 게임 뷰 툴바의 보기 단추다.
- 탭은 `Source/Editor/Panels/Multiplayer/Multiplayer<이름>Tab.cpp` 하나에 `SW_EDITOR_MULTIPLAYER_TAB` 한 줄로 단다. 확장 모듈도 같은 줄로 게임별 상태 보기를 더한다.

## S5 — 네트워크 프로파일러

- 종류별 셈(`NetMessageKindTallyTable`)과 트레이스의 메시지 흐름(`NetFlowLog`, 두 프로세스를 합친 트레이스에서 화살표로 이어짐)이 들어갔다. 선 형식에 칸을 더하지 않고 양쪽이 같은 번호를 따로 계산하는 것이 설계 결정이다. 링은 Core 층 규칙(네트워크는 진단을 include 하지 못한다) 때문에 Network 층에 있다.
- **오브젝트별 복제 빈도:** 키트가 `GameNetSession::setReplicaStats( 이름, 갱신 수, 바이트 )` 로 누적 셈을 올리면 `net.stats` 의 `replicas=` 로 실리고 Profiler 탭이 초당 표로 보인다(이름 32 개까지). 한 줄에는 바이트가 큰 8 개만 싣되 앞 4 칸은 최근 증가 순, 나머지는 누적 순이다(오래 큰 이름이 새로 활발해진 이름을 가리지 않게). 키트 쪽은 파괴 복제(`destruction`)와 페어드 애니메이션(`paired_animation`)이 올린다. `ReplicationServer` 와 MMO 복제는 키트에 셈 구조가 없어 올리지 않았다(셈을 먼저 만들어야 한다).
- **한 줄 답의 상한은 `DebugReplyLine::kMaxBytes`(3500) 하나다.** 줄일 수 있는 꼬리가 있는 줄(`net.stats`)은 `makeCapped`(넘으면 뒤 항목을 빼고 ` truncated=<원래 길이>` 를 붙임), 길이가 정해진 줄은 `makeFixed`, 줄일 수 없는 줄(`state.objectline` · `state.hashline`)은 `fits` 로 재서 넘으면 오류로 멈춘다. Sync 탭 질의는 시간 상한(기본 10 초)을 넘으면 오류 글을 보인다.
- **탭이 가려져 있어도 기록한다:** 창이 열려 있고 그 탭이 고른 탭이 아닐 때 `updateWhileHidden` 을 부르고, Profiler 의 "Record while hidden" 체크(기본 꺼짐 — 자식 로그에 2 Hz 줄이 쌓인다)가 켜져 있으면 `net.stats` 를 묻는다. 번역 키 `EditorMultiplayer.ProfilerRecordHidden` 의 `ko` 는 다음 글 수집에서 채운다.
- **부하 봇은 붙이지 않았다.** 봇은 온라인 서버(TCP)를 치는데 Profiler 는 게임 세션(UDP)의 `net.stats` 를 보므로 봇을 붙여도 숫자가 오르지 않는다. 봇이 matchmaking 키트 초기화에서 서던 것은 봇 `Main.cpp` 가 `ServerApp` 처럼 호스트 범위 + `Engine` 을 세워 고쳤다(빌드 전).
- **남은 확인:** 두 프로세스 합친 트레이스의 화살표를 눈으로 본다(`-gv_traceSaveOnExit`).

## S6 — 권위 겹쳐 보기

`AuthorityGhost`(`Source/Engine/Scene`)와 게임 화면 겹치기(`-gv_debugDrawGameView=1`, 게임 뷰 툴바 단추)가 들어갔다. 클라이언트가 `-gv_authorityGhostPort=N -gv_authorityGhostToken=T` 로 디버그 채널에 붙어 `scene.poses` 를 4 Hz 로 읽고 서버 위치 고스트와 어긋난 거리 선을 그린다. 개발 명령 `net.ghost` 가 차이를 글로 알린다.

- `scene.poses` 가 서버 씬의 월드 트랜스폼을 직접 읽으므로 서버가 실제로 가진 위치가 맞다(복제 키트가 마지막으로 알린 자세가 아니다).
- **회전 · 히트박스(빌드 전):** 줄이 `name x y z [rot qx qy qz qw] [box cx cy cz hx hy hz]` 로 늘었고(옛 형식도 읽는다), `AuthorityDelta` 에 회전 각도 차 · 상자 중심 거리가 더해졌다. 고스트는 서버 상자를 서버 회전으로 돌려 그리고 앞쪽(+Z) 화살표를 보이며, 위치가 같아도 각도가 어긋나면 선 · 글을 낸다. 히트박스는 오브젝트가 선언한 월드 상자(`getWorldBox`)다.
- **남은 것:** 클라이언트를 `-gv_authorityGhostPort … -gv_debugDrawGameView=1 -gv_screenshot=…` 로 띄워 고스트가 보이는지 네 백엔드로 본다. 복제 · 보간 지연 숫자와 키트의 랙 보상 몸(`LagRecord`)은 키트가 같은 줄 형식으로 싣는 창구가 먼저 필요하다.

## S7 — 어긋남 찾기

서버는 `-gv_stateHashHistory=N` 이면 틱마다 씬 해시를 남기고(`SceneStateHash` · `StateHashHistory`) `state.hashes <from> <count>` 가 읽어 준다. `findFirstDivergence` 가 처음 어긋난 틱을 찾고, Sync 탭은 세션 서버 · 다른 서버 · 저장한 이력 파일을 A · B 칸에 견준다.

- **클라이언트 기록:** 전용 서버가 틱마다 `sendServerTick` 으로 틱 번호를 보내고 클라이언트가 씬 프레임 뒤 기록한다. 클라이언트 칸은 자식 표준 입력에 `state.hashline` 을 묻고 로그의 한 줄을 거둔다.
- **어긋난 오브젝트 짚기:** 이력에 오브젝트별 해시를 쌓지 않고(틱 × 오브젝트라 크다) 어긋난 틱 근처에서 두 프로세스의 현재 값을 견준다. 답에 `tick <n>` 머리를 달고 틱이 다르면 거절해, 해시 이력과 오브젝트 답이 다른 시각이라는 어긋남을 형식으로 막는다.
- **한 줄 답 상한:** POSIX 파이프 줄 버퍼(4096)가 가장 작아 3500 을 쓰기 · 읽기가 같이 본다. 오브젝트가 많아 `state.objectline` 이 넘으면 클라이언트 칸은 쓸 수 없다.
- **키트별 해시(빌드 전):** 키트가 `IStateHashProvider` 를 `StateHashHistory::registerChannel( 이름, &제공자 )` 로 등록하면(8 줄까지, 구독이 풀려도 이력은 남음) 같은 호스트 틱에 제공자를 부른다. `destruction` 줄은 `DestructionNetSession`, `rollback` 줄은 `RollbackSession`(틱 = 프레임 번호, 쓰는 게임은 아직 없음)이 낸다. 읽기 명령은 줄 이름을 받고 Sync 탭의 줄 이름 칸이 고른다. 오브젝트별 짚기는 씬 줄만이다.
- **한계:** 클라이언트 질의가 오류 응답을 받으면 탭이 답을 기다린 채 남는다(시간 초과 없음).
- **되감기(빌드 전):** 의도 기록(`.swintent`)으로 새 서버를 틱 0 에서 멈춘 채 띄워 목표 틱까지 한 틱씩 진행한다. `Source/Server` 가 `-gv_serverStartPaused=1` 로 첫 틱 전에 멈춘 채 뜨고(Dev 전용, 서버 `pause` 와 같은 상태), `EditorNetPlaySession::startReplayServer( .swintent 경로 )` 가 클라이언트 없는 전용 서버 하나를 그 인자로 띄워 `intent.play` 를 걸며, Sync 탭의 "Replay server" · "Step" 줄이 `step N` 을 보낸다. 재생 서버에 디버그 채널이 같이 열려 같은 틱에서 Which objects differ · Inspector 로 볼 수 있다. 운영 서버가 `intent.record` · `intent.save` 로 남긴 파일도 같은 길이다(O7). 언리얼 `-pause` · 유니티 Play Mode 의 Pause On Start 와 같은 쓰임이다. 남은 것: 재생 씬이 기록 때와 같은 씬 · 같은 고정 프레임 시간이어야 같은 궤적이라는 한계(`ControlIntentHistory`)를 눈으로 확인한다.

## S8 — 한 프로세스 가상 서버

`NetSimHarness` 를 에디터 재생에 이었다. 서버 월드 + 클라이언트 월드 N 이 한 프로세스에서 돌고 월드마다 화면이 나와, 결정적이고 한 디버거에서 모두 중단점을 걸 수 있다(언리얼 기본 PIE).

- **들어간 것:** 씬 공급자(`NetSimConfig::_pSceneSource`), `EditorNetSimSession` 과 **In process** 탭(월드마다 미리보기 뷰포트를 나란히), 입력 초점(`setInputFocus` — 한 번에 한 월드, 게임 세션은 참일 때만 입력을 읽음), 본보기 게임 `NetSimSampleGame`, 카메라 창구. DX12 캡처는 눈으로 봤고 네 백엔드 자동 확인도 통과했다.
- **결정:** 본보기 게임은 복제 키트를 쓰지 않고 게임 메시지 둘(입력 · 아바타 목록)만 쓴다. 에디터 확장은 자기 키트만 include 할 수 있어 하니스 키트와 복제 키트를 함께 쓰면 층 위반이다. 복제 키트 위의 실제 게임 구현은 게임 모듈의 일이다.
- **남은 것:**
  - 게임 모듈이 `INetSimGame` 을 구현한 실제 게임이 없다. Shooter3D 의 `ShooterNetPawnComponent` 는 `onBeginPlay` · `stepServer` 에서 전역 `findSubsystem<GameNetSession>()` 으로 호스트 · 라우터 · 역할을 받는다. 월드마다 받게 하려면 이 셋을 한 값("넷 링크")으로 묶어 컴포넌트가 자기 오브젝트 매니저에서 찾게 하고, 전역 세션과 `NetSimWorld` 가 각자 그 값을 매니저에 올린다(전역 폴백을 두지 않는다). 그 뒤 게임 모듈이 월드마다 아레나 씬 · 폰 스폰 · 입력 초점 읽기를 하는 `INetSimGame` 을 구현해 `NetSimGameRegistry::registerGame` 한다. 눈 · 빌드로 확인할 수 없는 큰 개편이라 이번에는 시도하지 않았다.
  - 클라이언트 폰 카메라를 따르는 보기는 아직 고정 오빗이고, 뷰포트 크기에 따른 거리 조정이 없어 아바타가 작게 보인다. 캡처는 DX12 만 눈으로 봤다.

## 운영 단위

디버깅과 달리 서버가 이미 떠 있는 뒤의 일이다. 조각이 흩어져 있어 서버 실행 파일에 잇는 일이 많다. 확인은 서버 프로세스를 띄워 크래시 · 종료 신호 · 느린 틱을 일으키는 자동 시나리오로 한다(코드와 테스트는 썼고 빌드 전).

| 단위 | 한 것 | 남은 것 |
|---|---|---|
| O1 사고 기록 | `ServerFrameHistory`(최근 N 틱의 틱 시간 · 접속 수 · 밀린 틱 링 버퍼, `_frameHistoryTicks`)와 느린 틱 덤프(`_slowTickDumpMs` → `Saved/ServerReports/frames_…_slow_tick.txt`, 메시지 종류별 누적 셈 포함, 10 초 간격 제한). 크래시 보고에 `ServerTick` 문맥(마지막 틱 시간 · 접속 수)을 초마다 갱신해 붙인다 | 크래시 폴더 묶기(스택 · 마지막 로그 · 틱 이력을 같은 폴더에)와 서버용 보내기 동의 — 서버용 보고 경로가 먼저 필요. 죽어 가는 프로세스에서 파일을 쓰지 않으려고 이력의 크래시 시점 덤프는 문맥 한 줄로 갈음했다 |
| O2 로그 | `JSONLinesLogOutput`(`_bJSONLogs`). 칸: `time` · `ts` · `level` · `tag` · `caller` · `tick` · `trace` · `acct` · `id` · `msg` · `file` · `line`. 켜면 콘솔 장치는 꺼진다 | 감사 줄이 요청 문맥 안에서 쓰이는지 확인(추적 id 는 `trace` 칸으로 이미 실림). OpenTelemetry 내보내기 |
| O3 종료 · 드레인 | 종료 신호 → 즉시 `/readyz` 503 → 서비스 연결이 0 이 되거나 `_drainWaitSeconds` 까지 틱을 돌린 뒤 정상 종료. 콘솔 · Ctrl+C · 테스트 틱 상한은 기다리지 않는다 | 게임 UDP 접속은 연결 수에 안 든다. 진행 중인 매치 · 거래의 끝을 키트에 묻는 길과 끊김 사유를 클라이언트에 알리는 일은 키트의 종료 API 가 먼저다 |
| O4 관리 채널 | 하지 않음 | 재시작 없이 설정 바꾸기 · 접속자 목록 · 내보내기 · 제재 · 점검 모드 · 공지. S3 의 디버그 채널과 같은 채널 · 토큰 · 루프백 규칙을 쓴다(중복을 만들지 않는다). 그 채널의 Shipping 판 · 인증을 먼저 정한 뒤 |
| O5 부하 기준선 | 틱 사용률 지표 `server_tick_utilization_ratio`(`_tickLoadWarnPercent` 를 넘으면 1 분에 한 번 경고), 대시보드 예시 `deploy/dashboards/server-overview.json` | Release 서버 한 대에 봇 1000 · 5000 으로 수용 연결 수 · 틱 p99 · 메시지 p99 를 재서 기준선을 남긴다 |
| O6 배포 | `deploy/docker/Dockerfile`, `deploy/kubernetes/server.yaml`(`/readyz` · `/healthz`, 종료 유예, 비밀은 `secretKeyRef`), `deploy/README.md`(종료 · 드레인, 비밀 교체 절차) | Agones 연결(클라우드 계정 · SDK 필요), 빌드 판 협상(프로토콜 판이 다르면 "업데이트 필요"), 서명 키 이중 키 기간 |
| O7 운영에서 개발로 | Sync 탭의 Replay server 줄(S7 되감기)이 운영 서버가 기록한 `.swintent` 를 멈춘 서버에서 재생한다(빌드 전) | 운영 서버가 의도를 기록해 파일로 내는 길(`intent.record` · `intent.save` 를 관리 채널로 부르기)은 O4 가 선 뒤 |

## 지키는 것

디버그 채널 · 원격 명령은 Dev 전용 · 루프백 · 토큰을 요구하고, 서버 코드는 에디터에 의존하지 않는다. S3 · S5 는 디버그 HUD 의 섹션 레지스트리와 같은 원천을 쓴다.
