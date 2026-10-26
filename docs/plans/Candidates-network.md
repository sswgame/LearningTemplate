# 네트워크 후보

> 상태: 후보 · 선행: 없음 · 영역: network · 옛번호: B-9, B-10, C-54, D7-1, D7-2, D7-5, D7-6, D7-9, G-15, H-3 · 진행: 해당없음

이 문서는 네트워크 · 서버 · 온라인 서비스 영역에서 아직 착수하지 않은 후보의 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다. 서버 디버깅은 [ServerDebug](ServerDebug.md), 서버 버스 형식은 [결정 기록](../09_Decisions.md) 5-9 에 있습니다.

## 확인 대기

### PacketCompressionRecheck
제목: 패킷 압축의 남은 확인
옛번호: B-9
왜 필요한가: UDP 압축을 끈 결정과 측정값은 [결정 기록](../09_Decisions.md) 5-9 에 있다. 압축 시간은 컴파일러 · libc 에 따라 다르다.
남은 것: WSL Release 로 `NetCompressionBenchTest` 를 한 번 돌린다. 채팅 수치는 채팅 키트가 실제 기록을 내면 다시 잰다. 스트림 프레임 압축은 채팅 기록 · 거래 내역을 실을 키트가 zstd 로 켤지 정한다.
실행 대기: 매니페스트 id `net-compression-bench`, `net-compression-bench-wsl`(WSL 은 사용자가 요청할 때만). 결정 기록 5-9 표 옆에 적는다.

### UdpSocketLayerScaling
제목: MMO 규모 서버의 UDP 소켓 계층
옛번호: B-10
왜 필요한가: 지금은 호스트당 논블로킹 UDP 소켓 하나 + 전용 스레드 하나(`poll`/`WSAPoll`), 데이터그램마다 `recvfrom`/`sendto`, `NetHost` 잠금 하나다. UDP 는 소켓이 하나라 IOCP · epoll 은 지렛대가 아니다.
남은 것:
- 리눅스 측정: Windows 측정은 [검증과 측정](../08_Verification.md) 2절 "UDP 소켓 계층"(패킷당 약 6.5 us, 대부분 `recvfrom`/`sendto`)에 있다. 그 표의 서버 스레드 %(14 · 54 %)는 다른 빌드가 CPU 를 쓰던 때 값이라 무효이고 패킷당 시간만 참고한다. 리눅스(WSL Release, `ulimit -n 8192` 뒤 같은 필터)와 다른 빌드가 없을 때 Windows 를 다시 재서 나란히 놓는다.
- 일괄 받기 · 보내기: 시스템 호출이 지배적이면 `INetTransport` 일괄 계약 — 리눅스 `recvmmsg`/`sendmmsg`(+ GSO/GRO), Windows RIO(+ USO/URO, 없으면 `WSARecvMsg`).
- 포트 샤딩은 들어갔다(`GameNetSession` 샤드마다 `NetHost` · 스레드, 키트는 `NetHostShardSet`, `ServerDescriptor::pickPort`, 리눅스는 `SO_REUSEPORT` 한 포트도). 전후 측정이 남았다.
함정: 일괄 받기와 샤딩 모두 `NetUDPBenchTest` 숫자로 전후를 측정한다.
실행 대기: 매니페스트 id `net-udp-bench`, `net-udp-bench-wsl`(WSL 은 사용자가 요청할 때만). 일괄 받기는 시스템 호출이 지배적일 때만 착수한다.

### EpollAndRealStoreVerification
제목: 리눅스 epoll · 실제 DB 서버 확인
옛번호: D7-5
왜 필요한가: 온라인 서비스 계층 확인 방법 중 이 PC 에서 아직 못 한 것이다.
남은 것:
- 확인 대기: 매니페스트 id `epoll-stream-transport-wsl`(WSL 은 사용자가 요청할 때만).
- 확인 대기: 매니페스트 id `real-store-contract`(실제 서버가 있는 기계에서).
- 리눅스 epoll(`EpollStreamTransport`): `WSL-Debug` · `WSL-Shipping` 에서 `StreamTransportTest` 를 `--test_repeat=20` 과 변이(`setReceivePaused( false )` 의 할 일 빼기 → 백프레셔 테스트가 진다)로. io_uring 은 측정 뒤.
- PostgreSQL · RESP 계약 테스트를 실제 서버로 한 번(`SW_TEST_POSTGRES_URL` · `SW_TEST_RESP_URL` — Valkey(WSL) · Garnet(Windows), 이 PC 에는 서버가 없다). 계약 테스트(`ServiceStoreContract.h`)를 SQL 구현에도 같이 돌린다.

### EncryptedTickCost
제목: 암호화 켠 서버 틱 시간 측정
옛번호: D7-6
왜 필요한가: Core 창구(`Network/Security/`) · OpenSSL 3.6 구현 · 스트림 TLS 1.3 · UDP 보안 · 서버 설정의 TLS 인증서 배선은 있다(방향은 [결정 기록](../09_Decisions.md) 5-9). 암호화의 비용 숫자가 없다.
남은 것: 벤치 판 `NetReplicationBenchTest.SixteenClientsThousandEntitiesAtSixtyHertzEncrypted` 를 Release 로 세 번 돌려 평문 판과의 `[Bench]` 줄 차를 [결정 기록](../09_Decisions.md) 3절에 숫자 한 줄로 적는다.
실행 대기: 매니페스트 id `net-replication-bench`(혼자 돌 때 Release, 세 번).

## 정리

### ConfigRangeRejectRemaining
제목: 설정이 범위 밖 값을 조용히 맞추는 남은 곳
옛번호: C-54
왜 필요한가: 넷 키트 전부 · `MatchMaker` · `ScheduleSystem` · `RoundSeries` · `WorldClock` · `SessionStateComponent` · `Vitality` · `ResourceGauge` 와 그 받는 쪽, 그 밖의 설정 구조체 열다섯 안팎은 "선언된 범위 + 받을 때 거절"로 옮겼다(공통 형식은 `Base/Foundation/Utility/ConfigRangeUtil.h` 머리 주석, 빌드 전). 카탈로그가 없을 때 기본 규칙으로 대신 서던 곳도 오류로 멈춘다.
확인 대기: 매니페스트 id `config-range-reject-tests`(테스트 필터 묶음 — 빌드와 함께 처음 돈다).
남은 것: 위 밖에서 `initialize` 안의 `MathUtil::max` · `clamp` 로 설정을 맞추는 곳(카드 게임 Poker · Matgo · Uno, SRPG, CityBuilder 등 — 같은 모양의 `grep` 으로 찾는다). 하나씩 같은 방식으로 옮긴다.
함정: 구조체를 `REFLECT` + `PROPERTY( Min · Max )` 로 바꾸고 `initialize` 가 `ConfigRangeUtil` 로 거절한다. 받는 쪽에서 손으로 맞추지 않는다. 목록 필드나 함수 호출 기본값이 있어 `REFLECT` 로 못 쓰는 설정(`RhythmPlaySettings` · `InteractionConfig` · `CoasterPhysicsParams` · `AutosaveSettings`)은 `initialize` 안에서 손으로 보고 거절한다.

## 기능

### DestructionNetSessionScene
제목: 파괴 복제의 실제 세션 확인과 사건 지연 측정
옛번호: D7-1
왜 필요한가: 파괴 복제는 가상 서버에서 확인했다. 실제 세션 위의 파괴 복제(`DestructionNetSessionComponent` · `DestructionNetSession`), 해시 로그 비교(`Scripts/dev/CompareDestructionHashLogs.py`), 컴포넌트를 둔 씬 `game/empty/maps/destructionnet.scene.xml`(`DestructionShowcaseTest.NetworkSceneAddsTheSessionComponentToTheShowcase`)은 들어갔지만 빌드 · 실행 확인 전이다.
남은 것:
- 사건 지연 측정: 파괴 사건은 "신뢰 · 순서 없음" 채널(N21b)이다. 250 ms · 손실 15 % 의 사건 지연 평균 · 최대 전/후를 `NetSimDestructionMatrixTest` 로그(`max event lag … mean …`)로 잰다. 남은 최대는 한 메시지가 거듭 잃는 몫이다. 덩어리 멈춤 확정(신뢰 자세)도 받는 쪽이 틱으로 끼우므로 옮길 수 있다 — 재고 나서.
- 실행 대기: 매니페스트 id `destruction-net-tests` — 빌드 확인과 사건 지연 전/후를 한 번에 얻는다.
- 두 로그 견주기: 서버 `Server -gv_firstScene=<씬> -gv_netListenPort=7777` ↔ 클라이언트 `App -gv_firstScene=<씬> -gv_netConnectAddress=127.0.0.1:7777`. 리눅스 서버 ↔ Windows 클라이언트는 CI 잡 몫이다(결정 기록 5-12).
함정: 같은 입력의 해시가 구성을 넘어 같은 것은 `DestructionDamageTest.EventLogHashMatchesTheRecordedValueOnEveryBuild`(해시 다섯)가 지킨다. Windows Debug · Release · Shipping 같은 값을 확인했고 리눅스 clang 에서 다르면 CI 잡이 진다.

### OnlineMultiServerLatency
제목: 서버 여럿의 실제 망 지연 측정
옛번호: D7-5
왜 필요한가: 버스 · 캐시 왕복을 루프백에서만 쟀다. 남은 것: 실제 지연(루프백이 아닌 망)에서의 왕복 측정.
지금 못 하는 이유: 서로 다른 호스트 둘 이상의 망이 필요하다. 풀리는 조건: 두 번째 장비 또는 클라우드 인스턴스(루프백 지연 주입은 `NetSim` 이 이미 한다).

### NetworkExtensionIdeas
제목: 네트워크 확장 후보
옛번호: D7-9
왜 필요한가: 쓰는 게임이 생기면 설계한다. 지금 못 하는 이유: 요구하는 게임이 없다. 풀리는 조건: 해당 기능을 쓰는 게임 · 키트가 정해질 때.
남은 것: 물리 객체 예측([RollbackPrediction](RollbackPrediction.md) 다음), 음성 채팅, 치팅 방어 체계(서버 검증 규칙 목록 · 이상 탐지), 서버 오케스트레이션(매칭 뒤 인스턴스 할당 · 확장).

## 조건부

### StoreProviderIntegration
제목: 스토어 제공자(Steam · Epic) 연동
옛번호: G-15
착수 조건: 출시 플랫폼이 정해지면.
왜 필요한가: 업적 · 클라우드 세이브 · 오버레이 · 친구가 없다. 온라인 서비스의 제공자 추상에 붙일 자리는 있다.

## 보류

### OperationsAndDeployment
제목: 운영과 배포 (보류)
옛번호: H-3
착수 조건: 사용자 결정 2026-10-13 — "아직 할 예정이 없다". 폴리싱(결함 · 성능 증거 · 검증 자동화 · 다중 월드 · 에디터)이 먼저다. 풀 때까지 하지 않는다.
왜 필요한가: 서버 운영 단위 O1 ~ O7([서버 디버깅 계획](ServerDebug.md) 운영 단위)과 전용 서버의 운영 쪽이다.
남은 것:
- 서버용 크래시 보고 전송 동의 = 운영 설정(O1, 크래시 묶기 자체는 들어갔다).
- 서버 로그 JSON lines 출력 · OpenTelemetry 내보내기 · 감사 줄의 추적 id 칸(O2).
- 부하 봇(`Tools/OnlineLoadBot`) 첫 측정(O5): Release · 서버 한 대 · 봇 1000/5000 으로 `chat_and_match` · `login_storm` 의 동작별 p99 와 서버 틱 p99, 전용 서버에 `--server` 로 같은 시나리오. 채팅 채널 샤딩 · 정산 길이 · 매처 대기 p99 판단의 근거다. Debug 참고값은 [검증과 측정](../08_Verification.md) 2절 "온라인 부하 봇".
- 오케스트레이터(Agones · 쿠버네티스)와 서버 등록 · 매칭 할당 · 서버 띄우기 연결.
- 게임 UDP 접속 표 서명 키 · 계정 티켓 주 키의 배포 · 교체 절차, 실제 금칙어 목록(O6 · 운영).
