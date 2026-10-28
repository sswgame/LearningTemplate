# 네트워크 후보

> 상태: 후보 · 선행: 없음 · 영역: network · 옛번호: B-9, C-54, D7-1, D7-6, D7-9, G-15, G-16, H-3 · 진행: 해당없음

네트워크 · 서버 · 온라인 서비스 영역에서 아직 안 한 일의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다.

## 한눈에 보기

| 절 | 한 줄 설명 | 착수 조건 유형 |
|---|---|---|
| PacketCompressionRecheck | 채팅 · 스트림 프레임 압축 재판단 | 키트가 실제 기록을 낼 때 |
| EncryptedTickCost | 암호화 켠 서버 틱 비용 숫자 | 바로 가능(Release 벤치) |
| ConfigRangeRejectRemaining | 설정 범위 밖 값을 조용히 맞추는 곳 | 새 설정 구조체가 생길 때 |
| DestructionNetSessionScene | 파괴 복제 실제 세션 확인 | 바로 가능 |
| NetworkExtensionIdeas | 물리 예측 · 음성 · 치팅 방어 · 오케스트레이션 | 요구하는 게임이 생길 때 |
| StoreProviderIntegration | Steam · Epic 연동 | 출시 플랫폼 결정 |
| PackPatchSigning | 패치 목록 서명의 기동 연결 | 스팀 밖 채널이 생길 때 |
| OperationsAndDeployment | 서버 운영 · 배포 | 보류(사용자 결정) |

## 성능 · 비용

### PacketCompressionRecheck
제목: 패킷 압축의 남은 확인
옛번호: B-9
왜 필요한가: UDP 압축을 껐다. 압축 시간은 컴파일러 · libc 에 따라 다르다.
착수 조건: 채팅 키트가 실제 기록을 낼 때.
남은 첫 일: 채팅 수치를 다시 잰다. 스트림 프레임 압축은 채팅 기록 · 거래 내역을 실을 키트가 zstd 로 켤지 정한다.

### EncryptedTickCost
제목: 암호화 켠 서버 틱 시간 측정
옛번호: D7-6
왜 필요한가: Core 창구(`Network/Security/`) · OpenSSL 3.6 · 스트림 TLS 1.3 · UDP 보안 · TLS 인증서 배선은 있는데 암호화의 비용 숫자가 없다.
착수 조건: 없음.
남은 첫 일: `NetReplicationBenchTest.SixteenClientsThousandEntitiesAtSixtyHertzEncrypted` 를 Release 로 세 번 돌려 평문 판과의 `[Bench]` 줄 차를 한 줄로 적는다.

## 정리

### ConfigRangeRejectRemaining
제목: 설정이 범위 밖 값을 조용히 맞추는 남은 곳
옛번호: C-54
왜 필요한가: 넷 키트 · `MatchMaker` · `ScheduleSystem` · 카드 · 전술 · 도시 · 배틀로얄 등 설정 구조체 대부분은 "선언된 범위 + 받을 때 거절"(`Base/Foundation/Utility/ConfigRangeUtil.h`)로 옮겼다(빌드 전). 같은 모양의 `initialize` 안 클램프는 더 찾지 못했다.
착수 조건: 새 설정 구조체가 생길 때.
남은 첫 일: 구조체를 `REFLECT` + `PROPERTY( Min · Max )` 로 만들고 `initialize` 가 `ConfigRangeUtil` 로 거절한다. 받는 쪽에서 손으로 맞추지 않는다. 통합 검증의 필터에 각 스위트의 `*Refuses*` 케이스를 넣는다.
함정: 목록 필드나 함수 호출 기본값 때문에 `REFLECT` 를 못 쓰는 설정(`RhythmPlaySettings` · `InteractionConfig` · `CoasterPhysicsParams` · `AutosaveSettings`)은 `initialize` 안에서 손으로 보고 거절한다.

## 기능

### DestructionNetSessionScene
제목: 파괴 복제의 실제 세션 확인과 사건 지연 측정
옛번호: D7-1
왜 필요한가: 파괴 복제는 가상 서버에서만 확인했다. 실제 세션(`DestructionNetSessionComponent`), 해시 로그 비교(`Scripts/dev/CompareDestructionHashLogs.py`), 씬 `destructionnet.scene.xml` 은 들어갔지만 빌드 · 실행 확인 전이다.
착수 조건: 없음.
남은 첫 일: `py -3 Scripts/dev/RunDestructionNetPair.py --preset Ninja-Release` 로 서버와 클라이언트를 한 기계에서 띄워 해시를 맞댄다. 이어 250 ms · 손실 15 % 의 사건 지연 평균 · 최대를 `NetSimDestructionMatrixTest` 로그(`max event lag … mean …`)로 전/후 잰다. 파괴 사건은 "신뢰 · 순서 없음" 채널이고, 남은 최대는 한 메시지가 거듭 잃는 몫이며 덩어리 멈춤 확정도 옮길 수 있다 — 재고 나서.
함정: 같은 입력의 해시는 Windows Debug · Release · Shipping 이 같고(`EventLogHashMatchesTheRecordedValueOnEveryBuild`), 리눅스 clang 은 확인하지 않았다.

### NetworkExtensionIdeas
제목: 네트워크 확장 후보
옛번호: D7-9
왜 필요한가: 쓰는 게임이 생기면 설계한다. 요구하는 게임 · 키트가 `Source/Games` 어디에도 없다.
착수 조건: 해당 기능을 쓰는 게임 · 키트가 정해질 때.
남은 첫 일: 물리 객체 예측(클라이언트 예측 · 롤백 R1 ~ R4 는 코드까지 들어갔고 `lte` 실행 확인이 먼저), 음성 채팅, 치팅 방어(서버 검증 규칙 목록 · 이상 탐지), 서버 오케스트레이션(매칭 뒤 인스턴스 할당 · 확장) 중 하나를 고른다.

## 조건부

### StoreProviderIntegration
제목: 스토어 제공자(Steam · Epic) 연동
옛번호: G-15
왜 필요한가: 업적 · 클라우드 세이브 · 오버레이 · 친구가 없다. 온라인 서비스 제공자 추상에 붙일 자리는 있다.
착수 조건: 출시 플랫폼이 정해지면. 외부 계정 · 라이선스가 걸려 사용자 결정 몫이다.
남은 첫 일: 정해진 플랫폼의 제공자 SDK 를 제공자 추상에 붙인다.

### PackPatchSigning
제목: 패치 목록 서명의 시작 마운트 · 명령줄 연결과 배포 경로
옛번호: G-16
왜 필요한가: 목록 서명 · 팩 해시의 형식 · 확인 · 만들기는 구현했지만 암호 제공자(OpenSSL)가 GameFramework 에 있고 게임 인스턴스가 서야 들어와서, App 기동의 시작 마운트와 `-make-patch` 명령줄이 제공자를 받지 못한다.
착수 조건: 스팀 밖 채널이 생길 때. 서명 키 관리와 배포 경로는 계정 · 비용이 드는 연동이라 사용자 결정이 먼저다.
남은 첫 일: 제공자를 기동 앞(`mountStartupPacks` 이전)으로 당기는 구조를 결정하고, `-patch-sign-key=<개인 키 PEM>` 과 공개 키 설정(`Config/Engine/` 신뢰 설정)을 잇는다. 시작 마운트가 서명을 요구하는데 제공자가 없으면 오류로 멈춘다.

## 보류

### OperationsAndDeployment
제목: 운영과 배포 (보류)
옛번호: H-3
왜 필요한가: 서버 운영 단위 O1 ~ O7([ServerDebug](ServerDebug.md))과 전용 서버의 운영 쪽이다.
착수 조건: 사용자 결정(2026-10-13) "아직 할 예정이 없다" — 폴리싱이 먼저이고 풀 때까지 하지 않는다.
남은 첫 일(풀린 뒤):
- 서버용 크래시 보고 전송 동의를 운영 설정으로(O1).
- 서버 로그 JSON lines · OpenTelemetry 내보내기 · 감사 줄의 추적 id 칸(O2).
- 부하 봇(`Tools/OnlineLoadBot`) 첫 측정(O5): Release · 서버 한 대 · 봇 1000/5000 의 동작별 p99 와 서버 틱 p99.
- 오케스트레이터(Agones · 쿠버네티스) · 서버 등록 · 매칭 할당 연결.
- 게임 UDP 접속 표 서명 키 · 계정 티켓 주 키의 배포 · 교체 절차, 실제 금칙어 목록(O6).
