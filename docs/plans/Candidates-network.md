# 네트워크 후보

> 상태: 후보 · 선행: 없음 · 영역: network · 옛번호: B-9, B-10, C-54, D7-1, D7-2, D7-5, D7-6, D7-9, G-15, G-16, H-3 · 진행: 해당없음

이 문서는 네트워크 · 서버 · 온라인 서비스 영역에서 아직 착수하지 않은 후보의 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다.

## 확인 대기

### PacketCompressionRecheck
제목: 패킷 압축의 남은 확인
옛번호: B-9
왜 필요한가: UDP 압축을 껐다. 압축 시간은 컴파일러 · libc 에 따라 다르다.
남은 것: 채팅 수치는 채팅 키트가 실제 기록을 내면 다시 잰다. 스트림 프레임 압축은 채팅 기록 · 거래 내역을 실을 키트가 zstd 로 켤지 정한다.

### UdpSocketLayerScaling
제목: MMO 규모 서버의 UDP 소켓 계층
옛번호: B-10
왜 필요한가: 지금은 호스트당 논블로킹 UDP 소켓 하나 + 전용 스레드 하나(`poll`/`WSAPoll`), 데이터그램마다 `recvfrom`/`sendto`, `NetHost` 잠금 하나다. UDP 는 소켓이 하나라 IOCP · epoll 은 지렛대가 아니다.
남은 것:
- 일괄 받기 · 보내기: `INetTransport::sendBatch` · `receiveBatch`(기본은 단건 반복)와 리눅스 `sendmmsg` · `recvmmsg` 는 들어갔고 `NetHost` 가 64개씩 묶는다(2026-10-27, 리눅스 경로는 컴파일 전). Windows 는 단건 반복이다. 남은 것은 전후 비교(`net-udp-bench`), 그리고 그 뒤에도 시스템 호출이 지배적일 때 GSO/GRO · Windows RIO(+ USO/URO, 없으면 `WSARecvMsg`).
- 포트 샤딩은 들어갔다(`GameNetSession` 샤드마다 `NetHost` · 스레드, 키트는 `NetHostShardSet`, `ServerDescriptor::pickPort`, 리눅스는 `SO_REUSEPORT` 한 포트도). 전후 측정이 남았다.
함정: 일괄 받기와 샤딩 모두 `NetUDPBenchTest` 숫자로 전후를 측정한다.
일괄 받기 전후는 같은 필터로 이 변경 앞 · 뒤 빌드를 나란히 잰다. 패킷당 시간이 줄지 않으면 일괄 경로를 걷어 낸다.

### EpollAndRealStoreVerification
제목: 실제 DB 서버 확인
옛번호: D7-5
왜 필요한가: 온라인 서비스 계층 확인 방법 중 이 PC 에서 아직 못 한 것이다.
남은 것:
- 코드는 있다(`TestServiceStorePostgres` · `TestEphemeralStoreResp` 가 `ServiceStoreContract.h` 를 지난다) — PostgreSQL · RESP 계약 테스트를 실제 서버로 한 번(`SW_TEST_POSTGRES_URL` · `SW_TEST_RESP_URL` — Valkey · Garnet(Windows), 이 PC 에는 서버가 없다). 계약 테스트(`ServiceStoreContract.h`)를 SQL 구현에도 같이 돌린다.

### EncryptedTickCost
제목: 암호화 켠 서버 틱 시간 측정
옛번호: D7-6
왜 필요한가: Core 창구(`Network/Security/`) · OpenSSL 3.6 구현 · 스트림 TLS 1.3 · UDP 보안 · 서버 설정의 TLS 인증서 배선은 있다. 암호화의 비용 숫자가 없다.
남은 것: 벤치 판 `NetReplicationBenchTest.SixteenClientsThousandEntitiesAtSixtyHertzEncrypted` 를 Release 로 세 번 돌려 평문 판과의 `[Bench]` 줄 차를 숫자 한 줄로 적는다.

### RollbackPredictionLteRun
제목: Shooter3D 예측 · 롤백의 lte 실행 확인
옛번호: D7-8
왜 필요한가: 몸 이동 예측 · 서버 보정 · 시각 보정(R1 ~ R4)은 코드와 테스트가 들어갔다. 지연 · 손실이 있는 연결에서 보정이 얼마나 일어나고 화면이 튀지 않는지는 실행해야 안다.
남은 것:
- — Shooter3D 전용 서버 + 클라이언트(`Server -game=Shooter3D` · `App -game=Shooter3D -gv_netConnectAddress=127.0.0.1:7777`)에 `-gv_netEmuPreset=lte` 를 걸고 움직여 보정 횟수(`getCorrectionCount`)와 화면이 튀지 않는지 본다.
- — 같은 실행에서 짧게 눌러도 점프가 매번 나가는지(`ShooterNetPawnComponent::_bJumpPending`)와 보정 횟수를 본다.
못 하는 이유: 서버 + 클라이언트 두 프로세스에 입력을 주고 보정 횟수를 읽는 한 명령이 없고, 점프를 짧게 누르는 일과 화면 튐은 사람이 본다. 풀리는 조건: `netplay.scenario.xml` 에 점프 입력 단계와 보정 횟수 탐침이 생기면 `manual` 을 떼고 시나리오로 옮긴다.

## 정리

### ConfigRangeRejectRemaining
제목: 설정이 범위 밖 값을 조용히 맞추는 남은 곳
옛번호: C-54
왜 필요한가: 넷 키트 전부 · `MatchMaker` · `ScheduleSystem` · `RoundSeries` · `WorldClock` · `SessionStateComponent` · `Vitality` · `ResourceGauge` 와 그 받는 쪽, 그 밖의 설정 구조체 열다섯 안팎은 "선언된 범위 + 받을 때 거절"로 옮겼다(공통 형식은 `Base/Foundation/Utility/ConfigRangeUtil.h` 머리 주석, 빌드 전). 카탈로그가 없을 때 기본 규칙으로 대신 서던 곳도 오류로 멈춘다.
2026-10-27 옮김(빌드 전): `PokerTable` · `MatgoGame` · `UnoGame`(`CardGameTest`) · `RunMap`/`SRPGCampaign`(`TacticsSRPGTest`) · `CitySimulation`(`CityBuilderTest`) · `CreatureTown`(`CreatureLifeTest`) · `PartyItemSpawner`(`PartyArenaTest`) · `BrFlightPath::makeRandom`(`BattleRoyaleTest`) · `SQLConnectionPool`(`SQLDriverSQLiteTest`). 의 필터에 이 스위트의 `*Refuses*` · `*Refuse*` 케이스가 들어가야 한다(`VerifyBatch.json` 은 다른 작업자가 고치는 중이라 통합이 더한다).
남은 것: 같은 모양의 `initialize` 안 클램프는 `Source/GameFramework` · `Source/Games` 에서 더 찾지 못했다(`max`/`min`/`clamp` 가 설정 필드에 닿는 함수를 훑은 결과 `MetroAbilitySet::applyToSettings` 뿐이고 그것은 능력 효과로 계산한 값을 0 아래로 못 내리는 규칙이라 설정 거절이 아니다). 새 설정 구조체가 생길 때 같은 방식으로 만든다.
함정: 구조체를 `REFLECT` + `PROPERTY( Min · Max )` 로 바꾸고 `initialize` 가 `ConfigRangeUtil` 로 거절한다. 받는 쪽에서 손으로 맞추지 않는다. 목록 필드나 함수 호출 기본값이 있어 `REFLECT` 로 못 쓰는 설정(`RhythmPlaySettings` · `InteractionConfig` · `CoasterPhysicsParams` · `AutosaveSettings`)은 `initialize` 안에서 손으로 보고 거절한다.

## 기능

### DestructionNetSessionScene
제목: 파괴 복제의 실제 세션 확인과 사건 지연 측정
옛번호: D7-1
왜 필요한가: 파괴 복제는 가상 서버에서 확인했다. 실제 세션 위의 파괴 복제(`DestructionNetSessionComponent` · `DestructionNetSession`), 해시 로그 비교(`Scripts/dev/CompareDestructionHashLogs.py`), 컴포넌트를 둔 씬 `game/empty/maps/destructionnet.scene.xml`(`DestructionShowcaseTest.NetworkSceneAddsTheSessionComponentToTheShowcase`)은 들어갔지만 빌드 · 실행 확인 전이다.
남은 것:
- 사건 지연 측정: 파괴 사건은 "신뢰 · 순서 없음" 채널(N21b)이다. 250 ms · 손실 15 % 의 사건 지연 평균 · 최대 전/후를 `NetSimDestructionMatrixTest` 로그(`max event lag … mean …`)로 잰다. 남은 최대는 한 메시지가 거듭 잃는 몫이다. 덩어리 멈춤 확정(신뢰 자세)도 받는 쪽이 틱으로 끼우므로 옮길 수 있다 — 재고 나서.
- 실행 대기: — 빌드 확인과 사건 지연 전/후를 한 번에 얻는다.
- 두 로그 견주기: 한 기계의 두 프로세스는 `py -3 Scripts/dev/RunDestructionNetPair.py --preset Ninja-Release` 가 서버 `Server -gv_firstScene=<씬> -gv_netListenPort=7777` 와 클라이언트 `App -gv_firstScene=<씬> -gv_netConnectAddress=127.0.0.1:7777` 를 띄워 맞댄다(실행 전).
- 매니페스트에 더할 항목 초안(VerifyBatch.json 구조가 바뀌는 중이라 문서에 둔다. 통합이 더한다): `{ "id": "destruction-net-pair", "title": "파괴 복제 — Server + App 한 기계 해시 맞대기", "kind": "measure", "config": "Release", "platform": "windows", "gpu": "exclusive", "pending": true, "timeout_minutes": 20, "need_command": "py -3 Scripts/dev/RunDestructionNetPair.py --preset Ninja-Release (종료 코드 0 이 같음)", "source": "docs/plans/Candidates-network.md DestructionNetSessionScene", "result_doc": "docs/plans/Candidates-network.md DestructionNetSessionScene" }`
함정: 같은 입력의 해시가 구성을 넘어 같은 것은 `DestructionDamageTest.EventLogHashMatchesTheRecordedValueOnEveryBuild`(해시 다섯)가 지킨다. Windows Debug · Release · Shipping 같은 값을 확인했고 리눅스 clang 확인은 하지 않는다.

### OnlineMultiServerLatency
제목: 서버 여럿의 실제 망 지연 측정
옛번호: D7-5
왜 필요한가: 버스 · 캐시 왕복을 루프백에서만 쟀다. 남은 것: 실제 지연(루프백이 아닌 망)에서의 왕복 측정.
한 PC 판: `DelayedStreamTransport`(Core `Network/Transport/`)가 TCP 스트림 전송을 감싸 보내기에 고정 지연을 건다(들어갔고 `StreamTransportTest.DelayedTransportAddsTheDelayToEachDirectionAndKeepsOrder` 가 지킨다, 빌드 전). `_ioThreadCount == 0` 이면 스레드 없이 `pollIO` 가 도는 수동 판이라 루프백에도 쓴다. 벤치 `RespLatencyBenchTest`(가짜 RESP 서버 앞에 0 · 20 · 80 ms, GET 왕복 `[Bench]` 줄)가 들어갔고 첫 측정이 남았다(시간 예산도 그때 고친다). 서버 간 버스는 캐시(`EphemeralServerBus`) 위에서 돌므로 이 벤치가 버스 왕복의 바닥이다. `CacheStoreFactory` 는 전송을 직접 만들어 장식자를 끼울 자리가 없어, 실제 Valkey · Garnet 앞 지연은 공장에 전송을 주입하는 길을 낼 때 한다. UDP 쪽은 `NetSim`(`NetEmulationTransport`)이 이미 한다.
처음부터 두 번째 장비가 필요한 것은 실제 회선의 손실 · 경로 변화뿐이다. 그 확인은 두 번째 장비 또는 클라우드 인스턴스가 생길 때 한다.

### NetworkExtensionIdeas
제목: 네트워크 확장 후보
옛번호: D7-9
왜 필요한가: 쓰는 게임이 생기면 설계한다. 지금 못 하는 이유: 요구하는 게임이 없다. 풀리는 조건: 해당 기능을 쓰는 게임 · 키트가 정해질 때.
재확인(2026-10-27): 물리 객체 예측은 선행인 클라이언트 예측 · 롤백(R1 ~ R4)이 코드까지 들어갔지만 `lte` 실행 확인(`RollbackPredictionLteRun`)이 남아 착수하지 않고, 음성 채팅 · 치팅 방어 · 서버 오케스트레이션을 요구하는 게임 · 키트는 `Source/Games` 어디에도 없어 조건이 그대로다.
남은 것: 물리 객체 예측(예측 · 롤백 `RollbackPredictionLteRun` 다음), 음성 채팅, 치팅 방어 체계(서버 검증 규칙 목록 · 이상 탐지), 서버 오케스트레이션(매칭 뒤 인스턴스 할당 · 확장).

## 조건부

### StoreProviderIntegration
제목: 스토어 제공자(Steam · Epic) 연동
옛번호: G-15
착수 조건: 출시 플랫폼이 정해지면.
재확인(2026-10-27): 출시 플랫폼을 정한 문서가 없다. 제공자 SDK 연동은 외부 계정 · 라이선스가 걸려 사용자 결정 몫이라 착수하지 않았다.
왜 필요한가: 업적 · 클라우드 세이브 · 오버레이 · 친구가 없다. 온라인 서비스의 제공자 추상에 붙일 자리는 있다.

### PackPatchSigning
제목: 패치 목록 서명의 시작 마운트 · 명령줄 연결과 배포 경로
옛번호: G-16
왜 필요한가: 목록 서명 · 팩 해시의 형식 · 확인(`ResourcePakManager::setPatchTrust`) · 만들기(`PackPatchBuildParams::_signer`)는 구현했지만 암호 제공자(OpenSSL)가 GameFramework 에 있고 게임 인스턴스가 서야 들어와서 App 기동의 시작 마운트와 `-make-patch` 명령줄은 아직 제공자를 받지 못한다.
착수 조건: 스팀 밖 채널이 생길 때. 서명 키 관리와 배포 경로는 계정 · 비용이 드는 연동이라 사용자 결정이 먼저다.
남은 것: 제공자를 기동 앞(`mountStartupPacks` 이전)으로 당기는 구조 결정, 그 뒤 `-patch-sign-key=<개인 키 PEM>` 과 공개 키 설정(`Config/Engine/` 의 신뢰 설정)을 잇는다. 시작 마운트가 서명을 요구하는 설정을 가졌는데 제공자가 없으면 오류로 멈춘다.

## 보류

### OperationsAndDeployment
제목: 운영과 배포 (보류)
옛번호: H-3
착수 조건: 사용자 결정 2026-10-13 — "아직 할 예정이 없다". 폴리싱(결함 · 성능 증거 · 검증 자동화 · 다중 월드 · 에디터)이 먼저다. 풀 때까지 하지 않는다.
왜 필요한가: 서버 운영 단위 O1 ~ O7([서버 디버깅 계획](ServerDebug.md) 운영 단위)과 전용 서버의 운영 쪽이다.
남은 것:
- 서버용 크래시 보고 전송 동의 = 운영 설정(O1, 크래시 묶기 자체는 들어갔다).
- 서버 로그 JSON lines 출력 · OpenTelemetry 내보내기 · 감사 줄의 추적 id 칸(O2).
- (측정은 폴리싱의 "성능 증거"에 들므로 보류 밖이다.
- 부하 봇(`Tools/OnlineLoadBot`) 첫 측정(O5): Release · 서버 한 대 · 봇 1000/5000 으로 `chat_and_match` · `login_storm` 의 동작별 p99 와 서버 틱 p99, 전용 서버에 `--server` 로 같은 시나리오. 채팅 채널 샤딩 · 정산 길이 · 매처 대기 p99 판단의 근거다.
- 오케스트레이터(Agones · 쿠버네티스)와 서버 등록 · 매칭 할당 · 서버 띄우기 연결.
- 게임 UDP 접속 표 서명 키 · 계정 티켓 주 키의 배포 · 교체 절차, 실제 금칙어 목록(O6 · 운영).
