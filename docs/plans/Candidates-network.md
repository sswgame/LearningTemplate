# 네트워크 후보

> 상태: 후보 · 선행: 없음 · 영역: network · 진행: 해당없음

네트워크 · 서버 · 온라인 서비스 영역에서 아직 안 한 일의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다.

## 한눈에 보기

| 절 | 한 줄 설명 | 착수 조건 유형 |
|---|---|---|
| DestructionNetSessionScene | 파괴 복제 실제 세션 해시 맞대기(스크립트 끝, 에셋 필요) | 에셋을 만든 뒤 |
| NetworkExtensionIdeas | 물리 예측 · 음성 · 치팅 방어 · 오케스트레이션 | 요구하는 게임이 생길 때 |
| StoreProviderIntegration | Steam · Epic 연동의 SDK 쪽 | 출시 플랫폼 결정 |
| PackPatchSigning | 패치 서명의 배포 경로 · 키 교체 | 스팀 밖 채널이 생길 때 |
| OperationsAndDeployment | 서버 운영 · 배포 | 보류(사용자 결정) |

## 기능

### DestructionNetSessionScene
제목: 파괴 복제의 실제 세션 확인
왜 필요한가: 파괴 복제는 가상 서버에서만 확인했다. 두 프로세스로 띄워 해시 로그를 맞대는 스크립트(`py -3 -m Scripts destruction-net run|compare`, `Scripts/dev/DestructionNetPair.py`)는 들어갔고 맞대는 규칙은 시험했다.
착수 조건: 없음.
남은 첫 일: `py -3 -m Scripts destruction-net run --preset Ninja-Release` 를 돌린다. 이 작업 트리에서 Debug 로 시도했을 때 서버는 세션(6 오브젝트)까지 섰지만 클라이언트 `App` 이 엔진 기본 텍스처(`engine/textures/missing.dds`)가 없어 기동에 실패하고, 씬의 `.fracture` 에셋(`brick_wall` · `crate_medium` · `barrel`)도 읽지 못해 해시 줄이 나오지 않았다 — 에셋을 만든 뒤 다시 돌린다.
함정: 같은 입력의 해시는 Windows Debug · Release · Shipping 이 같고(`EventLogHashMatchesTheRecordedValueOnEveryBuild`), 리눅스 clang 은 확인하지 않았다. 클라이언트 `App` 은 자동 종료 인자가 없어 스크립트가 서버 종료 뒤 닫는다. 콘솔 대신 파일 로그만 남기는 구성이면 `--client-log` 로 파일을 준다.

### NetworkExtensionIdeas
제목: 네트워크 확장 후보
왜 필요한가: 쓰는 게임이 생기면 설계한다. 요구하는 게임 · 키트가 `Source/Games` 어디에도 없다.
착수 조건: 해당 기능을 쓰는 게임 · 키트가 정해질 때.
이번에 시도하지 않은 이유: 음성 채팅은 엔진에 마이크 캡처 · 코덱이 없어 전송 · 지터 버퍼만으로는 쓸 수 없고, 물리 예측은 `lte` 실행 확인 없이 코드를 더하면 검증할 길이 없다. 음성은 캡처 API(`IAudioCapture`)와 코덱 선택이 첫 단위다.
남은 첫 일: 물리 객체 예측(클라이언트 예측 · 롤백 R1 ~ R4 는 코드까지 들어갔고 `lte` 실행 확인이 먼저), 음성 채팅, 치팅 방어(서버 검증 규칙 목록 · 이상 탐지), 서버 오케스트레이션(매칭 뒤 인스턴스 할당 · 확장) 중 하나를 고른다.

## 조건부

### StoreProviderIntegration
제목: 스토어 제공자(Steam · Epic) 연동의 SDK 쪽
왜 필요한가: 클라이언트 쪽 계약(`IPlatformServices` — 업적 · 통계 · 클라우드 파일 · 리치 프레전스 · 로그인 티켓)과 스팀 어댑터(`SteamPlatformServices`, SDK 호출 묶음 `ISteamApiBridge` 를 받는다)는 SDK 없이 구현했다. 스토어 이름은 `PlatformStoreName::kSteam` 으로 서버의 스팀 영수증 검증기(`SteamReceiptValidator`)와 같은 글이다. 서버 쪽 영수증 검증기 · 로그인 제공자 조립은 이미 있었다.
착수 조건: 출시 플랫폼이 정해지면. 외부 계정 · 라이선스가 걸려 사용자 결정 몫이다.
남은 첫 일: SDK 를 링크하는 플러그인이 `ISteamApiBridge` 를 구현해 `SteamPlatformServices::create` 에 넘긴다. Epic 은 같은 계약의 다른 어댑터다.

### PackPatchSigning
제목: 패치 서명의 배포 경로와 키 교체
왜 필요한가: 시작 마운트 · `-make-patch` 연결은 구현했다(`PackPatchTrustSource` 함수 칸을 GameFramework 가 `NetSecurity::getProvider` 로 채우고, 공개 키는 `EngineConfig::_patchTrustPublicKeyXHex` · `Y`, 개인 키는 `-patch-sign-key <PEM 경로>` 인자로만 받는다. 키가 있는데 칸이 비면 기동이 멈춘다). 빌드 확인만 했고 서명한 패치를 실제로 마운트하는 실행은 하지 않았다.
착수 조건: 스팀 밖 채널이 생길 때. 배포 경로(CDN)와 키 교체 절차는 계정 · 비용이 드는 연동이라 사용자 결정이 먼저다.
남은 첫 일: 서명한 패치로 시작 마운트가 통과하고 변조한 패치가 거절되는 자동 시나리오. 공개 키 이중 기간(키 교체).

## 보류

### OperationsAndDeployment
제목: 운영과 배포 (보류)
왜 필요한가: 서버 운영 단위 O1 ~ O7([ServerDebug](ServerDebug.md))과 전용 서버의 운영 쪽이다.
착수 조건: 사용자 결정(2026-10-13) "아직 할 예정이 없다" — 폴리싱이 먼저이고 풀 때까지 하지 않는다.
남은 첫 일(풀린 뒤):
- 서버용 크래시 보고 전송 동의를 운영 설정으로(O1).
- 서버 로그 JSON lines · OpenTelemetry 내보내기 · 감사 줄의 추적 id 칸(O2).
- 오케스트레이터(Agones · 쿠버네티스) · 서버 등록 · 매칭 할당 연결.
- 게임 UDP 접속 표 서명 키 · 계정 티켓 주 키의 배포 · 교체 절차, 실제 금칙어 목록(O6).
