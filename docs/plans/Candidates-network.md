# 네트워크 후보

> 상태: 후보 · 선행: 없음 · 영역: network · 옛번호: B-9, B-10, C-54, D7-1, D7-2, D7-5, D7-6, D7-7, D7-8, D7-9, F-3, G-2, G-8, G-15, G-16, H-3 · 진행: 해당없음

이 문서는 네트워크 · 서버 · 온라인 서비스 영역에서 아직 착수하지 않은 후보의 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다. 서버 디버깅은 [ServerDebug](ServerDebug.md), 서버 버스 형식은 [ServerBusFormat](ServerBusFormat.md)에 있습니다.

## 확인 대기

### PacketCompressionRecheck
제목: 패킷 압축의 남은 확인
옛번호: B-9
왜 필요한가: UDP 압축을 끈 결정과 측정값은 [결정 기록](../09_Decisions.md) 5-9 에 있다. 압축 시간은 컴파일러 · libc 에 따라 다르다.
남은 것: WSL Release 로 `NetCompressionBenchTest` 를 한 번 돌린다. 채팅 수치는 채팅 키트가 실제 기록을 내면 다시 잰다. 스트림 프레임 압축은 채팅 기록 · 거래 내역을 실을 키트가 zstd 로 켤지 정한다.

### UdpSocketLayerScaling
제목: MMO 규모 서버의 UDP 소켓 계층
옛번호: B-10
왜 필요한가: 지금은 호스트당 논블로킹 UDP 소켓 하나 + 전용 스레드 하나(`poll`/`WSAPoll`), 데이터그램마다 `recvfrom`/`sendto`, `NetHost` 잠금 하나다. UDP 는 소켓이 하나라 IOCP · epoll 은 지렛대가 아니다.
남은 것:
- 리눅스 측정: Windows 측정은 [검증과 측정](../08_Verification.md) 2절 "UDP 소켓 계층" 에 있다(패킷당 약 6.5 us, 대부분 `recvfrom`/`sendto`). 그 표의 서버 스레드 %(14 · 54 %)는 다른 빌드가 CPU 를 20 ~ 46 % 쓰던 때 값이라 무효다. 패킷당 시간만 참고한다. 리눅스(WSL Release, `ulimit -n 8192` 뒤 같은 필터)와 함께 다른 빌드가 없을 때 Windows 를 다시 재서 나란히 놓는다.
- 일괄 받기 · 보내기: 시스템 호출이 지배적이면 `INetTransport` 일괄 받기 · 보내기 하나의 계약 — 리눅스 `recvmmsg`/`sendmmsg`(+ GSO/GRO), Windows RIO(+ USO/URO, 없으면 `WSARecvMsg`).
- 포트 샤딩: 스레드 하나가 차면 포트 샤딩(두 플랫폼 같은 계약 — 로그인 토큰이 포트를 준다, 리눅스는 `SO_REUSEPORT` 를 선택) + 샤드마다 `NetHost`. [ServerDebug](ServerDebug.md) S1 ⑤ 와 같은 일이다.
함정: 일괄 받기와 샤딩 모두 `NetUDPBenchTest` 숫자로 전후를 측정한다.

### EpollAndRealStoreVerification
제목: 리눅스 epoll · 실제 DB 서버 확인
옛번호: D7-5
왜 필요한가: 온라인 서비스 계층 확인 방법 중 이 PC 에서 아직 못 한 것이다.
남은 것:
- 리눅스 epoll(`EpollStreamTransport`): `WSL-Debug` · `WSL-Shipping` 에서 `StreamTransportTest` 를 `--test_repeat=20` 과 변이(`setReceivePaused( false )` 의 할 일 빼기 → 백프레셔 테스트가 진다)로. io_uring 은 측정 뒤.
- PostgreSQL · RESP 계약 테스트를 실제 서버로 한 번(`SW_TEST_POSTGRES_URL` · `SW_TEST_RESP_URL` — Valkey(WSL) · Garnet(Windows) 각각, 이 PC 에는 서버가 없다). 계약 테스트(`ServiceStoreContract.h`)를 SQL 구현에도 같이 돌린다.

### EncryptedTickCost
제목: 암호화 켠 서버 틱 시간 측정
옛번호: D7-6
왜 필요한가: Core 창구(`Network/Security/`) · OpenSSL 3.6 구현 · 스트림 TLS 1.3 · UDP 보안 · 서버 설정의 TLS 인증서 배선은 있다. 방향은 [결정 기록](../09_Decisions.md) 5-9. 암호화의 비용 숫자가 없다.
남은 것: 벤치 판 `NetReplicationBenchTest.SixteenClientsThousandEntitiesAtSixtyHertzEncrypted` 가 있다(평문 판과 `[Bench]` 줄을 나란히 남긴다). Release 로 세 번 돌려 두 줄의 차를 [결정 기록](../09_Decisions.md) 3절에 숫자 한 줄로 적는다.

## 정리

### ConfigRangeRejectRemaining
제목: 설정이 범위 밖 값을 조용히 맞추는 남은 곳
옛번호: C-54
왜 필요한가: 넷 키트 전부 · `MatchMaker` · Base 의 `ScheduleSystem` · `RoundSeries` · `WorldClock` · `SessionStateComponent` 는 "선언된 범위 + 받을 때 거절"로 옮겼다(공통 형식은 `Base/Foundation/Utility/ConfigRangeUtil.h` 머리 주석). 설정 구조체가 범위를 선언하지 않아 받는 쪽마다 손으로 맞추는 곳이 남았다.
남은 것:
- Base 의 `Vitality`: `initialize` 가 실패를 돌려주면 받는 장르 키트 여섯(`BrMatch` · `HorrorMatch` · `MechArenaWorld` · `ScavengerExpedition` · `MetroDuelist` · `AdventureVitals`)과 `Shooter3D` 의 void 함수가 모두 바뀐다. `MetroidvaniaCatalog` 처럼 데이터에서 읽는 쪽은 카탈로그를 읽을 때 거절하는 편이 맞다. 쓰는 곳의 재생 속도 `max( 0, … )` 도 같은 모양이다.
- 장르 키트: `FightingSettings`(라운드 시간 · 대기 `max( 1, … )`) · `PartyRoundSeries`(`setWinScore` · 순위 점수 · 라운드 시간) · `AdventureVitals` · `AdventureElementGrid` · `KlondikeGame` · `RTSWorld` · `TrampolineArena` 등.
함정: 구조체를 `REFLECT` + `PROPERTY( Min · Max )` 로 바꾸고 `initialize` 가 `ConfigRangeUtil` 로 거절한다. 받는 쪽에서 손으로 맞추지 않는다.

## 기능

### DestructionNetSessionScene
제목: 파괴 복제의 실제 세션 확인과 사건 지연 측정
옛번호: D7-1
왜 필요한가: `GF_NetSimulation` · `GF_NetDestruction` 의 파괴 복제는 가상 서버에서 확인했다. 실제 세션 위의 파괴 복제(`DestructionNetSessionComponent` · `DestructionNetSession`)와 해시 로그 비교(`Scripts/dev/CompareDestructionHashLogs.py`)는 들어갔지만 빌드 · 실행 확인 전이다.
남은 것:
- 사건 지연 측정: 파괴 사건은 "신뢰 · 순서 없음" 채널(N21b)이다. 250 ms · 손실 15 % 의 사건 지연 평균 · 최대 전/후를 `NetSimDestructionMatrixTest` 로그(`max event lag … mean …`)로 잰다(빌드 뒤). 남은 최대는 한 메시지가 거듭 잃는 몫이다. 덩어리 멈춤 확정(신뢰 자세)도 받는 쪽이 틱으로 끼우므로 옮길 수 있다 — 재고 나서.
- 컴포넌트를 둘 씬: 쇼케이스 씬(`destructionshowcase.scene.xml`)은 키트 없는 Empty 의 것이고 여러 테스트 · 쿠커가 읽는다. 정함(2026-10-21): 쇼케이스는 그대로 두고 네트워크용 씬을 따로 둔다 —
  에디터에서 쇼케이스를 열고(`scene.open`) 빈 오브젝트 하나에 `DestructionNetSessionComponent` 를 달아(`component.add`) `destructionnet.scene.xml` 로 저장한다(`scene.saveAs`). 손으로 XML 을 쓰지 않는다. 빌드 · 실행이 필요해 남았다.
- 두 로그 견주기: 서버 `Server -gv_firstScene=<씬> -gv_netListenPort=7777` ↔ 클라이언트 `App -gv_firstScene=<씬> -gv_netConnectAddress=127.0.0.1:7777`. 리눅스 서버 ↔ Windows 클라이언트는 CI 잡 몫이다(결정 기록 5-12).
함정: 같은 입력의 해시가 구성을 넘어 같은 것은 `DestructionDamageTest.EventLogHashMatchesTheRecordedValueOnEveryBuild`(해시 다섯)가 지킨다. Windows Debug · Release · Shipping 같은 값을 확인했고, 리눅스 clang 에서 다르면 CI 잡이 진다.

### DedicatedServerNoRenderer
제목: 전용 서버 타깃에서 렌더러 빼기와 리눅스 확인
옛번호: D7-2
왜 필요한가: 게임별 기동 확인은 ServerTest `EveryBuiltGameStartsAsADedicatedServer`(Debug 9 게임 77 s)가 있다. 남은 것은 서버 빌드의 군살과 리눅스 확인이다.
남은 것:
- 서버 빌드에서 렌더러 코드를 아예 빼기(결정 기록 `D7-2.1`, 지금은 컴파일만 되고 돌지 않는다). 렌더러 · RHI 를 빼고 링크 오류를 보며 고친 뒤 실행 파일 크기 · 링크 시간을 전후로 잰다. 렌더러 · RHI 를 Engine CMake 에서 타깃별로 가르는 일이라 Engine 소유 작업이다.
- 리눅스 서버(`WSL-*-Server`) 빌드 · `readelf -d` 로 libX11 없음 확인 · `ServerTest` — Windows 만 확인했다. 로컬 WSL 빌드는 하지 않기로 해서(사용자 결정) CI 리눅스 잡 몫이다.
함정: 서버용 크래시 보고 전송 동의(운영 설정)는 `OperationsAndDeployment` 쪽이다.

### OnlineMultiServerLatency
제목: 서버 여럿의 실제 망 지연 측정
옛번호: D7-5
왜 필요한가: 버스 · 캐시 왕복을 루프백에서만 쟀다.
남은 것: 실제 지연(루프백이 아닌 망)에서의 버스 · 캐시 왕복 측정.

### NetChannelSchemaHashHandshake
제목: 넷 채널 등록 API 로 스키마 해시를 핸드셰이크 버전에 섞기
옛번호: D7-7
왜 필요한가: 지금은 메시지마다 스트림 머리의 64 비트 해시가 실려, 배치가 다른 빌드의 바이트는 읽기가 `SchemaMismatch` 로 거절한다. Mech · Horror 스냅샷은 B 로 옮겼다(단계 4-a). 계획은 [리플렉션 형식](ReflectionFormats.md) 3.3 · 3.10 · 5절 "게임 넷 연결".
착수 조건: 이 스냅샷을 채널에 싣는 게임이 생길 때. 지금은 호출부가 테스트뿐이라 범용 API 를 미리 두지 않았다.
남은 것: 채널 객체에 `registerMessage<T>( kind )` 를 두고, 등록한 타입의 해시가 `NetHostConfig::_wireVersion` 에 섞이게 해 핸드셰이크가 `VersionMismatch` 로 먼저 거절하게 한다. 확인을 마친 채널 안에서만 본문만 쓰는 경로(3.10)를 만들어 메시지마다 싣는 해시 8 바이트를 뺀다.
함정: 등록이 버전을 만들므로 섞는 일을 따로 부를 자리가 없어야 한다.

### RollbackPrediction
제목: 클라이언트 예측 · 롤백과 관심 영역 복제
옛번호: D7-8
왜 필요한가: 상태 동기화와 파괴 동기화는 있지만 입력 예측 · 서버 보정 · 롤백, 그리고 관심 영역(relevancy)으로 복제 대상을 거르는 층이 없다. 설계는 결정 기록 5-12 `D7-8` 행으로 정했다(서버 권위 + 내 폰만 재시뮬레이션 + 허용 오차 비교, 64인 이하 30 Hz, 첫 게임 Shooter3D). 선행 잠복 결함 셋(`NetOwnerState`, `PredictionReconcileResult`, `RollbackConfig` 범위 거절)은 고쳤다.
남은 것:
- Shooter3D 의 내 폰을 `ClientPrediction` + `findOwnerState` 로 예측한다.
- 보정 오차를 부드럽게 녹이는 시각 보정(언리얼 smoothing).
함정: 월드 전체 롤백은 `NetworkExtensionIdeas` 쪽이다. 규모 방향은 `UdpSocketLayerScaling` 이다.

### NetworkExtensionIdeas
제목: 네트워크 확장 후보
옛번호: D7-9
왜 필요한가: 쓰는 게임이 생기면 설계한다.
남은 것: 물리 객체 예측(`RollbackPrediction` 5단계), 음성 채팅, 치팅 방어 체계(서버 검증 규칙 목록 · 이상 탐지), 서버 오케스트레이션(매칭 뒤 인스턴스 할당 · 확장).

### PersistentRecordUnknownFields
제목: 영속 레코드의 모르는 필드 보관
옛번호: F-3
왜 필요한가: 영속 레코드(`ServiceRecordUtil::readDurable`)는 배포 중 옛 빌드가 새 빌드의 레코드를 읽으면 모르는 필드 때문에 거절한다(조용히 지우지 않고 요청 실패). 모르는 필드를 버리고 읽게 하면 옛 빌드가 고쳐 다시 쓸 때 새 빌드의 값이 지워진다.
남은 것: 레코드가 orphan 바이트를 가지고 쓰기 때 그대로 다시 싣는 구조가 먼저다(언리얼 `FPropertyTag` 보관, protobuf unknown fields 보존과 같은 것).
함정: 캐시 레코드(`readCache`)는 이미 서버 버스와 같은 혼합 빌드 읽기다([서버 버스 계획](ServerBusFormat.md) 4절).

## 조건부

### SubtickInput
제목: CS2 식 서브틱 입력
옛번호: G-2
착수 조건: 넷 게임이 생기면(2026-10-05 제안; 지금 넷 키트를 쓰는 테스트 게임은 0).
왜 필요한가: `RawInputEvent` · `NativeWindowEvent` 에 시각이 없다(`MSG::time` 을 버림).
남은 것:
- B1 사건 시각 + 입력 리플레이 판 4.
- B2 프레임 안 자리와 그 순간까지의 시선 누적(시계 주입).
- B3 `InputMap` 이 행동별 "누른 순간" 을 준다.
- B4 클릭 순간의 시선으로 발사(쿨다운 남은 몫은 이미 고침).
- B5 넷 입력 항목마다 스탬프 · `viewTick`. 지금은 메시지마다 하나라 다시 보낸 옛 입력도 최신으로 판정한다. 자리는 있다: `NetInputFormat::_bStamped` · `NetInputSendWindow::push( …, stamp )` · `NetInputEntry::_stamp`, 켜면 `kClientServer` 판을 올린다. 서버 클램프(랙 보정 `LagCompensationHistory::raycastAt(float)` 는 있다).
- B6(선택) Win32 입력 스레드: 먼저 `GetMessageTime` 해상도를 재고, 에디터 · 콘솔이 먼저 소비하는 판정을 우회하지 않게.
함정: 롤백 · 락스텝 키트에는 넣지 않는다(프레임 단위 결정성이 계약).

### MmoUpdateAckViaDelivery
제목: MMO 갱신 확인을 NetConnection 전달 통지로
옛번호: G-8
착수 조건: 패킷 확인 → 메시지 전달 통지가 Core 에 생기면.
왜 필요한가: 키트 확인 메시지(`kUpdateAck`)를 지우고 그 통지로 판정한다(언리얼 NAK 자리).

### StoreProviderIntegration
제목: 스토어 제공자(Steam · Epic) 연동
옛번호: G-15
착수 조건: 출시 플랫폼이 정해지면.
왜 필요한가: 업적 · 클라우드 세이브 · 오버레이 · 친구가 없다. 온라인 서비스의 제공자 추상에 붙일 자리는 있다.

### PatchDeltaDelivery
제목: 패치 · 델타 업데이트 배포 구조
옛번호: G-16
착수 조건: 출시 일정이 잡히면(크래시 컨텍스트와 텔레메트리 코드는 있다).
왜 필요한가: 팩 단위 차분 배포 · 버전 관리가 없다.

### ServerBusFormatLeftovers
제목: 서버 버스 형식의 남은 확인 셋
옛번호: 없음
왜 필요한가: [ServerBusFormat](ServerBusFormat.md) 은 구현이 모두 들어가 문서를 요약으로 줄였지만, 확인하지 못한 셋이 남았습니다.
남은 것: (1) 6절 테스트의 통과 기록 — 통합 때 Debug 빌드를 세운 것까지만 확인했으므로 해당 스위트를 실제로 돌려 적습니다. (2) 7절 측정이 [검증과 측정](../08_Verification.md) 에 없어 Release 로 재서 옮깁니다. (3) 해지 알림 서버 둘 테스트를 쓰지 않았습니다.
함정: 측정은 Release 로만 합니다.

## 보류

### OperationsAndDeployment
제목: 운영과 배포 (보류)
옛번호: H-3
착수 조건: 사용자 결정 2026-10-13 — "아직 할 예정이 없다". 폴리싱(결함 · 성능 증거 · 검증 자동화 · 다중 월드 · 에디터)이 먼저다. 풀 때까지 하지 않는다.
왜 필요한가: 서버 운영 단위 O1 ~ O7([서버 디버깅 계획](ServerDebug.md) 운영 단위)과 전용 서버의 운영 쪽(크래시 묶기 · 텔레메트리 전송)이다.
남은 것:
- 서버용 크래시 보고 전송 동의 = 운영 설정(O1, 크래시 묶기 자체는 들어갔다).
- 서버 로그 JSON lines 출력 · OpenTelemetry 내보내기 · 감사 줄의 추적 id 칸(O2).
- 부하 봇(`Tools/OnlineLoadBot`) 첫 측정: Release · 서버 한 대 · 봇 1000/5000 으로 `chat_and_match` · `login_storm` 의 동작별 p99 와 서버 틱 p99, 전용 서버에 `--server` 로 같은 시나리오(O5). 채팅 채널 샤딩 · 정산 길이 · 매처 대기 p99 판단의 근거다. Debug 참고값은 [검증과 측정](../08_Verification.md) 2절 "온라인 부하 봇".
- 오케스트레이터(Agones · 쿠버네티스)와 서버 등록 · 매칭 할당 · 서버 띄우기 연결.
- 게임 UDP 접속 표 서명 키 · 계정 티켓 주 키의 배포 · 교체 절차.
- 실제 금칙어 목록(O6 · 운영).
