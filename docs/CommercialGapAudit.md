# 상용 엔진 대비 감사 (2026-11-12)

> 질문: 언리얼 5 · 유니티 6 · Godot 4 의 같은 시스템과 견주었을 때 이 엔진에 어떤 결함 · 성능 문제 · 구조 빈틈이 있고, 무엇부터 고치는가?
> 독자: 다음 묶음을 고르는 통합 담당과 그 일을 받는 작업자(에이전트와 사람).
> 결론: 지금 코드 경로로 확인된 오동작(P0) 17 건을 먼저 닫는다 — 에디터 4(되돌리기 2 · 자동 저장 · 에셋 옮김), 세이브 손실 2, 영수증 이중 지급 1, 연결 번호 오배달 1, Android 3(종료 · 볼륨 키 · 오디오 백엔드), 시퀀서 · ALSA · 타일맵 3, Win32 Alt 1, POSIX 프로세스 2. 그 뒤 P1 108 건(조건부 결함과 확장 · 성능 병목), P2 74 건이다. 실행 단위는 맨 아래 "다음 할 일" 에 있다.

기준 커밋은 main `e480b07498` 이다. 줄 번호는 그 커밋의 것이다.

## 읽는 법

- **범위:** `Source/` 의 2단계 폴더 122 개 전부(끝의 체크리스트). 셰이더 · `Content/` · `Config/` 는 엔진 코드가 쓰는 범위에서 봤다.
- **방법:** 영역 열하나(Core, 네트워크, 온라인, RHI, 렌더러, 에셋 · 직렬화 · 오브젝트, 시뮬레이션, 플랫폼 · 입력 · UI · 호스트, GameFramework Runtime, 키트 · 게임, 에디터)로 나눠 읽었다. 코드는 고치지 않았고 빌드 · 실행 · 측정도 하지 않았다. P0 17 건의 근거 줄은 통합 단계에서 다시 열어 확인했다.
- **우선순위:**
  - **P0** — 지금 코드 경로로 확인된 오동작 가운데 사용자 데이터 · 진행 · 기동 · 보안에 닿는 것.
  - **P1** — 조건이 붙는 결함 · 잠재 결함, 그리고 확장 · 성능 병목.
  - **P2** — 개선과 작은 결함.
- **확신:** "확인" 은 그 줄과 호출자를 읽었다는 뜻이다(실행으로 재현한 것은 없다). "추정" 은 조건이나 증상을 코드만으로 확정하지 못한 것이다. "측정 필요" 는 구조는 확인했지만 비용을 재지 않은 것이다.
- **외부 API:** context7 MCP 가 감사 세션에 없었다. Win32 · DXGI · Vulkan · NDK · Emscripten · ALSA · FCM 동작 주장은 기억 기준이다(Jolt 5.6.0 · Detour 는 설치 헤더를 읽었다). 고치기 전에 그 판의 문서로 대조한다.
- **이미 알려진 일과의 관계:** [NEXT](NEXT.md) · [DesignBacklog](DesignBacklog.md) · [StructureRefactor](StructureRefactor.md) · [PlatformTargets](PlatformTargets.md) 에 있는 항목은 다시 적지 않았다. 새 근거가 있는 것만 "(보강)" 으로 표시했다.
- **항목 형식:** 제목 줄에 번호 · 분류(결함 / 성능 / 구조) · 확신, 그 아래 근거(`파일:줄`), 상용 엔진, 제안, 확인 방법. 영역은 절 제목이다. 경로는 저장소 루트 기준이다.

## 되풀이되는 모양

항목 대부분이 아래 여섯 모양 가운데 하나다. 증상을 하나씩 고치지 말고 이 모양을 쓸 수 없게 형식을 바꾼다.

1. **세대 없는 번호 · 순번 · 경로 글을 정체로 쓴다.** 연결 슬롯 번호(P0-8), 컴포넌트 `이름#순번`(A-2), 복셀 블록 순서 번호(K-3), 재시작마다 1 부터인 파티 · 티켓 id(O-7), 열린 문서의 경로 글(P0-4), 32 비트 이름 해시(C-8).
2. **같은 사실을 두 곳이 따로 안다.** 쓰는 쪽 · 읽는 쪽의 한도와 판(A-3 · A-5 · A-6), 개수는 CPU · 목록은 GPU(RN-1), 캐시 기록과 서버 메모리 표(O-6 · O-7).
3. **실패 · 잘림을 값으로 돌려주지 않거나 호출자가 버린다.** `(void)` 로 버린 신뢰 송신(N-3), 구간 읽기 실패 뒤 새 판(P0-5), exec 실패를 성공으로(P0-16), 잘린 포맷을 성공으로(C-10), 팩 마운트 실패 뒤 기동(A-4).
4. **플랫폼 창이 "빠뜨려도 컴파일되는" 선택 훅을 가진다.** 포커스 전달 · 글 입력 · 닫기 완료 · 쉬기 · 수명 구독 · 오디오 백엔드(P0-9 ~ P0-11, P-3 ~ P-6).
5. **플레이어 세이브가 핫 리로드용 엄격한 스냅샷 형식을 그대로 쓴다.** 판이 정확히 같아야 읽고, 개수 · 색인으로 짝짓고, 실패하면 조용히 새 판이다(P0-5 · A-6 · G-1).
6. **상태 변화를 값이 아니라 호출 순서 규칙으로 다룬다.** 일시정지(P0-12), 배율 변경(S-4), 빨리 감기(K-2).

## P0 — 확인된 오동작

### P0-11 오디오: 웹 클라이언트에 오디오 장치 백엔드가 없다
- 분류: 결함(남은 일) · 확신: 확인(코드 경로)
- 근거: 장치 백엔드는 `Source/Engine/Audio/Windows/XAudio2AudioBackend.cpp` · `Linux/AlsaAudioBackend.cpp` · `Android/AAudioAudioBackend.cpp` 셋이다. 웹(Emscripten)에는 없어 예전에는 기동에서 `IAudioService::create` 가 `SW_FATAL` 로 섰다. 지금은 `Source/Engine/CMakeLists.txt` 가 오디오를 짓는 구성에서 `Audio/<플랫폼>/*AudioBackend.cpp` 가 정확히 하나가 아니면 구성 단계에서 멈춘다 — Web 클라이언트 구성은 백엔드가 생길 때까지 서지 않는다.
- 상용 엔진: 유니티 WebGL 은 WebAudio, Godot 4 웹은 AudioWorklet 백엔드를 둔다(기억). 언리얼은 백엔드 없는 플랫폼을 패키징 대상으로 두지 않는다.
- 제안: `Audio/Web`(AudioWorklet + 공유 메모리 링 버퍼, 사용자 몸짓 뒤 `AudioContext.resume`, 노드 오류 → `notifyOutputLost`).
- 확인: Web-Shipping 구성이 서고 `py -3 -m Scripts web-check --game Empty` 기동 로그에 `AudioService.InitializedChannels`(device output).

## P1 — 조건부 결함과 확장 · 성능 병목

### Core

#### C-2 삼각 분할 실패가 "빈 목록" 이 아니라 구멍을 덮는 부채꼴을 남긴다
- 분류: 결함 · 확신: 실패 계약은 확인, 구멍 2 개 반례는 손 시뮬레이션
- 근거: `Source/Core/Spatial/PolygonTriangulation.cpp:194-201` 은 실패 때 남은 부분을 부채꼴로 붙이고 false 를 돌려준다. 호출자 둘은 결과를 `(void)` 로 버리며 "실패하면 빈 목록" 이라 적었다(`Source/Editor/Common/Asset/Model/MeshFracture.cpp:310`, `Source/Engine/Destruction/Fracture/PolygonFracture.cpp:273`). 다리 놓기(`:79-84`)는 같은 위치의 중복 꼭짓점 중 늘 첫 사본에 이어 구멍 둘이 같은 바깥 꼭짓점을 고르면 막힌다. 귀 자르기는 최악 O(n³) 이다.
- 상용 엔진: polypartition(Godot)은 다리 후보에 `InCone`, earcut 은 `locallyInside` 검사를 건다.
- 제안: 실패하면 들어올 때 크기로 `outListIndex` 를 되돌린다. 다리 후보에 모서리 원뿔 검사를 더하고 귀 후보를 오목 꼭짓점 목록으로 좁힌다.
- 확인: 바깥 사각형 + 구멍 둘(같은 바깥 꼭짓점을 고르는 배치)로 `triangulate` 가 true 이고 넓이 합이 맞는지. 테스트는 구멍 1 개까지만 있다.

#### C-6 출하 빌드의 전역 할당기가 CRT `malloc` 그대로다
- 분류: 성능 / 구조 · 확신: 코드 확인, 병목 여부는 측정 필요
- 근거: `Source/Core/Memory/Memory.cpp:224-234`(`::malloc`) · `:148-158`. 스레드 캐시 · 크기 등급 할당기가 없고 `LinearAllocator` · `PoolAllocator` 사용처는 13 파일이다. NEXT 의 "메모리 풀은 측정 뒤" 결정과 같은 줄에 선다 — 이 항목은 그 측정의 대상과 방법이다.
- 상용 엔진: 언리얼 `FMallocBinned2/3` 또는 mimalloc, 유니티 TLSF + 스레드별 버킷 + 잡 임시 할당기.
- 제안: `Memory::allocate` 가 유일한 입구라 바꿀 자리가 하나다. 측정에서 병목이면 출하 구성에서 mimalloc(vcpkg overlay 포트)으로 바꾼다.
- 확인: Release 에서 프레임당 할당 수 · 시간을 `MemoryProfiler` 카운터와 Tracy 로 보고, 워커 N 개가 16 ~ 256 바이트를 할당 · 해제하는 벤치로 후보를 비교한다.

#### C-7 `gv_` 문자열 변수의 잠금을 직접 읽는 쪽이 잡을 수 없다
- 분류: 결함(경쟁) / 구조 · 확신: 코드 확인, 충돌 빈도는 추정
- 근거: `Source/Core/GlobalVariable/GlobalVariableManager.h:461-464` — 매크로가 보통 전역을 만든다(정의 234 곳). 문자열은 쓰는 쪽만 `s_stringVarMutex`(`Source/Core/GlobalVariable/GlobalVariableManager.cpp:42`, 익명 이름공간)를 잡는다. `Source/Engine/Renderer/Frame/FrameRenderer.cpp:177` 이 `gv_renderPipeline` 을 렌더 스레드에서 `string_view` 로 잡는다 — 그 사이 콘솔이 값을 바꾸면 해제된 버퍼를 읽는다. 스칼라는 한 프레임 안에서 두 번 읽으면 값이 다를 수 있다(b282a98f82 · 545d9cf099 와 같은 계열).
- 상용 엔진: 언리얼 `TAutoConsoleVariable` 은 게임 스레드 값과 렌더 스레드 값을 따로 두고 렌더 명령으로 옮긴다.
- 제안: 문자열 변수를 잠금 안에서 사본을 돌려주는 접근자 형식(`GlobalString::get()`)으로 바꿔 직접 참조가 컴파일되지 않게 한다. 렌더 스레드가 읽는 변수는 프레임 스냅샷으로 옮기는 길 하나만 둔다.
- 확인: TSan(Linux Debug)에서 한 스레드가 `gv_renderPipeline` 을 반복해 바꾸는 시나리오.

#### C-8 32 비트 이름 해시가 밖으로 나가는 키인데 현지화 표에 충돌 검사가 없다
- 분류: 결함(잠재 데이터 오류) · 확신: 코드 확인, 확률은 계산
- 근거: `Source/Core/Name/hashed_string.h:13` 이 "바이너리 필드 이름 · 열거자 · 지역화 테이블이 이 값을 저장" 한다고 적는다(uint32, 대소문자 무시 FNV-1a). `Source/Engine/Localization/StringTable.cpp:83-88` 은 `_mapTable[keyHash] = pStored` 로 나중 것이 이긴다. 생일 문제로 키 3 만 개에 약 10 %, 10 만 개에 약 69 % 다. 열거자 쪽(`Source/Engine/Reflection/TypeRegistry.cpp:55`)은 충돌을 보지만 타입 이름 표(`:188,203`)는 보지 않는다.
- 상용 엔진: 언리얼 `FTextKey` 는 해시로 버킷만 고르고 키 글을 비교한다.
- 제안: 밖으로 나가는 키 해시를 64 비트로 올리고(`computeStringHash64` 가 이미 있다), 표를 지을 때 "같은 해시, 다른 키" 를 오류로 멈춘다(쿠킹 · 로드 양쪽).
- 확인: 단위 테스트 — 32 비트 해시가 같은 두 키를 `LocalizationManager` 에 넣어 오류가 나는지.

#### C-9 컨테이너가 std 와 같은 이름 · API 인데 복잡도가 다르다
- 분류: 성능 · 확신: 코드 확인, 배수는 측정 필요
- 근거: ① `Source/Core/Container/vector.h:977-988` — 범위 `insert` 가 원소마다 `insert( pos, *it )` 을 부른다(`ByteStream.h:54` · `Archive.cpp:112` 의 바이트 붙이기가 이 경로다). ② `Source/Core/Container/deque.h:14` — `std::deque` 래퍼라 MSVC 에서 `sizeof( T ) > 8` 이면 원소마다 힙 할당 하나다(`AsyncFileIOQueue` · `NetTransport` 가 쓴다 — MSVC 헤더 값은 다시 열지 않았다). ③ `Source/Core/Container/unordered_map.h:645` — `reserve` 가 버킷만 잡는다. ④ `Source/Core/Container/map.h:30` — 정렬 벡터라 대량 구축이 O(n²) 다.
- 상용 엔진: `TArray::Append` 는 한 번 늘리고 memcpy, `TDeque` 는 링 버퍼, `TMap::Reserve` 는 원소 저장소도 잡는다.
- 제안: 범위 `insert` · `assign` 을 "개수 계산 → 한 번 예약 → 한 번 밀기 → `Memory::copy`" 로, `sw::deque` 는 2 의 거듭제곱 링 버퍼로, `reserve` 는 밀집 배열도 잡게, `map` 범위 구축은 붙이고 `stable_sort` 로 바꾼다.
- 확인: 벤치 — `vector<uint8>` 1 MB 범위 삽입, `deque<Datagram>` 1 만 번 push 의 할당 횟수, 무작위 키 10 만 개 `map` 구축.

#### C-14 결정성 표시가 손으로 적은 파일 목록이라 빠진 TU 가 있다
- 분류: 구조 · 확신: 목록의 빈틈은 확인, 실제 비트 차이는 추정
- 근거: `Source/Core/CMakeLists.txt:190` 의 표시 대상은 `VectorMath` · `DeterministicMathUtil` · `Ray` · `OrientationUtil` · `PolygonTriangulation` 다섯뿐이다. `MatrixMath.cpp`(`quaternion::slerp` · `operator*` — 결정성 표시된 물리 TU 가 부른다)와 `ValueNoise.cpp`(헤더가 "어느 기계에서나 같은 값" 이라 적고 `VoxelTerrain` 이 쓴다)가 없다. 표시된 TU 안의 libm `asin/atan2` 를 막는 린트도 없다.
- 상용 엔진: 유니티는 Burst `FloatMode.Deterministic` + `Unity.Mathematics` 로 형식 수준에서 가른다.
- 제안: 손 목록을 Core Math 티어 통째 `-ffp-contract=off` 로 바꾼다(비용은 재서 정한다). 표시된 TU 에서 libm 초월 함수를 막는 린트를 더한다.
- 확인: `DeterministicMathTest` 처럼 기록 비트를 고정하는 케이스를 `quaternion::operator*` · `slerp` · `ValueNoise::sampleFractal` 에 더해 Debug · Release · Linux 에서 돌린다.

### 네트워크 · 텔레메트리

#### N-7 게임 UDP 포트의 지표가 운영 끝점에 하나도 없다
- 분류: 구조(관측) · 확신: 확인
- 근거: `registerCounter|Gauge|Histogram` 전수 검색 결과는 `Source/Engine/Server/ServerObservability.cpp:81-85`(틱 · 드롭 · 서비스 연결 · 사용률)와 `ProcessMetrics` 등뿐이다. `NetEndpoint::getRejectedPacketCount` · `getAuthenticationFailureCount` · `getReplayRejectedCount`(`Source/Core/Network/Connection/NetEndpoint.h:332-338`)는 테스트만 읽는다. 접속 수 · 샤드별 pps · RTT/손실 분포 · 끊김 사유가 `/metrics` 에 없다. 덧붙여 접속 로그가 클라이언트 IP 를 Info 로 남기고(`Source/Engine/Net/GameNetSession.cpp:624`) 보존 · 가림 정책이 코드에 없다.
- 상용 엔진: 언리얼은 NetDriver 통계 · DDoS 감지 이벤트를 분석으로 보낸다. 유니티 Transport 는 NetworkMetrics 를 낸다.
- 제안: `GameNetSession` 이 `MetricRegistry` 를 받아 샤드 라벨로 등록한다. 사유별 끊김 카운터를 `NetDisconnectReason` 열거에서 생성한다. 로그의 주소는 가림 형식을 거친다.
- 확인: `OpsHTTPEndpointTest` 에 `/metrics` 본문이 `net_connections` · `net_rejected_packets_total` 을 담는지.

### 온라인 서비스 · 저장소

#### O-3 비밀번호 해시(Argon2id)가 저장소 워커(DB 연결) 스레드에서 돌고 저장소 대기열에 상한이 없다
- 분류: 성능(DoS) · 확신: 구조 확인, 수치는 측정 필요
- 근거: `Source/GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.cpp:83-108` 의 로그인 일은 저장소 스레드에서 돌고 그 안의 `Source/GameFramework/Kits/Feature/Online/Account/Server/Rule/LoginStoreLogic.cpp:554` 가 해시를 계산한다(19 MiB × 2 회, 워커 기본 4). 로그인이 몰리면 원장 · 우편 · 거래의 저장소 일이 전부 뒤에 선다. `Source/GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLConnectionPool.cpp:138-149` 의 대기열은 상한이 없다.
- 상용 서비스: KDF 를 DB 연결과 분리된 연산 풀에서 돌리고 동시 해시 수를 세마포어로 묶는다(OWASP Password Storage).
- 제안: 로그인을 "읽기(저장소) → 해시(상한 있는 연산 풀, 넘치면 `kBusy`) → 커밋(저장소)" 세 단계로 나눈다. 저장소 풀에 대기 상한을 둔다.
- 확인: 부하 봇 `login_storm` 을 `_bLightPasswordHash` 없이 돌리며 `service_store_pending_peak` 와 다른 키트의 저장소 지연을 본다(O-12).

#### O-6 매치 명단이 "최대 한 번" 버스로만 게임 서버에 가고, 받는 쪽 버퍼는 읽는 곳이 없다
- 분류: 결함 + 구조 · 확신: 확인
- 근거: `Source/GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.cpp:565-566` 이 `(void)kAssignTopic.publish( … )` 뒤 티켓에 Found 를 보낸다. 게임 서버의 받아들이기는 버스로 채운 메모리 표뿐이다(`MatchServerAgent.cpp:36-43`, `isAdmitted` `:80-84`) — 메시지 하나를 잃으면 전원이 Found 를 받고 접속에서 거절된다. 표의 수명은 2 분이라 경기 중 재접속이 거절된다. `_newMatchBuffer.push`(`:43`)를 비우는 `pollNewMatches` 는 호출자가 없어 경기마다 는다(`findExpected` · `requestBackfill` 도 호출자 0).
- 상용 서비스: Agones Allocation · PlayFab Multiplayer Servers 는 배정을 영속 기록으로 쓰고 서버가 그것을 읽는다.
- 제안: 명단을 캐시 기록(`mm/match/<서버>/<경기>`, 수명 = 경기 시한)으로 쓰고 표에 없으면 기록을 읽는다. 소비자 없는 버퍼 · 반쪽 기능은 지운다.
- 확인: 발행을 실패시키는 가짜 버스로 "Found 인데 `isAdmitted` 가 false" 케이스.

#### O-7 티켓 · 파티 · 로비 id 가 재시작마다 1 부터 다시 나오고, 티켓을 낸 서버가 죽으면 파티가 "줄 섬" 에 갇힌다
- 분류: 결함 · 확신: 확인
- 근거: `Source/GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h:215` `( _serverID << 32 ) | ++_sequence`, `MatchQueueService.cpp:397` 도 같다. 재시작 뒤 캐시에 살아 있는 옛 파티(30 분) · 로비(2 시간) 기록과 id 가 겹친다. `:304` 는 `party._queuedTicketID != 0` 이면 AlreadyQueued, `:743-747` `leaveQueue` 는 로컬 표에 없으면 NotQueued 라 낸 서버가 죽은 뒤 다른 서버로 붙은 파티장은 줄을 서지도 떠나지도 못한다.
- 상용 서비스: EOS Lobby · 유니티 Lobby 는 id 를 서비스가 발급(UUID)하고 멤버 하트비트로 낡은 상태를 걷는다.
- 제안: id 는 캐시 INCR 이나 난수 128 비트로 낸다. `leaveQueue` 는 로컬 티켓이 없으면 파티 기록의 티켓을 푼다.
- 확인: 같은 가짜 캐시로 `PartyLobbyService` 를 두 번 `initialize` 해 두 번째 `createParty`. `TestMatchQueueService` 에 "A 에서 티켓 → A 버림 → B 에서 leave → join".

#### O-10 길드 창고가 자산 이전 정책을 보지 않아 거래 불가 자산이 계정 사이로 옮겨진다
- 분류: 결함(정책 우회) · 확신: 확인
- 근거: `Source/GameFramework/Kits/Feature/Online/Social/Server/GuildService.cpp:729-737` 의 검사는 자산 id 형식 · 금액 범위 · 분개 키뿐이다. 거래는 `TradeStateMachine.cpp:26-27` 에서 `isTradable` 로 막는다. `ILedgerPolicy` 는 잔액 상한만 안다(`Source/GameFramework/Runtime/Online/Ledger/LedgerTypes.h:129-140`). 멤버가 유료 재화 · 귀속 아이템을 맡기고 임원이 꺼내면 금지된 이전이 된다.
- 상용 서비스: PlayFab Economy v2 · Steam Inventory 는 "이전 가능" 을 아이템 정의의 속성으로 두고 모든 통로가 같은 것을 본다.
- 제안: `ILedgerPolicy::isTransferable( assetID )` 를 원장 계층에 두고 "계정 → 다른 보유자" 다리를 가진 이동이면 `stageTransfer` 가 본다. 사용자 결정(2026-11-12): 맡긴 당사자가 허용한 권한 안에서만 이전한다 — 설계는 DesignBacklog "길드 창고의 이전 불가 자산".
- 확인: `TestGuildService` 에 "거래 접두어 밖 자산 Deposit → 다른 임원 Withdraw" 가 거절되는지.

#### O-12 부하 봇이 원장을 쓰는 경로를 하나도 치지 않고, 가벼운 해시 · 닫힌 루프로 잰다
- 분류: 구조(검증 공백) · 확신: 확인
- 근거: 봇의 키트 클라이언트는 계정 · 디렉터리 · 채팅 · 친구 · 순위 · 매칭 · 라이브옵스 일곱이다(`Source/Programs/OnlineLoadBot/LoadBot.cpp:125-128`). 동작 표(`LoadBotScenario.cpp:33-53`)에 구매 · 영수증 · 우편 수령 · 거래 · 길드 창고 · 점수 제출이 없다. `Config/Server/Online/Shooter3D.online.json` 의 `_bLightPasswordHash: true` 라 O-3 의 병목이 보이지 않는다. 다음 요청을 답 뒤에 내어(`LoadBot.cpp:521`) 서버가 밀리면 요청률이 함께 줄어 꼬리 지연이 낮게 나온다(coordinated omission).
- 상용 도구: k6 · Gatling 은 도착률 고정(열린 루프) 실행기를 따로 둔다.
- 제안: 동작에 `economy_purchase` · `mailbox_claim_all` · `trade_pair` · `leaderboard_submit` · `guild_storage` 를 더한다. `<Load>` 에 도착률 모드를 두고 지연을 "보내려던 시각" 기준으로 잰다. 끝에 `LedgerAuditReport` 균형 검사를 붙인다.
- 확인: 새 시나리오를 PostgreSQL 구성(`Shooter3D.external.json`)에 돌려 Conflict 재시도율 · `kBusy` 비율 · 균형 검사.

### RHI · 그래픽스 자원

#### R-1 DXGI Alt+Enter 를 막지 않는다 — 전체 화면 전환 뒤 테어링 Present 가 실패할 수 있다
- 분류: 결함 · 확신: 추정(API 부재는 확인, 증상은 재현 필요)
- 근거: `Source/` 전체에 `MakeWindowAssociation` · `SetFullscreenState` 호출이 없다. 스왑체인은 지원하면 항상 `ALLOW_TEARING` 으로 만들고(`Source/Engine/Graphics/RHI/DX/DX12/D3D12RHISwapChain.cpp:61`, `Source/Engine/Graphics/RHI/DX/DX11/D3D11RHIDevice.cpp:499-505`) vsync 가 꺼지면 `Present( 0, DXGI_PRESENT_ALLOW_TEARING )` 을 낸다. DXGI 기본 동작이 Alt+Enter 로 전체 화면 상태를 바꾸면 이 플래그는 `DXGI_ERROR_INVALID_CALL` 이고, 엔진은 "Present failed" 를 한 번 적고 계속 돈다(`D3D12RHIDevice.cpp:1826-1845`).
- 상용 엔진: 언리얼 D3D12RHI 는 `MakeWindowAssociation( DXGI_MWA_NO_WINDOW_CHANGES )` 로 DXGI 자동 전환을 끄고 전체 화면을 직접 관리한다.
- 제안: 스왑체인 생성 한 입구(DX11 · DX12 공용 헬퍼)에서 창 연결을 끈다. 전체 화면은 테두리 없는 창 하나로만 제공한다. P0-15 와 같은 묶음이다(Alt 처리).
- 확인: DX11 · DX12, vsync 끔 상태에서 Alt+Enter → `D3D12.PresentFailedHr…` 로그 여부.

#### R-3 정적 메시 버퍼가 CPU 가시 메모리에 산다 (DX12 UPLOAD 힙 · Vulkan HOST_VISIBLE)
- 분류: 성능 · 구조 · 확신: 확인(비용 크기는 측정 필요)
- 근거: `Source/Engine/Graphics/RHI/DX/DX12/D3D12RHIResourceFactory.cpp:726-729 · 765-786` — `createVertexBuffer` · `createIndexBuffer` 가 `D3D12_HEAP_TYPE_UPLOAD` 다. Vulkan 은 `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.cpp:695` 에서 모든 버퍼(정점 · 인덱스 · 구조 · 간접 인자 · 상수)를 `HOST_VISIBLE | HOST_COHERENT` 의 첫 일치 형식으로 잡는다. 외장 GPU 에서 정점 페치 · 컴퓨트 스토리지 버퍼까지 시스템 RAM 힙이 된다. `findMemoryType`(`:601`)은 부를 때마다 `vkGetPhysicalDeviceMemoryProperties` 를 다시 묻는다.
- 상용 엔진: 언리얼 · Godot 은 정적 버퍼를 DEFAULT 힙 / DEVICE_LOCAL 에 두고 스테이징으로 복사한다. 동적 버퍼만 CPU 가시다.
- 제안: `createVertexBuffer` / `createIndexBuffer` 가 용도(정적 · 동적, `RHIBufferUsage`)를 형식으로 받는다. 정적은 장치 로컬 + 전송 링(`RHIStagingAllocator`)으로 올리고 `updateGeometryBufferRegions` 는 동적 버퍼만 받는다.
- 확인: 외장 GPU 에서 메시가 많은 bench 시나리오의 `GPU.<깊이 프리패스 · G버퍼>` 전후, DXGI 예산의 non-local 사용량.

#### R-4 (보강) 자원마다 전용 할당 — 머티리얼 상수 버퍼 하나가 커밋 자원 · `VkDeviceMemory` 하나다
- 분류: 구조 · 확신: 확인 — NEXT RHI(5-2) "Android `VkDeviceMemory` 서브할당" 의 새 근거
- 근거: `Source/Engine/Graphics/Material/MaterialRenderProxy.cpp:48` 이 머티리얼 인스턴스마다 `createConstantBuffer` 를 부른다. DX12 는 버퍼마다 `CreateCommittedResource`(`D3D12RHIResourceFactory.cpp:60-66`, 64 KB 단위 — 기억 기준)라 256 B × 3 칸짜리가 64 KB 를 쓴다. `PassConstantRing.h:34` 의 4096 칸도 같은 길이라 최악 256 MB 다. Vulkan 은 버퍼(`VulkanRHIDevice.cpp:708`) · 텍스처(`VulkanRHIResourceFactory.cpp:675`)마다 `vkAllocateMemory` 이고 `maxMemoryAllocationCount`(데스크톱 드라이버도 흔히 4096)를 읽지 않는다 — Android 만의 일이 아니다. 상수 버퍼마다 bindless 번호도 하나씩 쓴다.
- 상용 엔진: 언리얼 `FD3D12FastConstantAllocator` · Vulkan `FMemoryManager`(서브할당), 유니티 SRP Batcher 는 머티리얼 CBUFFER 를 큰 버퍼 구간으로 둔다. Godot 는 VMA 다.
- 제안: RHI 공용 "버퍼 페이지 + 구간" 형식(`RHIBufferSlice{ buffer, offset, size }`)을 두고 상수 버퍼는 구간으로만 나간다. 메모리는 D3D12MA / VMA 를 vcpkg overlay 로 들이거나 `SpanAllocator` 를 재사용한다. 그 전이라도 `vkAllocateMemory` 수를 세어 한도 근처에서 오류로 멈춘다.
- 확인: 머티리얼 인스턴스 5000 개 시나리오에서 `RHIMemoryKind::Buffer` 바이트와 Vulkan 할당 수.

#### R-6 텍스처 스트리밍이 "나중에 올리기" 뿐이고, 전역 세대 하나가 모든 머티리얼을 다시 돌린다
- 분류: 성능 · 구조 · 확신: 확인(측정 필요)
- 근거: `Source/Engine/Graphics/Texture/TextureCache.cpp:229-232` — 대기 줄이 텍스처마다 `DDSImageData` 전체를 쥐고 프레임당 제출은 2(`TextureCache.h:58`, C++ 상수)라 500 장이 오면 250 프레임 동안 전부 RAM 에 남는다. `:551-680` — 화면 크기는 줄 순서만 정하고 결국 모두 전체 밉으로 올라가며 내리는 길이 없다(`Source/Engine/Visual/TextureStreamingSystem.cpp:30-75`, `Config/Engine/MemoryBudget.json` 의 Texture 1024 MB 를 스트리머가 읽지 않는다). 승급마다 전역 `_reloadGeneration` 을 올려(`:579`) 모든 `Material::refreshTextureBindings`(`Source/Engine/Graphics/Material/Material.cpp:237-275`)와 `MaterialInstance`(`MaterialInstance.cpp:344-349`)가 다시 패킹 · 재업로드한다.
- 상용 엔진: 언리얼은 `FRHITextureReference` 로 텍스처 뒤의 RHI 자원만 바꿔 끼우고, 풀 예산 안에서 원하는 밉을 계산해 올리고 내린다. 유니티 Mipmap Streaming 도 예산 + 밉별 로드다.
- 제안: `Texture2D` 가 bindless 번호를 수명 내내 유지하고 승급은 같은 번호의 디스크립터를 다시 쓴다(전역 세대 · 재패킹이 사라진다). 대기 줄은 경로 + 밉 범위를 들고 제출 때 읽는다. 메시 쿠킹 때 UV 밀도를 구해 원하는 밉을 정하고 예산을 넘으면 낮은 우선도부터 내린다.
- 확인: `load/*` 시나리오에서 Tracy `RT.TextureStreaming`, 머티리얼 `sync` 횟수, 대기 줄 바이트 카운터. 4K 텍스처 300 장 장면을 지나간 뒤 텍스처 메모리가 예산 아래로 돌아오는지.

### 렌더러

#### RN-1 압축을 포기한 투명 배치(Preserve)가 쓰지 않은 가시 칸을 읽어 그린다
- 분류: 결함(조건부) · 확신: 코드 경로 확인, 화면 증상은 추정
- 근거: `Content/engine/shaders/gpu_cull.hlsl:308-325` — 절두체 밖 인스턴스는 `:308` 에서 돌아가 `:321` 의 제자리 쓰기에 닿지 않는데 개수는 CPU 값 전체다(`:289`, `Source/Engine/Renderer/Scene/GPUScene.cpp:440-442`). 가시 버퍼는 초기 내용 없이 만든다(`GPUScene.cpp:295`). 한 번도 보인 적 없는 인스턴스의 칸은 미정 값이나 배치 재구성 전의 다른 인스턴스 번호를 담은 채 그려진다. 조건은 투명 배치 하나의 인스턴스가 512 를 넘을 때(`GPUScene.cpp:417`) — 같은 아틀라스의 스프라이트 · 타일이 길게 이어지는 2D 장면이다.
- 상용 엔진: 언리얼 `FInstanceCullingContext` 는 컬링하지 않는 드로우도 인스턴스 ID 를 매 프레임 채운다. 유니티 BRG 는 visible instance 배열을 컬링 콜백이 전부 쓴다.
- 제안: "개수는 CPU, 목록은 GPU 가 일부만" 조합을 없앤다. Preserve 배치는 컬링 판정 앞에서 제자리 쓰기를 하거나 CPU 가 그 구간에 항등 값을 올린다.
- 확인: 시나리오 — 투명 인스턴스 600 개 한 배치, 절반이 화면 밖, 첫 프레임과 배치 재구성 직후 골든(dx12 · vk). GPU assert 하나 더.

#### RN-2 TAA 에 서브픽셀 지터가 없어 안티에일리어싱이 되지 않는다
- 분류: 결함(화질) · 확신: 확인
- 근거: `Content/engine/shaders/taa.hlsl` 전체 — 재투영, 3x3 이웃 클램프, `kHistoryWeight = 0.9` 뿐이다. `Source/Engine/Renderer` · `Source/Engine/Graphics` 어디에도 jitter · halton · 투영 오프셋이 없다(Grep 0 건). FXAA · SMAA · MSAA 도 없다. 서 있는 카메라에서는 매 프레임 같은 표본이라 계단이 그대로다.
- 상용 엔진: 언리얼 TAA/TSR · 유니티 URP/HDRP · Godot TAA 는 Halton 지터를 투영 행렬에 넣고 속도에서 뺀다.
- 제안: 뷰 설정에 "지터 오프셋" 을 형식으로 둔다. `applyViewProjection` 이 지터 든 행렬과 안 든 행렬을 함께 싣고 속도 · 컬링 · 그림자 맞춤은 안 든 쪽만 받게 서명을 가른다.
- 확인: 단위 `TAAJitterTest`(지터 열 평균 0, 속도 출력에 지터가 섞이지 않음). 시나리오 — 가는 선 격자를 서 있는 카메라로 16 프레임 뒤 골든.

#### RN-3 광원을 뷰와 무관하게 전부 싣고, 1024 를 넘으면 목록 뒤부터 자른다
- 분류: 구조(확장) · 확신: 확인
- 근거: `Source/Engine/Renderer/Lighting/GPULightBuffer.cpp:223-305` `collectSceneLights` 는 활성 광원을 종류 순 · 등록 순으로 모두 싣는다(절두체 · 거리 판정 없음). `limitCPULightCount`(`:370-383` 부근)는 경고 한 번 뒤 뒤를 버린다 — 목록 뒤는 파티클 라이트 · 프로브 볼륨 · 반사 프로브라 넘치면 GI · 반사가 먼저 사라지고, 자유 모양 2D 빛의 꼬리 원소(`:267`)가 중간에서 잘리면 셰이더가 다른 원소를 변으로 읽는다. 상한과 그리드 크기는 HLSL `#define` 이다(`binding_slots.hlsli:341`, `:392-394`).
- 상용 엔진: 언리얼 · URP/HDRP · Godot 는 뷰마다 광원 가시성을 정하고 중요도로 자른 뒤 클러스터에 넣는다.
- 제안: 광원에도 경계 등록부를 두고 뷰마다 컬링한 뒤 중요도(화면 크기 × 세기)로 예산만큼 고른다. 자르는 단위는 "빛 + 꼬리 원소" 묶음. 상한 · 그리드 크기는 Config JSON 에서 읽어 셰이더 define 으로 쿠킹한다.
- 확인: 단위 — 광원 2000 개 중 화면 안 50 개가 모두 남는지, 꼬리 원소가 잘리지 않는지. `load/*` 에 광원 4000 장면.

#### RN-4 그림자 아틀라스가 2x2 고정 사분면이라 로컬 그림자 하나가 방향광 품질을 떨어뜨리고, 넘친 빛은 알림 없이 그림자가 없다
- 분류: 구조 · 확신: 확인
- 근거: `Source/Engine/Renderer/Lighting/ShadowSetup.cpp` `ShadowAtlasLayout::make`(109 부근) — 로컬 타일이 하나라도 있으면 캐스케이드를 2 로 줄이고(`ShadowSetup.h:56`) 캐스케이드 칸이 반 해상도가 된다. 개수는 화면 밖 포함 씬 전체로 센다. 로컬 용량은 점광 하나 + 스폿 둘이 한도이고 넘으면 `populateLocalShadows` 가 로그 없이 `continue` 한다(벽 너머로 빛이 샌다). 타일 번호가 우선순위 순서라 순서가 뒤집히면 서 있는 빛도 `ShadowCache::update`(`ShadowCache.cpp:53-58`)가 캐시 층 전체를 다시 그린다.
- 상용 엔진: 언리얼은 빛마다 그림자 맵 또는 VSM 페이지, URP 는 주광 캐스케이드 맵과 추가 광 아틀라스가 따로이고 칸 크기가 가변이다.
- 제안: 방향광 캐스케이드와 로컬 광을 다른 텍스처로 가르고 로컬 아틀라스는 화면 크기에 따라 칸을 나누는 할당기로 바꾼다. 캐시 키는 빛의 정체성(컴포넌트 ID, 면). 칸을 못 받은 빛은 프레임 통계와 로그로 알린다.
- 확인: 단위 `ShadowSetupTest` — 스폿 하나를 더해도 캐스케이드 해상도 · 수가 그대로, 우선순위가 뒤집혀도 서 있는 빛이 Reuse.

#### RN-5 점광 · 스폿 타일과 추가 뷰 캐스케이드가 합친 컬링 칸 하나를 쓴다
- 분류: 성능 · 확신: 확인(비용은 측정 필요)
- 근거: `Source/Engine/Renderer/Frame/Execute/FramePassExecutorDraw.cpp:247-249` — 주 시점 캐스케이드만 자기 칸이고 나머지는 방향광 볼륨과 모든 로컬 경계를 합친 직교 상자다. 점광 하나가 방향광 볼륨 전체를 6 번 정점 처리한다. 인스턴스 배치 항목(지형 청크 · 식생)은 정적이 아니라고 봐서(`Source/Engine/Renderer/Scene/GPUSceneBuilder.cpp:462`) 캐시한 타일에서도 매 프레임 다시 그린다.
- 상용 엔진: 언리얼 · 유니티는 그림자 절두체(빛 · 슬라이스)마다 캐스터를 컬링한다.
- 제안: 타일마다 컬링 칸을 주거나 인스턴스마다 타일 비트마스크를 만든다. 식생 · 지형 배치에 "정적 그림자 캐스터" 를 선언할 수 있게 한다.
- 확인: `GPU.PassMs`(Shadow)와 `RT.Draw.gpuBatches` 를 점광 0 · 1 · 2 개로 비교(Release, 식생 장면).

#### RN-6 포함 여부가 하나라도 바뀌면 GPUScene 을 통째로 다시 모으고 다시 올린다
- 분류: 구조(확장) · 확신: 확인(비용은 측정 필요)
- 근거: `Source/Engine/Renderer/Scene/GPUSceneBuilder.cpp:830-835` — 더티 슬롯의 "실릴지" 가 바뀌면 부분 수집을 버리고 `collectAllCandidates` 로 간다(집합 세대가 바뀌어도 같다, `:726`). 식생은 카메라가 움직일 때마다 셀을 켜고 끄고(`FoliageComponent.cpp:763` → `MeshInstanceBatch.cpp:107-114`), 스폰 · 소멸 · 스트리밍도 같은 길이다.
- 상용 엔진: 언리얼 `FScene` 은 프리미티브마다 오래 가는 GPUScene 칸(프리리스트)에 증분으로 올린다. 유니티 GPU Resident Drawer 는 오래 가는 인스턴스 ID 다.
- 제안: 인스턴스 번호를 "배치 순서로 압축한 자리" 에서 "오래 가는 칸 + 프리리스트" 로 바꾸고 배치 소속은 배치마다의 인스턴스 ID 목록으로 간접화한다. 숨김은 플래그로 둔다.
- 확인: `load/*` 에 오브젝트 20 만 + 매 프레임 스폰 10 · 소멸 10, Tracy `GT.GPUScene.build.collect` · `batchKeys` · `publish`.

#### RN-7 지형 청크 LOD 가 바뀔 때마다 CPU 가 메시를 새로 만들고 다시 올린다
- 분류: 성능 · 구조 · 확신: 확인(비용은 측정 필요)
- 근거: `Source/Engine/Environment/Terrain/TerrainComponent.cpp:919-966` `updateLODs` → `:866-908` `rebuildChunkMeshes` — LOD 나 이웃 LOD 가 바뀐 청크마다 정점 · 인덱스를 다시 만들고 새 `Mesh` 로 교체한다. 프레임당 상한이 없어 카메라 컷이면 전 청크가 한 프레임에 돈다. 시점은 하나뿐이라(`:270-272`) 분할 화면 · CCTV 는 주 카메라의 LOD 를 본다.
- 상용 엔진: 언리얼 랜드스케이프는 LOD 별 공유 인덱스 버퍼와 정점 셰이더 모핑이라 전환에 CPU 재생성이 없다.
- 제안: 청크마다 최고 해상도 정점 한 벌과 (LOD, 이웃 마스크)별 공유 인덱스 버퍼로 바꿔 LOD 전환을 인덱스 구간 선택으로 만든다(지오모프는 이미 정점 셰이더에 있다).
- 확인: Tracy 구간을 `rebuildChunkMeshes` 에 두고 2 km 지형을 20 m/s 로 가로지르는 경로와 컷.

#### RN-8 자동 렌더 배율이 벽시계 프레임 시간으로 판단하고, 단계마다 뷰 자원 전체를 다시 만든다
- 분류: 결함 + 구조(기본 끔) · 확신: 확인
- 근거: `Source/Engine/Renderer/Frame/Packet/RenderPacketViewSetup.cpp:62-97` — 프레임 델타를 넣는다. 수직동기 60 Hz 에서는 "여유" 조건(예산 × 0.8)이 성립하지 않아 한 번 내려간 배율이 올라오지 않고, CPU 병목에서도 해상도만 깎인다. `Source/Engine/Renderer/Frame/View/FrameViewResources.cpp:214-256` — 풀 크기가 바뀌면 모든 첨부 · TAA 기록 · 노출 기록을 다시 만들고 `_bHasHistory = SW_FALSE` 로 둔다.
- 상용 엔진: 언리얼 Dynamic Resolution · 유니티(`FrameTimingManager`)는 GPU 시간으로 판단하고 타깃은 최대 크기로 두고 뷰 사각형만 바꾼다.
- 제안: 입력을 `GPUPassTimeline` 의 GPU 프레임 시간으로 바꾼다. 풀은 최대 배율 크기로 한 번 만들고 "쓰는 사각형" 을 뷰 상수로 흘린다.
- 확인: 단위 `RenderScaleAdjusterTest` 에 "16.67 ms 고정 입력에서 내려간 배율이 돌아온다". 시나리오 — 배율을 바꾼 프레임에 `RT.Frame.transientAllocations` 0.

#### RN-9 GPU 방출기가 스냅샷에서 한 프레임 빠졌다 돌아오면 처음부터 빨리감기로 다시 돈다
- 분류: 결함 · 확신: 코드 경로 확인, 재현은 아직
- 근거: `Source/Engine/VFX/SceneVFXProvider.cpp` `contributeParticles` 는 `isActive() == false` 인 컴포넌트를 싣지 않고, 렌더 쪽은 그 프레임에 없는 재생의 상태를 놓는다(`Source/Engine/Renderer/VFX/VFXGPUSimulation.cpp:289-306`). 다시 실리면 새 상태(`:363-375`)가 `request._stepCount` 전체를 프레임당 32 단계로 따라잡는다(`:433-434`). 10 분 돈 횃불은 약 19 초 동안 32 배속으로 재생된다.
- 상용 엔진: 나이아가라는 다시 켤 때 리셋하거나 warm-up 하고 보이지 않으면 시뮬레이션을 멈춘다.
- 제안: 입자 수명이 `_lifetimeMax` 로 묶이므로 새 상태는 `stepCount - ceil( lifetimeMax / step )` 부터만 디스패치하고 그 앞은 시계만 감는다. 상태 해제는 재생 토큰이 죽었을 때로 바꾼다.
- 확인: 단위 — 3600 단계 진행한 요청을 새 상태에 주면 첫 프레임 디스패치 단계 수가 수명 창 이하.

### 에셋 · 직렬화 · 리플렉션 · 오브젝트 · 씬

#### A-2 컴포넌트 · 중첩 프리팹 부품의 영속 정체성이 순번이다
- 분류: 구조(잠재 데이터 오염) · 확신: 확인
- 근거: `Source/Engine/Object/Component/ComponentStableKey.cpp:32-58` — 키가 `이름#같은 이름 중 몇 번째` 다. 프리팹 덮어쓰기(`PrefabOverrides.cpp:85-103`)와 세이브의 컴포넌트 핸들(`ObjectStateSerializer.cpp:346,453`)이 이 키를 쓰고, 중첩 부품 키는 노드 인덱스다(`PrefabAsset.cpp:499`). 앞 형제를 지우거나 끼우면 기존 씬 덮어쓰기와 쌓인 세이브의 참조가 경고 없이 다른 대상으로 간다.
- 상용 엔진: 유니티는 파일 안 고정 fileID, 언리얼은 유일 컴포넌트 이름과 SCS `VariableGuid`, Godot 은 유일성이 강제되는 노드 이름 경로다.
- 제안: 컴포넌트 · 중첩 노드에 저작 시점 고정 지역 id(64 비트)를 주고 키 · 부품 경로 · 세이브 핸들이 그것을 쓴다. 옛 형식은 `SchemaMigrate` 로 한 번 옮긴다(에셋 GUID 작업과 같은 줄기).
- 확인: 단위 `PrefabOverridesTest.OverrideFollowsComponentWhenAnEarlierSiblingIsRemoved`, `ObjectStateSerializerTest.SavedHandleSurvivesSiblingInsertion`.

#### A-5 사용자 설정 판 번호가 스키마 여럿의 최댓값 하나다
- 분류: 결함(잠재 — 첫 `<Upgrade>` 단계를 더하는 날 드러난다) · 확신: 확인
- 근거: `Source/Engine/UserSettings/UserSettingsSchema.cpp:661` — `_version = max( _version, overlay._version )` 이고 파일에는 그 값 하나만 적는다(`UserSettingsFileEntry.cpp:139`). 엔진 3 · 게임 1 로 나간 파일에서 게임이 2 로 올려도 단계가 돌지 않고 옛 키는 버려진다. 새 판 파일을 옛 빌드가 읽으면 경고만 하고(`:129-130`) 옛 번호로 다시 써, 새 빌드로 돌아오면 멱등이 아닌 `Scale` 단계가 한 번 더 돈다.
- 상용 엔진: 언리얼 `FCustomVersion` 은 주인(GUID)마다 번호를 따로 적는다.
- 제안: 파일에 스키마 주인별 판(`"versions": { "engine": 3, "game": 2 }`)을 적는다. 빌드보다 새 파일은 덮어쓰지 않고 옆에 보관하고 모르는 키는 원문 그대로 다시 쓴다.
- 확인: 단위 `UserSettingsFileEntryTest.GameUpgradeRunsWhenEngineVersionIsHigher`, `NewerFileIsNotRewrittenAsOlder`.

#### A-6 플레이어 세이브에 판 올림 길이 없다 — 봉투는 같은 판만, 구간은 정확히 같은 판만, 상태는 색인 · 개수에 묶임
- 분류: 구조(세이브 호환) · 확신: 확인 — DesignBacklog "데이터 버전 정책은 감사하지 않았다" 의 대조 결과
- 근거: `Source/Engine/Serialization/Format/StateIO.cpp:116-123` — `readHeader` 가 `readVersion == version` 일 때만 통과한다(약 30 곳이 `io.header( kStateTag, kStateVersion )` 를 쓴다). 봉투도 같다(`Source/GameFramework/Runtime/Foundation/Framework/Save/GameStateSnapshot.cpp:33, 179` `kVersion = 5`, `GameInstance.cpp:578-583` "옛 형식 리더는 두지 않는다"). 판 올림 사슬(`Source/Engine/Serialization/Format/FormatUpgrade.h:22-31`)은 봉투 전체 바이트 단위이고 `SW_SAVE_FORMAT_UPGRADE` 등록이 0 건이다. 이미 올린 곳이 있다(`ShooterGameModeComponent.cpp:43` 3, `FarmGameModeComponent.cpp:45` 4) — 올리면 옛 세이브는 새 판이 된다. 상태가 색인 · 개수로 짝짓는 자리: `Shop.cpp:544-551`, `SpawnDirector.cpp:253-259`, `Weapon.cpp:246-248`, `WeatherSystem.cpp:218`, `ElementGrid.cpp:389-390`. 같은 저장소의 `QuestLog::serializeState`(`QuestLog.cpp:440-448`)는 id 로 짝짓고 `reconcileWithDefinition` 으로 맞춘다 — 규칙이 둘로 갈려 있다. MMO 프로필은 형식이 다르면 로그인을 거절한다(`Source/Games/LanternVale/ValeProfileStore.cpp:71-80` — 전 계정이 잠긴다).
- 상용 엔진: 언리얼 `FCustomVersion` + `Ar.CustomVer()` + CoreRedirects, 유니티 `FormerlySerializedAs`, Godot 은 리소스 `format` 호환 읽기다.
- 제안: `StateIO::header` 를 읽은 판을 돌려주는 `versioned( tag, current, upgradeTable )` 로 바꾼다. 구간 종류마다 n → n+1 단계를 등록하고 판이 다른데 단계가 없으면 오류다(올릴 때 승계 함수가 없으면 빌드가 진다). 구간은 길이 머리를 써 모르는 구간을 건너뛰고 보존한다. 상태 직렬화 규칙을 "카탈로그 항목은 id 로, 정의와 다르면 reconcile" 하나로 통일하고 색인 저장은 핫 리로드 목적에만 허용한다(목적 인자로 형식이 가른다).
- 확인: `Test/Fixtures/formats/save/<태그>/v<n>` 표본을 올려 읽는 단위 테스트. 등록 검사 `StateSectionUpgradeTest.EveryBumpedSectionHasASteps`. `ShopStateTest.SaveSurvivesAddedStock`, `SpawnDirectorTest.SaveSurvivesReorderedTable`.

#### A-7 공유 에셋의 제자리 다시 읽기가 `shared_ptr<const T>` 를 쥔 다른 스레드와 겹칠 수 있다
- 분류: 결함(잠재 경쟁) · 확신: 구조 확인, 발현은 추정
- 근거: `Source/Engine/Asset/Cache/SharedAssetTable.h:104-115` — `reloadShared` 가 `*live = std::move( fresh )` 로 살아 있는 객체를 덮는다. 캐시는 워커에서 불러도 된다고 계약하고(`SharedAssetCache.h:100-105`) `shared_ptr<const AssetType>` 를 나눠 준다. 다시 읽기는 배포에도 있다: 매 프레임 `reloadComposedDependents`(`Source/Engine/Engine.cpp:452-455` — 품질 · 언어 태그 변경)와 `ModSession.cpp:322`.
- 상용 엔진: 언리얼은 다시 가져올 때 새 객체를 만들고 게임 스레드에서 참조를 바꾼다(비동기 로딩 flush 뒤).
- 제안: 다시 읽기 방식을 `Detach`(새 객체 + 다시 읽은 횟수) 하나로 줄여 `const` 를 실제 불변으로 만든다.
- 확인: TSan 빌드에서 `SharedAssetCacheTest.ReloadWhileWorkerReads`.

#### A-8 ReflectionParser 증분 판정에 명령줄이 없다
- 분류: 결함 · 확신: 확인(재현은 안 함)
- 근거: `Source/Programs/ReflectionParser/GeneratedFiles.cpp:226-249` — "도구 입력" 이 실행 파일 · builtins · 주석 메타 · 설정 JSON · 템플릿의 쓰기 시각뿐이다. `--module` · `--target-arg -D…` · `--include` 는 생성 내용을 정하지만 스탬프에 없다. 대상의 모듈 이름 · `SW_WITH_*` 정의를 바꾼 빌드 폴더에 옛 `.gen.cpp` 가 남는다.
- 상용 엔진: 언리얼 UBT 는 UHT 매니페스트(모듈 정의 · 컴파일 환경)가 바뀌면 그 모듈 생성 코드를 무효로 한다.
- 제안: 스탬프에 `args <해시>` 줄을 필수로 둔다. 줄이 없거나 다르면 낡은 것으로 본다.
- 확인: `Test/ReflectionParser/RunParserToolInputs.cmake` 에 `MODE=args-changed`.

#### A-9 패치 서명 검증이 기동마다 패치 팩 전체를 메모리로 읽어 해시한다
- 분류: 성능 · 확장 · 확신: 확인(시간은 측정 필요)
- 근거: `Source/Engine/Content/Pack/PackManager.cpp:685-703` — `readFile( packPath, packBytes )` 로 패치 팩을 통째로 읽고 `PackPatch.cpp:86-99` 가 SHA-256 을 낸다(2 GiB 이상은 거절). 사슬의 모든 패치가 대상이고 매 기동마다 돈다. 검증한 바이트와 마운트가 여는 파일은 다른 핸들이다.
- 상용 엔진: 언리얼 pak 서명은 블록(64 KB)별 해시 표(`.sig`)를 서명하고 읽을 때 블록마다 확인한다.
- 제안: 목록이 서명하는 것을 "FAT + 스트링 풀의 해시" 로 바꾸고 항목 무결성은 FAT 의 항목별 해시로 읽을 때 확인한다.
- 확인: 1 GiB 패치 팩 fixture 로 기동 시간 · 최고 메모리(`Asset.mount` 구간) 전후.

#### A-10 팩 쓰기가 원본 · 압축 결과 · 팩 이미지를 모두 메모리에 든다
- 분류: 확장 · 확신: 확인
- 근거: `Source/Engine/Content/Pack/PackWriter.cpp:132-139` 가 항목 바이트를 모두 들고 `:446` 이 팩 전체를 한 버퍼에 조립해 `:507` 에서 한 번에 쓴다. `Source/Engine/Asset/Cook/CookStore.cpp:90-98` 은 쓴 팩을 다시 통째로 읽어 해시하고 DDC 에 사본을 둔다. 팩은 도메인당 하나(`PackCooker.cpp:642-656`)라 에셋 하나가 바뀌어도 그 도메인 팩 전체를 다시 쓴다.
- 상용 엔진: 언리얼 IoStore 는 청크 단위로 스트리밍해 쓰고 컨테이너를 청크 id 로 나눈다. 유니티는 번들 단위다.
- 제안: PackWriter 를 "항목을 하나씩 받아 압축해 파일에 이어 쓰고 끝에 색인을 쓰는" 스트림으로 바꾼다. 팩 분할 규칙(크기 · 폴더 · 청크 id)은 PackConfig 에 둔다.
- 확인: 10 GB 합성 콘텐츠로 `--cook-packs` 최고 메모리와 시간.

#### A-11 증분 쿠킹의 "바뀐 것 없음" 판정이 매 실행 모든 입력 · 출력을 읽는다
- 분류: 성능(+ 잠재 결함) · 확신: 구조 확인, 비용은 측정 필요
- 근거: `Source/Engine/Asset/Cook/CookRecord.cpp:154-158` — 기록의 입력마다 `computeCurrentHash` 를 낸다. 해시 캐시(`Source/Engine/Content/ContentHash.cpp:122-158`)는 프로세스 메모리에만 있다. 잠재 결함: `computeBytes`(`:55-66`)가 바이너리 입력에서도 0x0D 를 건너뛰어 0x0D 의 자리만 다른 두 바이너리가 같은 해시가 된다.
- 상용 엔진: git 색인 · 언리얼 `FFileHashCache` · 유니티 AssetDatabase 는 (크기, 시각) → 해시 표를 디스크에 둔다.
- 제안: `ContentHashCache` 표를 `DerivedData` 에 영속한다. CR 정규화는 텍스트 종류 입력에만 하고 해시 함수는 xxh3 계열로 바꾼다.
- 확인: 변경 없는 `--cook-all` 두 번째 실행의 읽은 바이트 수 · 시간. 단위 `ContentHashTest.BinaryWithMovedCRHashesDifferently`.

#### A-12 팩 색인의 메모리와 항목 통째 압축
- 분류: 확장 · 성능 · 확신: 구조 확인, 비용은 측정 필요
- 근거: `Source/Engine/Content/Pack/PackReader.cpp:464-510` — 마운트 때 항목마다 `PackFileEntry`(경로 `string` 사본 포함)를 `unordered_map` 에 넣고 스트링 풀 원본도 계속 든다. 항목은 통째로 압축돼 있고 읽기 API 는 `readFile` · `readFileAsync` 뿐이라 부분 읽기가 없다(R-6 의 밉별 로드가 여기에 막힌다).
- 상용 엔진: 언리얼 IoStore 는 완전 해시 TOC 와 고정 크기 압축 블록, 유니티 LZ4 번들은 청크 단위 임의 접근이다.
- 제안: FAT 이 이미 경로 해시 오름차순이니 정렬 배열 그대로 이진 탐색하고 경로는 스트링 풀 오프셋으로만 가리킨다. 큰 항목은 블록 표(64 ~ 256 KB)로 나눠 `readRange( pathHash, offset, size )` 를 둔다.
- 확인: 항목 30 만 개 합성 팩의 마운트 시간 · 색인 메모리.

#### A-13 씬 스트리밍: 모든 셀의 서술이 메모리에 있고, 셀 짓기가 게임 스레드 동기이며, 내릴 때 상태를 버린다
- 분류: 확장 · 성능 · 확신: 확인(히치 크기는 측정 필요)
- 근거: `Source/Engine/Scene/SceneStreamingSubsystem.cpp:61-66` — 씬 로드 때 모든 셀의 노드를 `slot._listNode` 로 복사해 계속 든다. `:186-190,222-235` — 셀 짓기는 `DestroyPost` 에서 `instantiateCell` 을 동기로 부르고 예산은 프레임당 셀 수뿐이다. `Source/Engine/Scene/Scene.cpp:624-640` — 셀을 내릴 때 오브젝트를 파괴만 해서 다시 올리면 그 사이 바뀐 상태가 사라진다.
- 상용 엔진: 언리얼 World Partition 은 셀을 따로 패키지로 비동기 로드하고 AddToWorld 를 시간 예산으로 나눈다. 유니티는 서브씬을 비동기로 읽는다.
- 제안: 쿠킹이 셀마다 구간을 쓰고 런타임은 셀 색인만 든다. 셀 로드는 워커에서 파싱 · 인스턴스화하고 등록은 `pumpHeldRegistration` 예산을 쓴다. 셀을 내릴 때는 `SaveGame` 표시가 있는 상태를 셀 키로 보관한다(G-7 과 같은 줄기).
- 확인: `load/` 에 셀 400 × 오브젝트 2000, `GT.Scene.service.SceneStreamingSubsystem` 최대 프레임 시간.

#### A-15 프리팹 스폰이 인스턴스마다 XML 텍스트를 파싱한다
- 분류: 성능 · 확신: 구조 확인(통합 단계에서 다시 읽지 않음), 비용은 측정 필요
- 근거: `Source/Engine/Object/Prefab/PrefabAsset.cpp:1017,1021` — 쿠킹본(`.prefab.bin`)이 상태를 XML 문자열로 싣고 `:1065-1066` 의 `applyStateTo` 가 스폰마다 `loadFromXMLString` 으로 파싱한다. 중첩 저작본은 스폰마다 XML 을 합친 뒤 파싱한다(`PrefabCache.cpp:504,580`).
- 상용 엔진: 언리얼은 아키타입에서 프로퍼티를 복사하고 유니티 `Instantiate` 는 오브젝트 그래프를 복제한다. Godot PackedScene 은 색인 + Variant 배열이다.
- 제안: 쿠킹이 상태를 `BinarySerializer` 본문으로 굽거나, 캐시가 프리팹마다 원형 오브젝트를 한 번 짓고 리플렉션 복사로 인스턴스를 만든다.
- 확인: `load/` 에 프리팹 하나를 1000 번 스폰하는 시나리오, Tracy `PrefabCache::spawn`.

#### A-16 쿠킹된 이진 번역 표가 어디서도 쓰이지 않는다
- 분류: 성능 · 구조 · 확신: 사용처 없음 확인(통합 단계에서 다시 읽지 않음)
- 근거: `Source/Engine/Localization/LocalizationManager.cpp:418,449,461` — `loadBinaryTable` · `cookBinaryTable` 의 호출이 Localization 폴더와 테스트 밖에 없다. 배포 팩은 저작 XML 을 담고 `readProject`(`:174-250`)가 모든 문화권 XML 을 읽으며 `rebuildTablesLocked`(`:541-636`)는 마운트마다 모든 프로젝트 × 문화권을 다시 채운다.
- 상용 엔진: 언리얼은 `.locres` 를 쿠킹해 지금 문화권과 폴백 사슬만 읽는다.
- 제안: 쿠킹이 문화권마다 `cookBinaryTable` 을 부르고 배포는 조회 사슬의 표만 읽는다. 쓰지 않을 것이면 형식과 등록을 지운다.
- 확인: 키 20 만 × 문화권 15 합성 프로젝트로 `Localization.mount` · 메모리 비교.

### 물리 · 캐릭터 · 애니메이션 · 내비 · 오디오

#### S-2 Jolt 임시 할당자가 고정 16 MB 이고 넘치면 `std::abort()` — 한도는 C++ 상수
- 분류: 결함(잠재 크래시) + 구조 · 확신: 확인(설치 헤더로 확인)
- 근거: `Source/Engine/Physics/Jolt/JoltPhysicsScene.h:352` 가 `JPH::TempAllocatorImpl` 이고 `JoltPhysicsScene.cpp:42-45` 에 `kMaxBodyCount 65536 · kMaxBodyPairCount 65536 · kMaxContactCount 32768 · 16 MB` 가 하드코딩이다. Jolt 5.6.0 `Jolt/Core/TempAllocator.h:68-72` 는 넘치면 `Trace` 뒤 `std::abort()` 라 엔진 로그 · 크래시 처리기를 타지 않는다. 같은 할당자를 스텝 · 캐릭터 `ExtendedUpdate`(`:1590`) · 소프트 바디(`:299`)가 함께 쓰고, 미리보기 씬마다 16 MB 를 또 잡는다.
- 상용 엔진: Jolt 는 `TempAllocatorImplWithMallocFallback` 을 제공한다(같은 헤더 `:168`). Chaos · PhysX 는 스크래치가 모자라면 힙으로 넘긴다.
- 제안: 네 값을 `PhysicsSettings`(데이터)로 옮기고 없으면 로드 오류로 둔다. 할당자는 폴백형으로 바꾸되 폴백이 일어난 스텝을 카운터 · 경고로 올린다.
- 확인: 단위 `PhysicsScene3DTest.LargeActivePileDoesNotAbort`(작은 할당자 설정으로 재현), 감시 값 `Physics.TempAllocator.peakBytes`.

#### S-5 2D 블렌드 스페이스가 역거리 가중이라 표본 전부가 늘 켜진다
- 분류: 성능 + 품질 · 확신: 확인
- 근거: `Source/Engine/Animation/Graph/BlendSpace.cpp:46-81` — 표본 위에 정확히 있지 않으면 모든 표본의 가중치가 0 보다 크다. `Source/Engine/Animation/Component/SkeletalAnimatorComponent.cpp:1321-1332` 는 가중치가 0 보다 크면 샘플하므로 표본 N 개의 클립을 평가마다 전부 샘플 · 블렌드한다. 알림 기본 모드가 `All`(`AnimationGraphAsset.h:99`)이라 표본 전부가 알림을 낸다. 두 축의 단위를 정규화하지 않아 한 축이 거리를 지배한다.
- 상용 엔진: 언리얼 Blend Space 는 들로네 삼각분할(표본 많아야 3 개)이고 축마다 정규화한다. Godot `AnimationNodeBlendSpace2D` 도 삼각형이다.
- 제안: 그래프 에셋을 읽을 때 삼각분할을 구워 두고 런타임은 무게중심 좌표 3 개만 낸다. 축 범위는 에셋 값으로 정규화한다.
- 확인: 단위 `BlendSpaceTest.TwoAxisActivatesAtMostThreeSamples`. 측정 — 9 표본 이동 블렌드 캐릭터 500 의 `GT.Animation` BasePose 구간.

#### S-6 내비메시가 32 비트 폴리 참조(22 비트)에 묶이고 에이전트 종류당 한 장이다
- 분류: 구조(확장 한도) · 확신: 확인(헤더 확인)
- 근거: 설치된 `recastnavigation/DetourNavMesh.h:28` 에 `DT_POLYREF64` 가 주석 처리되어 `dtPolyRef` 가 32 비트다. `Source/Engine/Navigation/Recast/RecastNavMesh.cpp:34` `kTileAndPolyBits = 22`, `:405-420` 에서 타일 수 × 타일당 폴리 ≤ 4M 이다. 엔진 쪽 `NavPolyRef` 는 uint64 인데 `Source/Engine/Navigation/Recast/DetourNavCrowd.cpp:249` 가 `static_cast<uint32>` 로 자른다. 내비메시는 씬 경계 전체를 한 장으로 잡고 스트리밍이 없다.
- 상용 엔진: 언리얼은 64 비트 폴리 참조에 내비메시 스트리밍(월드 파티션 · 인보커), 유니티는 `NavMeshSurface` 를 조각으로 붙였다 뗀다.
- 제안: vcpkg 오버레이 포트로 `DT_POLYREF64` 를 켜고 자르는 캐스트는 `static_assert( sizeof( dtPolyRef ) == sizeof( NavPolyRef ) )` 로 막는다. `INavMeshTileStore` 위에 관심 영역 타일만 올리는 층을 둔다.
- 확인: 단위 `RecastNavMeshTest.LargeGridFitsPolyRef`(8192 타일).

#### S-7 캐릭터 컨트롤러는 기본 설정에서 월드에 바디가 없다 — 캐릭터끼리 · 트리거 · 강체가 보지 못한다
- 분류: 결함 후보 · 확신: 추정(코드 사실은 확인, 게임 동작은 프리팹 구성에 달림)
- 근거: `Source/Engine/Physics/PhysicsDesc.h:168` 과 `CharacterControllerComponent.cpp:63` 에서 `_bInnerBody` 기본이 false 이고 `Content` 의 캐릭터 컨트롤러 XML 7 개와 `Source` 어디에도 켜는 곳이 없다. `JoltPhysicsScene.cpp` 에 `SetCharacterVsCharacterCollision` · `CharacterContactListener` 등록이 없고 `:145-149` 의 캐릭터 필터는 센서를 건너뛴다. Jolt `CharacterVirtual` 은 브로드페이즈에 없으므로(기억 기준) 두 캐릭터가 서로 통과하고, 트리거 겹침 이벤트에 캐릭터가 잡히지 않는다.
- 상용 엔진: 언리얼 캐릭터는 캡슐이 실제 바디다. Jolt 샘플은 `CharacterVsCharacterCollisionSimple` + 속 바디를 쓴다.
- 제안: 캐릭터를 만들 때 속 바디를 기본으로 두고 씬이 캐릭터끼리 충돌 인터페이스를 등록한다. 먼저 캐릭터 프리팹이 따로 키네마틱 바디를 달아 메우고 있는지 확인한다.
- 확인: 시나리오 — 캐릭터 둘을 마주 걷게 해 거리 유지, 트리거 통과 때 `Physics.ContactLog` 의 겹침 Begin · End.

#### S-8 (보강) 캐릭터 이동이 스텝마다 게임 스레드에서 한 명씩 돈다
- 분류: 성능 · 확신: 확인 — NEXT 5-3 "캐릭터 컨트롤러 입력 SoA" 의 보강
- 근거: `Source/Engine/Physics/Component/CharacterControllerComponent.cpp:310-377` 의 `prePhysicsStep` 이 캐릭터마다 `moveCharacter` 를 차례로 부른다. `JoltPhysicsScene.cpp:1589-1590` 의 `ExtendedUpdate` 는 씬 공용 `_tempAllocator`(스레드 안전하지 않음, S-2)를 받는다. 입력을 SoA 로 옮겨도 이 할당자와 `_characters` 표 때문에 그대로는 병렬화할 수 없다. 덧붙여 NEXT 5-3 의 "군중 `NavAgentCrowdSync` 일괄 쓰기" 는 이미 `applyTransformBatch` 로 되어 있어 그 줄은 낡았다.
- 상용 엔진: 유니티 DOTS Character Controller 는 잡으로 일괄 처리한다.
- 제안: SoA 로 옮길 때 `moveCharacters( span )` 일괄 API 로 백엔드에 넘기고 레인마다 임시 할당자를 둔다.
- 확인: `load/*` 캐릭터 500 사례의 Tracy PreStep 구간(워커 1 · 4 · 8).

### 플랫폼 · 창 · 입력 · 런타임 UI · 빌드

#### P-2 리사이즈를 메시지 처리 안에서 동기로 하고, Win32 모달 이동 · 크기 루프 동안 프레임이 멈춘다
- 분류: 구조 · 성능 · 확신: 확인(코드 경로), 체감 비용은 측정 필요
- 근거: `Source/Engine/Renderer/EngineRenderDriver.cpp:187-196` — 크기 통보마다 `waitIdle( Resize )` 와 `device.resize()` 를 한다. `Source/Engine/Window/Linux/X11Window.cpp:642-654` 는 `while ( XPending )` 안에서 `ConfigureNotify` 마다 불러 한 펌프에 스왑체인을 여러 번 다시 만든다. Win32 는 `processMessages`(`Win32Window.cpp:336-348`)가 `DispatchMessageW` 안의 모달 루프에 갇혀 제목 줄을 끄는 동안 `App::tickMainLoop` 가 돌지 않는다(리슨 호스트 틱 · 오디오 갱신 정지).
- 상용 엔진: Godot 4 `DisplayServerWindows` 는 `WM_ENTERSIZEMOVE` 에서 타이머를 걸고 `WM_TIMER` 에서 `Main::iteration()` 을 돌린다. 언리얼은 모달 크기 루프 안에서 Slate 를 틱한다.
- 제안: 플랫폼 창은 "대기 크기" 만 적고 프레임 시작에서 한 번 적용한다. Win32 는 모달 루프 동안 타이머로 `tickMainLoop` 를 부르되 재진입 가드를 창 형식에 둔다.
- 확인: 카운터 `RHI.ResizeCount`. 시나리오 `engine/automation/windowdrag` 에서 `WM_ENTERSIZEMOVE` 뒤 `Engine.FrameIndex` 가 증가하는지.

#### P-3 웹 · Android: 포커스를 잃어도 입력 관리자가 몰라 눌린 키 · 버튼이 남는다
- 분류: 결함 · 확신: 확인
- 근거: `Source/Engine/Window/PlatformInputEvent.h:13-31` 의 `PlatformInputKind` 에 포커스 종류가 없다. `Source/Engine/Window/Web/WebWindow.cpp:255-260` 과 `Android/AndroidWindow.cpp:391-394` 는 `IWindow::setFocusState`(페이서용 비트)만 부른다. `makeFocusChange` 를 내는 곳은 Win32 와 X11 뿐이라 `resetAllDeviceState` 가 돌지 않는다. 웹에서 W 를 누른 채 다른 창으로 가면 캐릭터가 계속 달린다.
- 상용 엔진: 언리얼은 `OnWindowActivationChanged` 에서 `FSlateApplication` 이 눌린 키를 모두 뗀다(모든 플랫폼 공통 경로). 유니티 Input System 은 `backgroundBehavior` 다.
- 제안: 포커스의 주인을 하나로 한다. `IWindow::setFocusState` 가 값이 바뀌면 포커스 델리게이트를 부르고 `InputManager` 가 그것만 구독한다. 플랫폼 입력 파일의 개별 `makeFocusChange` 는 지운다.
- 확인: 단위 `PlatformInputTest.FocusLossReleasesHeldKeys`(`CallbackInputWindow` 가짜 창).

#### P-4 웹 · Android: 글 입력 경로가 없다(소프트 키보드 · IME 조합)
- 분류: 결함 · 확신: 확인
- 근거: `Source/Engine/Window/IWindow.h:494-496` 의 `applyTextInputWanted` · `applyTextInputArea` 를 Android · 웹 창이 재정의하지 않는다(Grep 0 건). Android 는 `PlatformInputKind::Text` 를 만드는 코드도 없다. 웹은 `WebWindow.cpp:199-209` 의 `keypress` 만 받아 한글 · 일본어 조합(`composition*`)과 모바일 가상 키보드가 오지 않는다. 채팅 · 이름 입력 화면은 이 두 플랫폼에서 글을 받지 못한다.
- 상용 엔진: 언리얼은 Android 가상 키보드 위젯, 유니티는 `TouchScreenKeyboard` 와 WebGL 숨은 input 요소, Godot 은 `DisplayServer.virtual_keyboard_show` 다.
- 제안: `applyTextInputWanted` 를 순수 가상으로 바꿔 구현하지 않는 플랫폼은 컴파일이 멈추거나 "지원 안 함" 을 선언하게 한다. 웹은 숨은 `<input>` 에 `beforeinput` · `composition*`, Android 는 GameTextInput 을 쓴다.
- 확인: 단위 `WindowTextInputTest` 에 플랫폼 창별 케이스. `web-check` 에서 `compositionend` 를 넣어 화면 PNG.

#### P-5 웹: 배경 페이싱이 브라우저 메인 스레드를 막고, Android 는 멈춘 동안 10 ms 폴링을 한다
- 분류: 성능 · 확신: 확인(코드 경로), 비용은 측정 필요
- 근거: `Source/Engine/Window/FramePacer.cpp:62-66` — 포커스가 없으면 `sleepUntilNanoseconds`(기본 30 fps)다. 웹 앱은 메인 스레드의 rAF 콜백에서 돌고(`PROXY_TO_PTHREAD` 없음) `MonotonicClock.cpp:75-80` 의 `clock_nanosleep` 은 Emscripten 메인 스레드에서 바쁜 대기다(기억 기준). Android 는 `Source/App/App.cpp:639-644` 에서 멈춘 동안 10 ms 마다 깨어 `ALooper` 를 본다.
- 상용 엔진: 유니티 WebGL 은 `targetFrameRate` 를 `emscripten_set_main_loop_timing` 으로 옮긴다. NDK 견본과 언리얼은 그리지 않을 때 `ALooper_pollOnce( -1 )` 로 막아 둔다.
- 제안: "프레임 끝에 얼마나 쉬는가" 를 `IWindow::waitForNextFrame( deadline )` 가상 하나로 모은다.
- 확인: `web-check` 에 blur 뒤 메인 스레드 long task 측정(chrome-devtools performance trace).

#### P-6 앱 멈춤 수명 이벤트를 렌더러만 받는다
- 분류: 구조 · 확신: 확인(구독자 하나), 오디오 영향은 추정
- 근거: `Source/Engine/Window/IWindow.h:441, 522` — `_onLifecycle` 이 `DelegateSlot`(주인 하나)이고 구독자는 `Source/Engine/Renderer/EngineRenderDriver.cpp:91, 171-185` 뿐이다. `AppSuspended` 에서 프레임이 서므로 `HapticPlayback::advance` 가 돌지 않아 진동이 꺼지지 않고 입력 상태도 리셋되지 않는다. 오디오 장치 스레드가 프레임과 독립이면 홈 버튼 뒤에도 소리가 난다(`Engine/Audio` 에 전체 멈춤 API 가 보이지 않았다).
- 상용 엔진: 언리얼 `FCoreDelegates::ApplicationWillEnterBackgroundDelegate` 는 멀티캐스트라 오디오 · 입력 · 네트워크가 각자 구독한다. 유니티는 `OnApplicationPause` 다.
- 제안: 수명 통보를 멀티캐스트 + 순서 선언(표면 해제는 렌더러가 마지막)으로 바꾸고 멈춤 진입 때 입력 리셋 · 햅틱 정지 · 오디오 서스펜드를 구독자로 건다.
- 확인: 시나리오 — `simulateAppInterruption` 뒤 `Input.AnyDown == 0`, `Haptics.Active == 0`, `Audio.DeviceRunning == 0`.

#### P-9 (보강) 웹 · Android 프리셋이 Shipping 뿐이라 플랫폼 결함을 자동으로 볼 길이 없다
- 분류: 구조 · 확신: 확인(프리셋), 개발 구성이 두 플랫폼에서 지어지는지는 추정
- 근거: `CMakePresets.json` 에 `Web-Shipping` · `Android-Shipping` 만 있다. [PlatformTargets](PlatformTargets.md) 의 "배포 빌드에 시나리오 실행기가 없어 앱 반응을 읽을 길이 없다" 가 이 결과이고, P0-9 ~ P0-11 과 P-3 ~ P-6 이 모두 그 틈에서 나왔다.
- 상용 엔진: 언리얼은 Android · HTML5 에 Development 구성과 Gauntlet 장치 실행, 유니티는 Development Build + Test Framework 플레이어 실행이 있다.
- 제안: `Web-Release` · `Android-Release` 개발 프리셋(DevTools 정적 링크)으로 기존 `-scenario` 를 그대로 쓴다. 그 전에라도 `CallbackInputWindow` 기반 가짜 창 단위 테스트로 플랫폼 입력 논리를 데스크톱에서 고정한다.
- 확인: `PlatformInputTest.*` 를 `ctest -L nogpu` 에 넣는다.

### GameFramework Runtime

#### G-3 제작의 환불 · 취소와 상점 판매가 아이템 개체 상태를 버린다 — 내구도 공짜 수리
- 분류: 결함(악용 경로) · 확신: 확인
- 근거: `Source/GameFramework/Runtime/Gameplay/Inventory/Crafting.cpp:202-207`, `:235-246`(`cancel`) — 재료를 `Inventory::removeItem`(`Inventory.cpp:161-181`, id 로만)으로 빼고 `addItem`(`:116-127`, `slot._durability = pDef->_maxDurability`)으로 돌려준다. 닳은 장비가 재료인 레시피를 `enqueue` 했다가 `cancel` 하면 새것이 되고 꾸미기 · 뗀 부품은 사라진다. `ShopState::sell`(`Shop.cpp:330-331`)도 어느 개체를 파는지 고르지 못한다.
- 상용 엔진: Lyra 는 `ULyraInventoryItemInstance` 를 개체로 옮긴다.
- 제안: 꺼낸 것을 `InventorySlot` 묶음으로 받아 그대로 돌려주는 거래 형식(`InventoryTransaction`: take → commit/rollback). `Equipment::equipFromInventory` 가 이미 사본 복원을 쓴다.
- 확인: 단위 `CraftingTest.CancelReturnsWornInputWithSameDurability`.

### 장르 키트 · 게임 샘플

#### K-4 (보강) 키트 규칙 설정을 게임이 C++ 기본값으로 만든다(데이터 파일이 없다)
- 분류: 구조 · 확신: 확인 — DesignBacklog "데이터 구동 구조 후보 판별" 5 번의 구체 근거
- 근거: `RTSSettings{}`(`Source/Games/StarSkirmish/SkirmishMatch.cpp:64`, `FrontierSkirmishGameModeComponent.cpp:84`), `CitySettings{}`(`FrontierCityGameModeComponent.cpp:75`), `CreatureTownSettings{}`(`MeadowTownGameModeComponent.cpp:160`), `SRPGSettings{}` · `PlatformerSettings{}`(Empty KitDemo), `CoasterPhysicsParams{}`(`ParkGameModeComponent.cpp:296-297`). 구조체는 `PROPERTY( Min · Max )` 까지 갖췄는데 값을 줄 데이터 길이 없다. `ThemeParkSettings`(`ThemePark.h:126`)는 리플렉션도 범위 검사도 없고 `_noArrivalEntryFee` · `_arrivalRatingScale` 이 분모다(`ThemePark.cpp:301-302`). 게임 쪽 리터럴: 슈터 적 체력 곡선(`ShooterGameModeComponent.cpp:428-431`), 복셀 광석 확률(`VoxelGameModeComponent.cpp:229-241`).
- 상용 엔진: Lyra `ULyraExperienceDefinition` · `DataAsset`, 유니티 Boss Room `ScriptableObject`.
- 제안: 키트 `initialize` 가 값 구조체 대신 `AssetRef<…Settings>`(규칙 자산)를 받는다. 기본 생성한 설정으로는 열 수 없고 없으면 오류로 멈춘다.
- 확인: `ResourceDataSchemaTest` 에 규칙 자산 종류 추가, `ThemeParkSimulationTest.RejectsZeroDivisorSettings`.

### 에디터

#### E-1 복구가 미저장 · 플레이 확인을 건너뛰고, 새 씬과 문서 패널은 자동 저장 대상이 아니다
- 분류: 결함 + 구조 · 확신: 확인
- 근거: `Source/Editor/Common/Commands/Scene/EditorAutosaveCommands.cpp:400` — `restore` 는 `loadScene` 을 바로 부른다(`tryOpenScene` 이 하는 `isPlayStoppedForSceneSwap` 과 dirty 확인을 건너뛴다). 지금 씬을 고친 뒤 Restore 를 누르면 그 편집이 묻지 않고 사라진다. `:197` — `getSourcePath().empty()` 면 쓰지 않아 저장한 적 없는 새 씬은 사본이 없다. 대상은 활성 씬뿐이라 dirty 문서 패널(머티리얼 그래프 · 타일맵 · 시퀀서)은 죽으면 전부 잃는다. 쓰기는 UI 스레드 동기이고 끌기 중인지 보지 않는다.
- 상용 엔진: 언리얼은 dirty 맵 · 에셋 패키지를 모두 자동 저장하고(Untitled 맵 포함) 상호작용 중이면 미룬다.
- 제안: 복구는 `tryOpenScene` 과 같은 입구로 보낸다. 자동 저장 대상을 "dirty 문서 목록" 으로 올리고 새 씬은 임시 id 로 쓴다. 상호작용 중이면 한 프레임 미룬다.
- 확인: 시나리오 `autosave` 에 "씬 편집 → restore → 미저장 확인 모달이 뜬다" 와 "새 씬 편집 → `autosave.now` → 사본 1".

#### E-2 History 패널의 되돌리기가 편집 잠금과 명령 큐를 우회한다
- 분류: 결함 · 확신: 확인
- 근거: `Source/Editor/Panels/Scene/Model/HistoryViewModel.cpp:43-69` — `undo` · `redo` · `jumpTo` · `clear` 가 `pStack->undo()` 를 그리기 도중 바로 부른다. `edit.undo` 명령은 `EditorCommandRegistry.cpp:155-159` 에서 `isGatedByEditLock`(플레이 · 베이크 중 꺼짐)에 걸리고 다음 프레임 시작에 도는데 패널 단추는 둘 다 건너뛴다. `CommandStack::undo`(`:287-296`)는 진행 중 상호작용도 보지 않아 기즈모 끌기 중 Ctrl+Z 가 돌면 redo 가 잘리고 되돌린 편집이 기록 없이 되살아난다.
- 상용 엔진: 언리얼 `UTransBuffer::CanUndo` 는 트랜잭션이 열려 있으면 거절하고 Undo History 창도 같은 `GEditor->UndoTransaction` 을 탄다.
- 제안: 되돌리기 입구를 하나로 둔다. History 는 `edit.undo` · `edit.redo` · `edit.jumpTo` 를 큐에 넣기만 한다. 스택에 "상호작용 구간" 카운터(문서 편집의 `DocumentEditSpan` 과 같은 구간)를 두어 열려 있으면 거절한다.
- 확인: 시나리오 `lightprobebake` 에 "베이크 중 `history.step.0` 클릭 → `Editor.UndoIndex` 그대로".

#### E-3 플레이 경계에서 미리보기 없는 문서 패널의 편집 기록이 사라진다 — 그 패널은 씬과 전역 스택 하나를 쓴다
- 분류: 결함(되돌리기 손실) · 확신: 확인
- 근거: `Source/Editor/Common/Workspace/Play/EditorPlaySession.cpp:573`(`parkHistory`) · `:564`(`clear`) · `:587`(`unparkHistory` 는 지금 내역을 버린다). 문서 기록이 갈 스택은 `EditorDocumentPanel::getDocumentCommandStack` 하나가 답하고, 미리보기 패널(머티리얼 · 머티리얼 그래프 · 프리팹 …)은 자기 창 스택이라 플레이 경계를 넘는다. 미리보기가 없는 문서 패널(시퀀서 · 타일맵 · 대화 · 애니메이션 그래프 · UI 미리보기 …)은 기본 답인 전역 스택이라, 플레이 중 고친 뒤 Stop 하면 문서는 dirty 인데 그 편집은 되돌릴 수 없다.
- 상용 엔진: 유니티는 플레이 중 에셋 편집과 그 Undo 가 플레이 종료 뒤에도 남는다. Godot 은 리소스마다 history 가 따로다.
- 제안: 문서 패널마다 스택을 두고(미리보기 패널의 창 스택을 기반으로 올린다) 포커스가 그 스택을 겨누게 한다 — `parkHistory` 는 씬 스택만 맡는다. 그때 `Editor.UndoCount` · History 패널이 보는 스택(전역 → 포커스)과 이를 보는 시나리오(`sequencercuts` · `gimmickgraph` · `uipreview*`)를 함께 옮긴다.
- 확인: 시나리오 `undosurvivesplay` 에 "플레이 중 시퀀서 편집 → Stop → 그 창에서 undo → 값 복귀".

#### E-6 형제 순서 한 줄을 옮기면 형제 전부가 스냅샷 · 기록 · 씬 파일 변경이 된다
- 분류: 성능 + 구조(팀 병합) · 확신: 확인
- 근거: `Source/Editor/Common/Commands/Scene/EditorSiblingOrder.cpp:40-52` — `renumber` 가 번호가 바뀐 형제마다 편집 전 전체 XML 스냅샷 → `setSiblingIndex` → `commitModify` 를 한다. 루트 8000 개 씬에서 맨 아래를 맨 위로 끌면 오브젝트 직렬화 16000 회가 되돌리기 한 단계에 들어가고, 저장한 씬 파일은 8000 오브젝트의 번호 줄이 모두 바뀐다(두 사람이 같은 씬을 고치면 충돌). 번호는 `uint16` 이다(`:22`).
- 상용 엔진: 유니티는 `m_RootOrder` 가 같은 병합 충돌을 일으켜 2022.2 에서 씬 파일의 `SceneRoots` 목록 하나로 옮겼다.
- 제안: 순서를 "부모가 든 자식 id 목록 한 칸"(루트는 씬 문서의 목록)으로 옮긴다. 그때까지는 분수 색인으로 한 오브젝트만 바꾼다.
- 확인: 시나리오 `hierarchyorder` 를 8000 루트로 돌려 `Editor.UndoCount` 1 과 그 프레임 시간, 저장 diff 줄 수.

#### E-7 인스펙터 편집 기록: 위젯을 누를 때마다 선택 전체를 직렬화하고, 편집 없이 풀리면 스냅샷이 남는다
- 분류: 성능 + 누수 · 확신: 확인
- 근거: `Source/Editor/Panels/Inspector/Model/InspectorEditRecorder.cpp:61-66` — `beginEdit` 이 선택한 오브젝트 전부를 `captureSnapshot`(오브젝트 전체 XML)한다(칸을 클릭만 해도). `InspectorPropertyUndo.cpp:23-27` — 지우는 길은 `IsItemDeactivatedAfterEdit` 뿐이라 클릭했다가 그냥 나오면 `_mapPending[widgetID]` 가 세션 끝까지 선택 전체의 XML 을 쥔다.
- 상용 엔진: 언리얼 `Modify()` 는 바뀌는 오브젝트만 이진으로, 유니티 `Undo.RecordObject` 는 프로퍼티 단위 diff 만 남긴다.
- 제안: 기록 단위를 "(오브젝트 id, 컴포넌트 안정 키, 프로퍼티 경로, 전 값, 후 값)" 으로 내린다. 그 전까지는 `IsItemDeactivated()` 에서 pending 을 지우고 스냅샷은 첫 실제 쓰기 직전에 지연해 뜬다.
- 확인: 단위 `InspectorEditRecorderTest.DeactivateWithoutEditDropsPending`. 8000 선택에서 칸 클릭 프레임.

#### E-8 에셋 목록 · 참조 색인이 규모에 비례해 다시 돈다
- 분류: 성능 · 확신: 구조 확인, 시간은 측정 필요
- 근거: `Source/Editor/Common/Workspace/HotReload/AssetReferenceTracker.cpp:55-63` — 바뀐 파일 중 하나라도 생기거나 사라졌으면 증분을 포기하고 전체 재스캔한다(만들기 · 지우기 · 이름 바꾸기 · 임포트 모두). `Source/Editor/Common/Asset/Reference/EditorReferenceIndex.cpp:297-314` — `refreshFile` 한 번이 참조 전체 `removeIf` + 전체 정렬을 UI 스레드에서 한다. 색인은 디스크에 남기지 않아 기동마다 전체 스캔이다. 문서 패널의 Open 단추(`EditorDocumentPanel.cpp:60`)와 인스펙터 에셋 선택 팝업(`PropertyGridModel.cpp:1130`)은 `EditorAssetIndex::collectEntries` 로 리소스 트리 전체를 UI 스레드에서 동기로 걷는다.
- 상용 엔진: 언리얼 AssetRegistry 는 디스크 캐시와 파일 감시 증분을 쓰고 피커 · 검색 · 참조 뷰어가 모두 레지스트리 질의다. 유니티 AssetDatabase(LMDB)는 영속 색인이다.
- 제안: `EditorReferenceIndex` 를 에셋 레지스트리 하나로 올린다 — 경로를 정수 id 로 intern 하고 참조자 · 대상 양방향 해시 표로 들며, 추가 · 삭제도 증분으로 하고 색인을 `Saved/Editor` 에 캐시한다. 피커 · Open · 검색은 이 레지스트리만 읽는다.
- 확인: 로그 `AssetReferenceTracker.ReferenceIndexReady` 의 ms 를 에셋 1 만 · 10 만 합성 트리에서. 단위 `EditorReferenceIndexTest.AddedFileRefreshesIncrementally`.

#### E-9 문서 편집 알림마다 문서 전체를 직렬화해 비교한다
- 분류: 성능 · 확신: 구조 확인, 측정 필요
- 근거: `Source/Editor/Common/GUI/Panel/EditorDocumentPanel.cpp:302-303` — `notifyDocumentEdited` 가 매번 `captureDocumentText()`(문서 전체 → 글)를 하고 기준 글과 통째로 비교한다. 끌기 중에는 프레임마다 돈다(상호작용 구간을 쥔 동안에도 알림마다 직렬화해 기준 글과 비교한다). 타일맵(512×512 칸)이나 긴 시퀀스는 MB 단위 직렬화가 프레임마다 든다. 같은 모양이 키트 에디터 확장에 있다(`Source/GameFramework/Plugin/Editor/MeshModifiers/MeshModifierSession.cpp:316-319` 의 `isStackUnsaved()` 가 호출마다 스택을 JSON 으로 만들어 비교).
- 상용 엔진: 언리얼은 바뀐 UObject 만 트랜잭션에 직렬화하고 dirty 는 깃발이다. Godot 은 프로퍼티 단위 do · undo 다.
- 제안: 구간(`DocumentEditSpan`)을 쥔 동안은 직렬화하지 않고 dirty 만 표시하면 직렬화는 구간 시작 · 끝 두 번이 된다. 큰 문서는 문서 모델이 "바뀐 범위" 를 내는 데이터 단계로 기록한다. 미저장 판정은 편집 개정 번호로 한다.
- 확인: `-gv_editorPanelTimes` 로 `tilemaptools` 를 256² · 512² 맵에서 칠하는 프레임.

#### E-10 뷰포트 피킹이 경계 구 레이 검사이고 전체 오브젝트를 훑는다
- 분류: 결함(정밀도) + 성능 · 확신: 확인
- 근거: `Source/Editor/Viewport/Model/EditorViewportRayPick.cpp:135-136` — 후보는 `getWorldBounds( center, radius )` 의 구 하나이고 가장 가까운 구 진입점이 이긴다. 길고 얇은 물체는 실루엣 밖을 눌러도 잡히고 큰 구가 안쪽의 작은 물체를 가린다. `:165` — `forEachGameObject` 로 전체를 훑는다(주석 "8000 개이면 Debug 약 20 ms"). 마키(`EditorViewportPicker.cpp:140-143`)는 피벗 한 점의 투영만 본다.
- 상용 엔진: 언리얼은 히트 프록시(ID 버퍼, 픽셀 정확), 유니티는 GPU 피킹 패스, Godot 4 는 AABB 뒤에 메시 삼각형 레이다.
- 제안: 렌더 쪽에 오브젝트 id 타깃을 두고 클릭 때 1 픽셀 읽기로 고른다. 그 전 단계로는 구 → OBB(`GameObject::getWorldBox`) → 메시 삼각형 순으로 좁힌다.
- 확인: 시나리오 `viewportpick` 에 "긴 기둥 옆 빈 곳 클릭 → 선택 없음" 과 "큰 바닥 위 작은 큐브 → 큐브".

#### E-11 읽기 전용 · 잠금 확인이 씬 저장에만 있다
- 분류: 구조 + 결함 · 확신: 호출 위치는 확인, 플랫폼별 결과는 추정
- 근거: `Source/Editor/Common/Commands/Scene/EditorSceneFileCommands.cpp:244-268` — `reportReadOnlyTarget` 은 씬 저장 두 곳에서만 부른다. 문서 패널의 `saveDocument` 들은 묻지 않고 `FileUtil::writeTextFile`(임시 파일 → 바꿔 끼우기)로 간다. POSIX 는 `std::rename`(`Source/Core/File/PlatformFileUtil.cpp:315`)이 읽기 전용 대상도 바꿔 끼워 잠금 규약(LFS lockable = 읽기 전용)이 조용히 뚫린다(추정). Windows 는 실패해 20 ms × 10 회를 UI 스레드에서 잔다.
- 상용 엔진: 언리얼은 저장 전 `PromptForCheckoutAndSave` 가 모든 패키지에 같은 길이다.
- 제안: "에디터가 콘텐츠 파일을 쓰는 입구" 하나(`EditorContentWrite::write( assetID, text )`)를 두어 읽기 전용 · 남의 잠금 · 쓰기 게이트를 거기서 본다.
- 확인: 단위(스크래치 파일을 읽기 전용으로) — 문서 저장이 false + 알림이고 파일 내용 · 속성이 그대로인지 Windows · Linux 둘 다.

## P2 — 개선과 작은 결함

한 줄에 근거 · 상용 엔진 · 제안 · 확인을 함께 적는다.

### Core

- **C-16 (보강) 원자적 쓰기가 fsync 를 하지 않는다** (결함 · 내구성 — 코드 확인) — `Source/Core/File/FileUtil.cpp:96-121` 은 `fflush` · `fclose` 뒤 바로 이름을 바꾼다. 세이브는 알려진 한도(`FileLocalSlotStorage.h:5`)지만 같은 길로 쓰는 장치 키 파일(`LocalDeviceKeyProvider.cpp:122-137`)에는 봉투가 없어 깨진 채 남으면 그 사용자는 지울 때까지 저장 · 읽기가 모두 IOError 다. 상용: 콘솔 세이브 API · SQLite 는 fsync 한다. 제안: `writeAtomically` 에 내구성 등급(`Durable` | `Fast`)을 필수 인자로 받고 세이브는 한 세대 전 파일을 남긴다. 확인: 0 바이트 · 쓰레기 키 파일을 두고 기동해 복구되는지.
- **C-19 IO · 로그의 실패 경로** (결함 · 작음 — 확인) — IOCP 포트 실패 뒤 IO 스레드가 끝나도 큐는 요청을 계속 받아 영영 끝나지 않는다(`Source/Core/File/Windows/WindowsAsyncFileIOBackend.cpp:113-117`). `flushForCrash` 가 막히는 장치 잠금을 잡는다(`Source/Core/Log/Sink/AsyncLogSink.cpp:262-265`). 큐가 가득 찬 줄은 큐에 든 줄보다 먼저 나간다(`:444-449`). 제안: 백엔드 실패를 큐 상태로 올려 새 요청을 즉시 실패시키고 크래시 flush 는 `try_lock` 으로 한다. 확인: 가짜 백엔드 단위 테스트.
- **C-20 구독의 약한 역참조** (결함 — 추정) — `Subscription::reset`(`Source/Core/Delegate/Subscription.h:208-223`)은 출처 포인터를 읽은 뒤 잠금을 잡는다. 다른 스레드가 그 사이 출처를 소멸시키면 해제 뒤 사용이다. `LockedMulticastDelegate` 머리말("약 2 초 뒤 경고하고 돌아온다")과 코드(끝까지 기다림, `:153-172`)도 어긋난다. 제안: 같은 스레드 규칙을 `ThreadOwners` 표지로 형식에 올린다. 확인: TSan 구성 단위 테스트.
- **C-21 다시 열어 확인하지 않은 후보** (미확인) — `SlotHandleTable` 세대 31 비트 + LIFO 재사용, `DynamicBitset` 범위 밖 쓰기 뒤 계속, Base64 `=` 개수 무제한, `MathUtil::wrapAngle` 이 무한대에서 끝나지 않음, `SpatialHashGrid2D` 셀 크기 조용한 대체, `BitReader::readVarUint` 넘친 비트 버림, `PositionWelder` NaN 캐스트, Windows `fromSearchPath` 가 작업 폴더를 PATH 보다 먼저 뒤짐, POSIX `Process` 소멸자의 좀비, Windows 크래시 필터의 로더 락 · 힙 사용. 고치기 전에 줄을 다시 읽는다.

### 네트워크

- **N-11 손실 · RTT 에 반응하는 송신률 조절과 재전송 백오프가 없다** (구조 — 확인) — `Source/Core/Network/Connection/NetEndpoint.cpp:534-544` 토큰 버킷은 설정값 고정, `NetConnection.cpp:376-379` 재전송 간격은 횟수와 무관하다. 상용: ENet(Godot)은 RTT 분산 기반 스로틀, 유니티 Transport 는 RTT 기반 재전송 + 창. 제안: 연결별 송신 상한을 손실률 · RTT 로 조절하고 재전송 간격은 SRTT + 4·RTTVAR. 확인: `NetEmulation` 손실 20 % 에서 `_resentMessageCount`.
- **N-13 RemoteCall 에 연결별 호출률 제한이 없고 인자가 글로 오간다** (구조 — 확인) — `Source/GameFramework/Kits/Feature/Network/NetClientServer/RemoteCall.cpp:281-343`. 상용: 언리얼 RPC DoS Detection + NetFieldExport 인덱스. 제안: 함수 메타데이터에 초당 상한(기본값은 Config), 접속 때 함수표 해시로 인덱스 합의. 확인: `TestRemoteCall` 에 상한 초과 사례.
- **N-14 주소 형식과 소켓이 IPv4 전용이다** (구조 — 확인) — `Source/Core/Network/NetTypes.h:158-161`, `Transport/PlatformSocketUtil.cpp:75,275`. 상용: 언리얼 `FInternetAddr` · 유니티 Transport · ENet 은 v4/v6 을 받는다. 제안: 요구 여부를 [PlatformTargets](PlatformTargets.md) 에 한 줄로 정하고 `NetAddress` 를 주소군 + 16 바이트로 넓힌다. 확인: `TestNetTransport` 에 `::1` 루프백.

### 온라인

- **O-13 OIDC 로그인의 nonce 를 클라이언트가 토큰과 함께 보낸다** (결함 · 인증 약화 — 확인) — `Source/GameFramework/Kits/Feature/Online/Account/Server/Platform/OidcLoginProvider.cpp:33-36`, `:142`. 토큰을 얻은 쪽이 nonce 를 그대로 채워 `exp` 까지 재사용한다. 상용: 서버가 nonce 를 발급 · 기억 · 1 회 소비. 제안: 로그인 전 챌린지 메서드 + 휘발 저장소 TTL. 확인: 같은 `토큰|nonce` 두 번 제출 → 두 번째 거절.
- **O-14 로그인 실패 횟수가 동시 시도에서 한 번만 세어진다** (결함 — 확인) — `LoginStoreLogic.cpp:1341-1344` `recordFailure` 가 Conflict 를 버리고 주소당 버킷은 프로세스 메모리다(`LoginService.h:226`). 상용: 계정 단위 카운터를 원자 증가로. 제안: 휘발 저장소 INCR + TTL. 확인: 워커 4 개 SQL 구성에서 틀린 비밀번호 20 건 동시.
- **O-15 FCM 404 한 번에 기기 등록을 지우고, 토큰이 계정 사이에서 유일하지 않다** (결함 — 코드 확인) — `Source/GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/FCM/FCMPushProvider.cpp:127`, `PushNotificationDispatcher.cpp:165-169`. 틀린 프로젝트 id 의 404 로 토큰이 전부 지워지고, 계정을 바꾼 기기에 앞 계정의 푸시가 뜬다. 상용: FCM v1 은 `errorCode == "UNREGISTERED"` 로 판정. 제안: 응답 JSON 의 오류 코드를 읽고 토큰 → 계정 역색인을 둔다. 확인: `TestPushProviders` 두 케이스.
- **O-16 GM: 기간 정지에 상한이 없고 대상 계정이 있는지 보지 않는다** (결함 — 확인) — `Source/GameFramework/Kits/Feature/Online/Admin/Server/AdminStoreLogic.cpp:306-314`. Operator 가 `INT64_MAX` 로 Suspend 하면 Super 전용 영구 정지와 같고, 오타 난 계정 id 에 재화가 Ok 로 들어간다. 제안: 등급별 최대 정지 기간(Config)과 계정 존재 확인. 확인: `TestAdminService` 두 케이스.
- **O-17 주소 단위 제한이 프록시 뒤 배치를 전제하지 않는다** (구조 — 추정) — `Source/GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.cpp:25`, `:540-545` 주소당 동시 연결 64. L4 프록시가 클라이언트 IP 를 보존하지 않으면 65 번째 연결부터 닫힌다. 상용: PROXY protocol v2. 제안: "신뢰 프록시" 설정일 때만 PROXY 머리를 읽는다. 확인: 한 주소에서 연결 100 개.
- **O-18 다시 읽지 않은 후보** (미확인) — 매칭 취소가 권한 서버 확인 없이 로컬에서 끝남(`MatchQueueService.cpp:669`), 서버 고르기의 좌석 예약이 프로세스 로컬(`ServerRegistryReader.cpp:127`), 공지 128 · 이벤트 256 뒤 조용한 잘림, 점검 목록의 fail-open(`ServerDirectoryService.cpp:204-205`), 차단한 상대의 글이 History 로 읽힘(`ChatService.cpp:580-581`), 길드 추방이 처리한 서버의 채팅 멤버십만 고침(`GuildService.cpp:935`), "모두 받기" 가 최근 50 통만(`MailboxStoreLogic.cpp:362`), APNs 403 재서명에 최소 간격 없음, 정책 값(도배 한도 · 거래 시한 · 매칭 유예)이 C++ 상수, `TokenBucketMap.h` 가 상한에서 표 전체를 훑음, `SQLMigrationRunner.cpp:282-289` 가 더 새 DB 면 기동 거절(순차 배포 막힘).

### RHI

- **R-7 DX12 가 아무도 읽지 않는 힙 CBV 를 프레임마다 다시 쓴다** (성능 — 확인, 측정 필요) — 상수 버퍼는 루트 CBV 인데(`D3D12RHIDevice.cpp:1964-2010`) 등록 때 힙 CBV 를 둘 만들고(`D3D12RHIResourceFactory.cpp:1310-1311`) `beginFrame` 마다 `refreshConstantBufferViews`(`D3D12RHIDevice.cpp:1889-1913`)가 SRV 표 전체를 훑는다. 상용: 언리얼 D3D12 는 힙에 CBV 를 두지 않는다. 제안: 힙 CBV 생성과 프레임 갱신을 지운다. 확인: 삭제 뒤 네 백엔드 골든 그대로.
- **R-8 Vulkan 스왑체인: 선호 형식이 없으면 실제와 다른 형식을 보고한다** (결함 — 확인, 드문 표면에서만) — `Source/Engine/Graphics/RHI/Vulkan/VulkanRHISwapChain.cpp:292-312` 가 `listFormat[0]` 을 쓰는데 `:445-448` 은 `_actualBackBufferFormat` 을 무조건 `R8G8B8A8_UNORM` 으로 적는다. 재생성 때 렌더 패스를 다시 만들지 않고(`VulkanRHIDevice.cpp:1742`) `oldSwapchain` 도 넘기지 않는다. 상용: 언리얼은 역매핑이 없으면 치명 오류. 제안: `VkFormat → RHIFormat` 역매핑 한 함수, 없으면 생성 실패(폴백 없음). 확인: 형식 고르기를 순수 함수로 빼 "선호 없음 → 실패" 단위 테스트.
- **R-9 텍스처 "셰이더 읽기" 전이가 픽셀 단계만 덮는다** (결함 — 잠재, 지금 호출처 없음) — DX12 `D3D12RHICommandContext.cpp:298` 이 `PIXEL_SHADER_RESOURCE` 만, Vulkan `VulkanRHIDevice.cpp:3662 · 3676 · 3684` 가 `FRAGMENT_SHADER_BIT` 만 건다. Vulkan 동기화 검증 · D3D12 GBV 스위치가 없어 검출 수단도 없다. 상용: 언리얼 `ERHIAccess::SRVGraphics/SRVCompute`. 제안: `ShaderResource` 를 모든 셰이더 단계 읽기로 정의하고 syncval · GBV 스위치를 `Config/Dev` 에 둔다. 확인: "RT 를 컴퓨트 SRV 로 읽기" GPU 테스트.
- **R-10 텍스처 상태와 첨부 뷰가 자원 단위다** (구조 — 확인) — `RHITextureTransition` 에 서브리소스가 없고 RTV · DSV 는 `MipSlice = 0` 고정(`D3D12RHIResourceFactory.cpp:1033 · 1041 · 1076`)이라 밉 사슬 다운샘플 · 큐브 한 면 갱신이 한 텍스처로 안 된다. 상용: 언리얼 `FRHITransitionInfo`(Mip · Slice), Godot RD 슬라이스 뷰. 제안: `RHITrackedState` 를 "전체 상태 + 필요할 때만 서브리소스 배열" 로 넓힌다. 확인: `RHITrackedStateTest` 서브리소스 사례.
- **R-11 DX12 오프스크린 RTV 128 · DSV 64 가 C++ 고정 상한이고 넘으면 뷰 없는 텍스처가 유효 핸들로 나간다** (구조 — 확인) — `D3D12RHIDevice.h:342-343`, `D3D12RHIResourceFactory.cpp:1014-1025`. 큐브 그림자 10 개 + CSM 이면 닿고 DX12 에서만 장면이 달라진다. 상용: 언리얼 오프라인 디스크립터 힙은 페이지로 늘어난다. 제안: 페이지 목록 + 뷰 실패 시 생성 실패. 확인: "큐브 RT 30 개 생성 → 모두 렌더 가능".
- **R-12 "상수 버퍼는 프레임에 한 번만 쓴다" 가 글 규칙이다** (구조 — 확인) — `IRHICommandList.h:158-165`. DX12 · Vulkan · GL 은 두 번 쓰면 앞 드로우까지 마지막 값으로 그려지고 막는 단언이 없다. 상용: 언리얼 `RHIUpdateUniformBuffer` 는 갱신마다 새 할당. 제안: R-4 의 구간 할당과 함께 쓰기가 구간을 돌려준다. 그 전에는 같은 프레임 두 번째 쓰기를 진단 오류로. 확인: 가짜 장치 단위 테스트.
- **R-13 Vulkan 파이프라인 캐시 파일이 장치 구분 없이 하나이고 종료 때만 저장한다** (성능 — 확인) — `VulkanRHIDevice.cpp:754-757`, `:1798-1831`. 하드웨어 ↔ lavapipe 를 번갈아 돌리면 서로 덮어쓰고 크래시한 실행은 캐시를 남기지 않는다. 상용: 언리얼은 헤더 검증 + 파일 이름에 장치 ID + 주기 저장. 제안: 파일 이름에 vendor · device · driverVersion. 확인: 하드웨어 → software → 하드웨어 실행의 컴파일 시간 합.
- **R-14 DX11 이 기능 수준 10_0 으로 내려가도 그대로 진행한다** (결함 — 확인) — `D3D11RHIDevice.cpp:512-516 · 550` 이 받은 `featureLevel` 을 읽지 않는다. Debug 는 SDK 레이어 유무를 보지 않고 `D3D11_CREATE_DEVICE_DEBUG` 를 붙인다. 상용: 언리얼은 최소 기능 수준을 어댑터 선택에서 거른다. 제안: 10_0 을 빼고 요구 판 표와 대조. 확인: WARP 초기화 로그.
- **R-15 인덱스 버퍼가 항상 32 비트이고 Packed 정점의 색이 float4 다** (성능 · 메모리 — 확인) — `Source/Engine/Graphics/Mesh/MeshRenderData.cpp:60-62`. CPU 사본(`RHIVertex` 60 B/정점)을 업로드 뒤 놓는 길도 없다. 상용: 언리얼 · 유니티는 16 비트 인덱스 · RGBA8 색 · CPU 사본 해제. 제안: 굽는 단계에서 인덱스 폭을 정해 `.mesh` 에 싣는다. 확인: `MeshIndicesTest` 폭 선택 사례.
- **R-16 GL 은 드로우마다 정점 속성을 전부 다시 지정한다** (성능 — 측정 필요) — `OpenGLRHICommandContext.cpp:672-732`. 상용: 언리얼 `FOpenGLContextState`, Godot GLES3 VAO 캐시. 제안: (PSO 정점 형식, 슬롯별 버퍼) 열쇠의 VAO 캐시. 3 순위 백엔드라 측정 뒤. 확인: Linux GL 체인의 apitrace 호출 수.
- **R-17 작은 동시성 틈 둘** (결함 — 추정) — WebGPU `_bDeviceLost` 가 비트필드인데 `AllowSpontaneous` 콜백이 쓴다(`WebGPURHIDevice.h:494-501`). `RHIRenderResource.cpp:57-75` 의 `broadcastInternal` 이 잠금을 놓은 뒤 `notify` 를 불러 그 사이 파괴된 리소스에 통보할 수 있다. 제안: R-2 의 분실 상태 통합, 통보 전에 에셋 로드를 비우는 순서를 단언으로. 확인: TSan 에서 "로드 진행 중 백엔드 교체".

### 렌더러

- **RN-10 갱신 주기를 낮춘 추가 뷰에서 물체의 지난 행렬과 뷰의 지난 뷰-투영이 다른 프레임이다** (결함 — 코드 확인, 증상 추정) — 움직임 기록은 GPUScene 에 하나이고 업로드마다 넘어가는데(`Source/Engine/Renderer/Scene/GPUScene.cpp:144`, `:347-351`) 쉬는 뷰의 `_prevViewProj` 는 그릴 때만 바뀐다. `FrameGraphExecutor.cpp:724` 의 `bViewCut` 은 죽은 조건이다. 상용: 언리얼 캡처 뷰는 기본으로 시간 누적을 끈다. 제안: 매 프레임 그리지 않는 뷰는 TAA · 모션 블러를 형식으로 끈다. 확인: 10 Hz CCTV 뷰 앞을 가로지르는 큐브 골든.
- **RN-11 SSAO 를 블룸 합성에서 최종 색 전체에 곱한다** (결함 · 화질 — 확인) — `Content/engine/shaders/post_bloom.hlsli:147-149`, `:157-159`. 직접광 · 발광 · 반투명 · 안개까지 뒤 면의 AO 로 어두워진다. 상용: 언리얼 · URP · Godot 는 AO 를 간접광에만 건다. 제안: AOColor 를 Shading 의 입력 계약에 넣어 환경광 · 프로브 항에만 곱한다. 확인: 유리판 + 발광 큐브 장면에서 AO 켬/끔 차이가 0 인지.
- **RN-12 트랜지언트 별칭 계획의 수명 합치기가 한 단계만 전파된다** (결함 — 잠재, 엔진 파이프라인에서는 발현 조건 없음) — `Source/Engine/Renderer/Frame/Execute/FramePassExecutor.cpp:787-820`. A→B→C 우회 사슬에서 A 의 수명이 늘지 않는다(게임이 만든 파이프라인 XML 은 걸릴 수 있다). 상용: 언리얼 RDG 는 패스 컬링 뒤의 실제 그래프에서 수명을 계산. 제안: union-find 로 합친다. 확인: `RenderGraphAliasPlanTest` 우회 패스 셋 사슬.
- **RN-13 추가 뷰 한도와 뷰당 고정 비용** (구조 · 성능 — 확인) — `Source/Engine/Renderer/Frame/View/RenderViewCollector.cpp:184-187` 이 카메라 등록 순서로 8 개에서 로그 없이 `break` 한다(보임 판정보다 앞). 상용: 언리얼 인스턴스 컬링은 여러 뷰를 한 패스에서. 제안: "보이는 것 먼저 → 늦은 정도" 로 고른 뒤 자르고 넘친 수를 감시 값으로. 확인: `RenderViewSchedulerTest` 카메라 12 대.
- **RN-14 글리프 아틀라스의 페이지 단위 비우기가 CJK 규모에서 흔들릴 수 있다** (구조 — 추정, 측정 필요) — `Source/Engine/Text/GlyphAtlas.h:58-59`(1024² × 8 장 상수), `GlyphCache.cpp:73-87`(가장 오래된 페이지 통째 비우기). 상용: Slate 폰트 캐시는 아틀라스를 늘린다. 제안: 페이지 수 · 크기를 Config 로, 비우기 전에 최근 글리프를 새 페이지로 옮긴다. 확인: 한글 3000 자 채팅 로그의 `Text.GlyphsRasterized`.
- **RN-15 입자 정렬이 방출기 단위다** (구조 — 주석에 적힌 한도) — `Source/Engine/Renderer/Scene/GPUSceneBuilder.cpp:1254`. 알파 블렌드 입자가 카메라가 돌면 앞뒤가 뒤바뀐다. 상용: 나이아가라 · VFX Graph 는 이미터 · 출력마다 정렬 모드. 제안: 방출기 정의에 정렬 모드, GPU 시뮬은 `instance_sort.hlsl` 재사용. 확인: 알파 연기 방출기 180° 골든.
- **RN-16 렌더 정책 값이 C++ · HLSL 상수에 흩어져 있다** (구조 — 확인) — 그림자 바이어스(`ShadowSetup.cpp:17-28`), 캐스케이드 람다(`DirectionalLightComponent.cpp:33-49`), TAA 기록 비율, 그리드 16x9x24, 광원 1024, 추가 뷰 8, PSO 예산 2000 µs(`FramePSOManager.cpp:1101`). `Config/Engine` 에 렌더 설정 파일이 없다. 상용: 언리얼 `r.*` + 스케일러빌리티, 유니티 SRP 에셋. 제안: `Config/Engine/RenderSettings.json` 한 벌(품질 단계별), 셰이더 값은 쿠킹 define. 확인: 린트 + 로드 검증 단위 테스트.

### 에셋 · 직렬화 · 오브젝트

- **A-19 비동기 로딩의 취소 · 우선순위가 요청자 단위가 아니고 프리로드가 같은 바이트를 두 번 읽는다** (구조 · 성능 — 확인) — `Source/Engine/Asset/AssetStreamingQueue.cpp:388-457` 의 `cancelRequest( 경로 )` 가 모든 대기자를 실패로 돌리고 합류 때 우선순위를 올리지 않는다. `PreloadSet.cpp:58-64` 는 읽고 버린 뒤 `pinAsset` 이 다시 읽는다. 상용: 언리얼 `FStreamableHandle`. 제안: 요청이 요청자 토큰을 돌려준다. 확인: `AssetStreamingQueueTest.CancelLeavesOtherRequesters`.
- **A-20 (보강) 공유 캐시 키가 요청 글 그대로라 같은 에셋이 두 벌 오를 수 있다** (구조 — 경로 확인) — `Source/Engine/Asset/Cache/SharedAssetTable.h:145`. 옛 id 와 새 id, 대소문자만 다른 글이 각각 칸을 만든다. 제안: GUID 2 단계에서 캐시 키를 "풀린 정체성" 으로. 확인: `SharedAssetCacheTest.RedirectedAndCanonicalIdsShareOneEntry`.
- **A-21 수명 불변식이 주석에만 있는 세 자리** (결함 — 잠재, 지금 트리거 없음) — `GameObjectStore::clear()` 중 해체 콜백의 생성(`GameObjectStore.cpp:524-571`), 삭제 대기 오브젝트의 즉시 `removeComponent` 뒤 틱 파도의 포인터(`GameObject.cpp:763-806`), `PrimitiveRegistry` 대기자 순회 중 제거(`PrimitiveRegistry.cpp:152-169`). 제안: 순회 중 구조 변경을 `StructuralChangeBuffer` 로만 받게 단언. 확인: ASan 단위 테스트 셋.
- **A-22 타입 등록의 조용한 덮기와 재잠금** (결함 — 잠재) — `registerTypeAlias` 가 살아 있는 타입 이름을 조용히 덮고(`Source/Engine/Reflection/TypeRegistry.cpp:688-705`), 타입 이름 32 비트 해시 표에 충돌 검사가 없고(`:188,203`, C-8), `getDerivedTypes` 가 공유 잠금을 쥔 채 같은 `shared_mutex` 를 다시 잡는다(`TypeRegistry.h:210-217` — 교착 발현은 추정). 제안: 충돌 · 덮기는 오류, 잠금 안에서는 포인터만 모은다. 확인: 단위 테스트 셋.
- **A-23 팩 · 쿠킹 설정의 빈 끝** (구조 — 확인) — DLC 소유권 검사는 쿠커가 `_dlcAppID` 를 적지 않고 검증자가 없으면 통과한다(`PackManager.cpp:395-403`). `Config/Engine/PackConfig.json` 이 게임 이름과 키트 카탈로그 칸을 엔진 설정에 든다. `ZstdDictionaryCompressor` 는 호출 0 건이고 기본 팩 코덱이 zlib 9 다. 호스트 설정에 플랫폼 덮어쓰기 층이 없다(`ConfigManager.cpp:73-78`). 제안: 게임별 쿠킹 규칙 층, 쓰지 않는 형식은 지운다. 확인: 쿠킹 단위 테스트.
- **A-24 그 밖의 작은 것** (확인 · 일부 추정) — `FloatCurve` 키 1 개 + NaN 시간에서 범위 밖 읽기(`Source/Engine/Curve/FloatCurve.cpp:30-42`), JSON 중복 키 · XML 중복 속성을 엄격 읽기가 잡지 않음, `JSONDocument` 깊이 한도 없음(추정), `TagIndex` 첫 질의의 전체 깊은 복사(`TagIndex.cpp:49-66`), `PrefabInstanceRegistry` 매 프레임 전체 훑기, `NetObjectTable` 씬 번호가 이름 해시 31 비트, 씬 비동기 로드가 다른 경로 요청에도 끝까지 짓고 버림(`SceneManager.cpp:249-263`), 리플렉션 묶음 TU 가 헤더 하나에 전체 재파싱(NEXT 5-4 보강).

### 물리 · 애니메이션 · 내비 · 오디오

- **S-9 경로 요청 핸들이 소유 타입이 아니고 프레임당 예산이 없다** (구조 — 확인) — `Source/Engine/Navigation/PathRequestQueue.h:94-131`. 요청자가 취소 없이 사라지면 `Ready` 칸이 씬을 내릴 때까지 남고, 한 프레임 수천 건이면 다음 프레임 머리에서 전부 기다린다(`SceneNavigation.cpp:692,726`). 상용: 유니티 `pathfindingIterationsPerFrame`, Detour `dtPathQueue`. 제안: 이동 전용 `PathRequest`(소멸자가 취소) + 프레임당 상한. 확인: `PathRequestQueueTest.DroppedTicketFreesSlot`.
- **S-10 보이스 풀이 차면 우선순위와 상관없이 새 소리를 버리고 그 경고를 오디오 스레드에서 찍는다** (결함 · 부하 때 — 확인) — `Source/Engine/Audio/AudioVoicePool.cpp:24-36`, `AudioEngine.cpp:712-717`. 가상 보이스(먼 루프)도 256 칸을 차지한다. 상용: 언리얼 · 유니티는 우선순위가 낮은 것을 뺏는다. 제안: 가상 보이스를 풀 밖 기록으로 빼고 고갈 때 (우선순위, 들림)이 낮은 칸을 한 블록 페이드로 뺏는다. 확인: `AudioVoiceTest.HighPriorityStealsWhenPoolIsFull`.
- **S-11 전역 잔해 한도를 "먼저 틱한 오브젝트" 가 치르고 한도는 콘솔 변수다** (구조 — 확인) — `Source/Engine/Destruction/Fracture/FractureComponentBase.cpp:1435-1479`. 상용: Chaos 는 조각별 제거 타이머 · 거리 기준. 제안: 잔해 바디를 `ScenePhysics` 풀 하나에 (나이, 부피, 거리) 키로 올린다. 확인: `DestructionStateTest.GlobalBudgetEvictsOldestAcrossObjects`.
- **S-12 대화 그래프 로드 검증이 링크 끝점 · 시작 노드를 보지 않는다** (구조 — 확인) — `Source/Engine/Dialogue/DialogueGraphAsset.cpp:102-155`. `findStartNode()`(`:185-195`)는 Start 가 없으면 첫 노드로 대신한다(폴백). 상용: Yarn Spinner 는 컴파일에서 끊긴 링크를 오류로. 제안: `validateFile` 이 끝점 · 핀 · Start 정확히 1 개 · 기다림 없는 순환을 검사. 확인: `DialogueGraphTest.RejectsDanglingLink`.
- **S-14 컴파일한 액션 · 조건식이 등록 엔트리를 원시 포인터로 든다** (구조 — 추정) — `Source/Engine/Behavior/ActionRegistry.cpp:181-189`. "핫 리로드 뒤에는 다시 컴파일" 이 주석 규칙뿐이다. 상용: 언리얼은 핫 리로드 때 `UFunction` 을 재바인딩. 제안: 등록표에 세대를 두고 `invoke` 가 대조. 확인: LiveReload 시나리오에서 리로드 뒤 대화 Action 노드(ASan).

### 플랫폼 · 입력 · 호스트

- **P-10 창이 배경일 때도 패드 입력이 게임에 들어간다** (구조 · 정책 누락 — 확인) — `Source/Engine/Input/InputManager.cpp:367-378`. XInput 은 전역이라 다른 게임을 패드로 하면 이 게임 캐릭터도 움직인다. 상용: SDL `JOYSTICK_ALLOW_BACKGROUND_EVENTS` 기본 0. 제안: `Config/Engine` 에 `backgroundGamepadInput`(필수 값). 확인: `InputManagerTest.BackgroundPadFollowsPolicy`.
- **P-11 접근성 단축키 끄기 API 는 호출자가 없고 구현은 사용자가 켠 고정 키까지 끈다** (결함 · 죽은 코드 — 확인) — `Source/Engine/Input/Windows/InputManagerWin32.cpp:558-583`. 상용: 언리얼은 기능이 꺼져 있을 때만 단축키를 끈다. 제안: 포커스 획득 · 유실에 묶고 `…ON` 비트는 건드리지 않거나 지운다. 확인: `InputAccessibilityTest.KeepsEnabledStickyKeys`.
- **P-13 스플래시 창이 상태를 바꿀 때만 메시지를 처리한다** (결함 · UX — 추정) — `Source/Engine/Window/Windows/Win32SplashWindow.cpp:157-183`. 긴 단계 사이 5 초를 넘으면 "응답 없음" 이다. 상용: 언리얼 `FWindowsSplash` 는 전용 스레드. 제안: `ScopedProgressReport` 간격 콜백이 스플래시를 펌프. 확인: `updateStatus` 사이 최대 간격 측정.

### GameFramework Runtime

- **G-8 어빌리티 · 상점 카탈로그가 "조용히 무효가 되는" 값을 받는다** (구조 — 확인) — 쿨다운 이펙트에 부여 태그가 없으면 쿨다운 없이 연타되고(`Source/GameFramework/Runtime/Gameplay/Ability/Ability.cpp:110-115`, `AbilityCatalog.cpp:451-452`), 주기 하한이 `Min = 0.0` 뿐이며, 상점은 사는 값이 파는 값보다 싸도 받는다(`Shop.cpp:392-422`). 상용: 언리얼 GameplayEffect `IsDataValid`. 제안: 카탈로그 읽기 끝의 교차 검증 하나를 쿠커도 돈다. 확인: `AbilityCatalogTest.CooldownEffectWithoutGrantedTagIsRejected`.
- **G-9 (보강) 틱 밖 프레임 경로의 타입 전수 걷기 — 린트가 보지 못하는 자리** (성능 · 구조 — 확인, 측정 필요) — `Source/GameFramework/Client/Marker/QuestObjectiveMarkerComponent.cpp:69`, `Runtime/Gameplay/Zone/ZoneTracker.cpp:251, 288-291`, `InteractionInterruptionSystem.cpp:59`. `tick-scene-walk` 린트는 `onTick` 에서 닿는 길만 따라간다. 제안: 린트 출발점을 프레임 시스템 · 위젯 갱신 훅으로 넓힌다. 확인: Tracy `ZoneSystem::runFramePass`.
- **G-10 관통 탄이 "방금 맞힌 하나" 만 기억한다** (결함 — 추정) — `Source/GameFramework/Runtime/Character/Combat/Projectile/ProjectileSimulation.cpp:117-139`. 겹친 대상 A · B 를 번갈아 맞을 수 있다. 상용: 언리얼은 맞힌 액터를 무시 목록에 쌓는다. 제안: `ProjectileState` 에 맞힌 대상 고정 배열. 확인: `ProjectileSimulationTest.PierceHitsOverlappingTargetsOnceEach`.
- **G-11 카메라 충돌 탐침의 레이어가 C++ 이름 둘에 박혀 있고 없으면 조용히 충돌이 꺼진다** (구조 — 확인) — `Source/GameFramework/Runtime/Character/Camera/CameraCollisionProbe.cpp:72-83`. 상용: SpringArm `ProbeChannel`. 제안: 카메라 프리셋에 레이어 칸, 비면 오류. 확인: `CameraCollisionProbeTest.MissingLayersIsAnError`.
- **G-12 키트 에디터 확장의 미저장 보호가 패널 UI 에만 있고, CharacterEditor 의 문서 dirty 는 소켓 · 포즈만 본다** (결함 — 확인) — `MeshModifierSession::setTarget`(`Source/GameFramework/Plugin/Editor/MeshModifiers/MeshModifierSession.cpp:287-289`)이 무조건 스택과 기록을 지운다. `CharacterEditorModel.h:118` 의 `isDirty()` 에 몸 데이터 · 외형이 없다. 상용: 언리얼은 편집하는 패키지 전부를 dirty 목록에 올린다. 제안: `setTarget` 이 미저장이면 `expected` 오류, 패널이 "이 문서가 여는 파일 목록" 하나로 dirty · 저장을 돈다. 확인: `MeshModifierSessionTest.SetTargetRefusesWhenUnsaved`.
- **G-13 ScreenTransitionManager: 전환 중에 새 전환을 걸면 앞 전환의 동작이 사라진다** (결함 — 잠재, 프로덕션 호출처 없음) — `Source/GameFramework/Runtime/Foundation/Framework/Flow/ScreenTransitionManager.cpp:91-98`. 제안: 진행 중이면 거절하거나 대기열 하나. 확인: `ScreenTransitionTest.SecondTransitionDuringFadeOutDoesNotDropFirstAction`.

### 키트 · 게임

- **K-5 누적 시각을 float32 로 든다** (결함 — 잠재, 산술 확인) — `Source/GameFramework/Kits/Genre/Strategy/CityBuilder/CitySimulation.cpp:510` 의 `_time += 0.25f` 는 2^22 초(NileCity 8 배속 146 시간)에서 멈춰 서비스 만료 판정이 영영 참이 된다. `RTSWorld.cpp:500` 은 2^20 초에서 멈춘다. `WorldClock` 은 정수 날 + 초라 문제없다. 제안: 키트 공용 `SimTick`(int64 걸음 수), `float32 _time` 멤버는 린트로 막는다. 확인: `CitySimulationTest.ServiceExpiresAfterLongRun`.
- **K-6 StarSkirmish 8 배속이 프레임률에 묶이고 AI 시계가 월드와 어긋난다** (결함 — 확인) — `Source/Core/Time/FixedStepTimer.h:48` 의 `_maxFrameTime`(RTS 0.25 초)이 `deltaTime * _timeScale` 을 자른다(30 fps 에서 잘린다). AI · 타이머는 자르지 않은 델타를 받는다(`SkirmishMatch.cpp:237-241`). 제안: 배속을 걸음 수로 넘긴다(`update( dt, speed )`). 확인: `RTSWorldTest.SpeedScaleKeepsStepRatio`.
- **K-7 맵 배치 · 뷰 표가 C++ 에 있고 모델 경로를 문자열로 조립한다** (구조 — 확인, NEXT 7 절 "경로를 조립해 읽는 에셋" 의 실제 자리) — `Source/Games/NileCity/NileBuildingComponent.cpp:25-27, 82-116, 131-134`, `Source/Games/StarSkirmish/SkirmishMatch.cpp:154-175`. 쿠킹 도달 계산이 참조로만 담아 조립 경로는 빠질 수 있다. 제안: 키트에 "규칙 id → 뷰" 카탈로그(`AssetRef<Mesh>`). 확인: `-cooked-content` 로 두 게임 `slice`.
- **K-8 VoxelCraft 광석 배치가 지형 씨앗을 쓰지 않는다** (결함 — 확인) — `Source/Games/VoxelCraft/VoxelGameModeComponent.cpp:33-40, 236` 의 `hashCoord( x, y, z )` 에 씨앗 인자가 없다. 제안: 꾸밈을 `VoxelTerrainSettings` 의 데이터 줄로 올린다. 확인: `VoxelTerrainTest.DecorationsFollowSeed`.
- **K-9 Velmora 확장 팩이 정해진 훅 밖에서 올라간다** (구조 — 확인) — `Source/Games/Velmora/VelmoraGame.cpp:67-71` 이 `ModSession::applyForGame` 을 `onPrepareContent` 가 아니라 `onInitialize` 에서 부른다. 넷 와이어 해시가 0 이고 세이브 머리에 구성이 실리지 않는다. 제안: `applyForGame` 을 준비 단계 토큰으로만 부를 수 있게 한다. 확인: `GameInstanceTest.ContentIsPreparedBeforeDeclaredData`.
- **K-10 배속 · 일시정지와 "규칙 → 뷰" 동기화가 게임마다 따로 있다** (구조 · 중복 — 확인) — 배속 코드가 `NileGameModeComponent.cpp:720-730` 과 `SkirmishGameModeComponent.cpp:602-615` 에 같고 FrontierTown 은 셋째 방식이다(K-2 의 원인). 뷰 동기화는 다섯 벌이다. 상용: Mass Representation 프로세서 하나. 제안: Runtime 에 `GameSpeedControl` 과 `SimulationViewSync<TID>`. 확인: 기존 `control` · `pause` · `viewsync` 시나리오.
- **K-11 오브젝트 풀이 한 곳에서만 쓰인다** (성능 — 측정 필요) — `PrefabPool` 사용처는 슈터 탄착뿐이고 아레나 탄(`ArenaGameModeComponent.cpp:619-624`) · 슈터 적은 매번 스폰한다(A-15 의 XML 파싱 비용과 겹친다). 상용: Lyra · Boss Room 은 풀. 제안: `spawnPrefab` 이 풀 정책(데이터)을 받는다. 확인: Tracy `GameModeFlush`.
- **K-12 복셀 월드가 고정 크기 전량 상주이고 메셔가 면마다 정점 6 개다** (성능 · 구조 — 측정 필요) — `VoxelGameModeComponent.h:51-52`(8 × 8 청크 상수), `View/VoxelMesher.cpp:119-123, 160-188`. 상용: 그리디 메싱 + 인덱스 + 거리 기반 청크 로드. 제안: "청크 좌표 → 청크" 사전 + 생성/저장 훅(K-3 과 함께). 확인: Tracy `VoxelMesher::fillChunkMesh`.
- **K-13 LanternVale 서버가 변경과 무관하게 전원을 저장하고 매 틱 전 엔티티를 다시 싣는다** (성능 — 측정 필요) — `Source/Games/LanternVale/ValeNetLink.cpp:343-349`, `:351-389`. 상용: 더티 프로퍼티만(Push Model · Iris). 제안: 프로필 저장 세대, 엔티티는 바뀔 때만 `setEntity`. 확인: `net-pair mmo` 부하의 자동 저장 쓰기 수.
- **K-14 (보강) 장르 키트 17 개에 쓰는 게임 · 시연이 없다** (구조 · 테스트 — 확인) — BattleRoyale, Fighting, MechArena, Metroidvania, CardGame, KartRacing, PartyArena, Rhythm, AsymmetricHorror, CoopScavenger, GhostHunt, SurvivalHorror, ClassicJRPG, MonsterCollector, MonsterHuntRPG, RestaurantSim, SideScrollConquest 가 `Source/Games` 에서 include 되지 않는다. 단위 테스트는 있지만 게임 모드 · 세이브 봉투 · 뷰 스폰 조립은 한 번도 돌지 않았다. 반대로 게임 폴더의 순수 규칙 클래스(`SkirmishMatch` · `NileCityPlanner`)에는 단위 테스트가 없다. 제안: Empty `KitDemo` 를 키트마다 한 씬 + `slice` 시나리오로 채우는 것을 키트 완료 조건으로. 확인: `game.empty.kit.<이름>` 시나리오.

### 에디터

- **E-13 되돌리기 한도가 개수 상수이고 일부 문서 패널은 되돌리기가 없다** (구조 — 확인, NEXT "표 페이지 Ctrl+Z" 보강) — `CommandStack.cpp:182` 의 128 과 `EditSnapshotHistory.h` 의 64 가 C++ 상수이고 바이트 한도가 없다(8000 개 다중 편집 128 번이면 GB 단위). AudioEvents · InputMap · DataTable 패널은 undo 가 0 건이다. 상용: 언리얼 Undo Buffer Size(MB). 제안: 한도를 `Config/Editor`(개수 + 바이트)로, 세 패널은 P0-1 · P0-2 의 문서 스택을 받는다. 확인: `CommandStackTest.EvictsByByteBudget`.
- **E-15 외부 프로세스: 패키징 출력은 끝나야 보이고 넷플레이 자식은 에디터가 죽으면 고아가 된다** (구조 — 확인) — `Source/Editor/Panels/Packaging/Model/PackagingModel.cpp:150-156`(줄이 종료 때 한 번에 온다, 오류 판정이 `"error"` 부분 문자열), `Source/Core/Process/Windows/WindowsProcess.cpp:251`(잡 오브젝트에 `KILL_ON_JOB_CLOSE` 없음). 상용: 언리얼 UAT 는 줄 단위로 흘린다. 제안: `takeLines` + `ProcessOptions` 의 "부모와 함께 종료" 칸(Windows 잡, Linux `PR_SET_PDEATHSIG`). 확인: 시나리오 `packaging` 에 "Running 동안 줄 수 > 0".
- **E-16 (보강) MVVM 강제 범위와 SelfTest 가 덮지 않는 길** (구조 — 확인, DesignBacklog "에디터 UI 스레드 분리" 1 번 보강) — `Config/Rules/Layers/EditorLayers.toml:55-63` 의 `[panelViewBan]` 이 문서 패널의 View 를 막지 않아 `MaterialPanel.cpp:362-386` · `SequencerPanel.cpp` · `TileMapPanel.cpp:352` 가 문서 모델을 직접 고친다. `EditorUndoCoverage.cpp` 는 레지스트리의 거래 명령만 돌아 이 문서의 에디터 P0 넷이 모두 빈칸에 있었다. 제안: `notifyDocumentEdited` 를 Model 창구로 내리고 `editor/undocoverage` 에 "문서 패널 전수"(열기 → 편집 → undo → 글 해시 비교)를 더한다. 확인: View 파일의 `notifyDocumentEdited` 호출 수 0 린트.

## 다음 할 일

한 줄이 한 묶음(커밋 하나에서 서넛)이다. 각 묶음은 "확인" 에 적은 단위 테스트를 먼저 써서 지금 지는 것을 본 뒤 고친다. 성능 묶음은 측정이 먼저다. 온라인 · 서버 · 보안 묶음은 `security-and-hardening`, 외부 API(Win32 · Vulkan · D3D · NDK · Emscripten)를 고치는 묶음은 context7 로 그 판의 문서를 먼저 대조한다.

### P0 (6 묶음)

1. **에디터 되돌리기 구조** — 문서마다 스택(미리보기 없는 문서 패널): E-3, 함께 닫히는 E-2 · E-9 · E-13. 상호작용 구간(`DocumentEditSpan`)과 문서 스택의 답(`getDocumentCommandStack`)은 들어갔다. `editor/undocoverage` 에 문서 패널 전수 단계(E-16)를 먼저 더해 지는 것을 본다.
2. **복구 입구** — 복구 입구 통일과 새 씬 · 문서 패널의 자동 저장: E-1(세션 폴더 · pid 잠금 · 리로드 이어 쓰기 P0-3 은 들어갔다).
3. **깨끗한 참조 문서 다시 읽기** — 옮김 방송에 참조자 목록을 실어 열린 깨끗한 문서를 다시 읽고, 미리보기 선택의 에셋 경로도 따라가게. 방송 · 미저장 참조자 거절 · 연 문서 삭제 거절은 들어갔다(P0-4). 참조 쪽 근본은 integ2 의 GUID 2 · 3 단계.
4. **자동 저장 되돌리기의 씬 이동** — `restoreLatest` · `restoreCheckpoint` 도 찍은 씬으로 옮긴 뒤 적용(불러오기 델리게이트 비동기화). 슬롯 불러오기는 들어갔다(G-7 · P0-6).
5. **읽은 것을 커밋 조건으로** — 일 안에서 읽은 것을 커밋 조건으로 자동으로 붙이는 연결 포장과 Admin · Mailbox · Guild 저장 로직 점검(경제 키트의 지급 · 환불 · 철회는 영수증 레코드 하나로 묶였다 — O-1).
6. **웹 오디오 백엔드** — AudioWorklet + 링 버퍼: P0-11.

### P1 (29 묶음)

13. **새니타이저 프리셋** — `Linux-Debug-ASan` · `TSan` 을 linux-verify 에: P-8. 뒤 묶음들의 확인 수단이라 먼저 한다.
14. **웹 · Android 개발 프리셋과 가짜 창 테스트** — P-9, 그 위에서 포커스 · 글 입력 · 페이싱 · 수명 구독 P-3 · P-4 · P-5 · P-6.
15. **세이브 판 올림** — `StateIO::versioned` + 구간별 승계 표 + id 로 짝짓는 규칙 통일: A-6, 사용자 설정 주인별 판 A-5, 어빌리티 저장 G-1, 복셀 팔레트 K-3.
16. **영속 정체성** — 컴포넌트 · 중첩 노드 고정 지역 id: A-2 · A-1(에셋 GUID 2 단계와 같은 줄기, A-20).
17. **팩 한도 · 마운트 실패** — 쓰기 · 읽기 한도 상수 하나, 쿠킹 뒤 되읽기, Cooked 마운트 실패는 기동 실패: A-3 · A-4.
18. **팩 규모** — 스트림 쓰기 · 분할 · 정렬 배열 색인 · 블록 읽기 · 색인 서명: A-9 · A-10 · A-12, 증분 쿠킹 해시 캐시 A-11. 30 만 항목 합성 팩으로 먼저 잰다.
19. **태스크 대기 모델** — 합류 대기 워커 깨우기, 큐 넘침 처리: C-3 · C-4. 단위 테스트로 교착을 먼저 재현한다.
20. **잠금 · 할당 측정 묶음** — 적응형 `SpinLock`, 출하 할당기, 컨테이너 범위 삽입 · 링 버퍼 `deque`, 파일 읽기 큐: C-5 · C-6 · C-9 · C-15. 벤치 숫자가 나온 것만 고친다.
21. **Core 작은 결함** — 오일러 특이점, 삼각 분할 실패 계약, `appendFormat` 잘림, 백오프 지터, `getTotalTime` float64, POSIX 크래시 처리기: C-1 · C-2 · C-10 · C-11 · C-12 · C-13.
22. **전역 변수와 이름 해시** — 문자열 `gv_` 접근자, 렌더 스레드 스냅샷, 64 비트 현지화 키 + 충돌 오류: C-7 · C-8. 결정성 TU 목록 C-14.
23. **네트워크 보안** — 접속 증명 PRF, 핸드셰이크 시한, 파괴 스냅숏 요청 집합: N-1 · N-10 · N-5. TLS 서버 이름 O-2.
24. **네트워크 정확성** — 종류별 순서 번호, 틱 시각 값 형식: N-2 · N-4.
25. **서버 관측 · 텔레메트리** — UDP 지표 등록, 업로드를 전용 워커로: N-7 · N-6.
26. **네트워크 규모(측정 뒤)** — 기한 큐 · 받기 분리, always relevant 목록: N-8 · N-9. 부하 봇 250 · 500 · 1000 연결.
27. **온라인 정확성** — 거래 스냅숏 노출, 리더보드 멱등, 길드 창고 이전 정책(약관 확인은 사용자에게): O-4 · O-5 · O-10.
28. **매칭 영속 기록** — 명단 캐시 기록, id 발급, 죽은 서버의 티켓 풀기: O-6 · O-7.
29. **보존 규칙** — 키트가 규칙을 내놓는 계약, 만료 색인 공용 형식, 받은 우편 정리: O-9 · O-8.
30. **온라인 부하** — 해시 연산 풀 · 저장소 대기 상한, presence 역색인, 부하 봇 원장 동작 · 열린 루프: O-3 · O-11 · O-12.
31. **RHI 자원 모델** — 정적 버퍼 장치 로컬, 버퍼 구간 할당, bindless 번호 목록, 장치 분실 통보: R-3 · R-4 · R-5 · R-2.
32. **텍스처 스트리밍** — bindless 번호 고정, 밉 범위 대기 줄, 예산 · 내리기: R-6(블록 읽기 A-12 위).
33. **렌더러 결함** — Preserve 배치 가시 칸, TAA 지터, GPU 방출기 재개, 자동 배율 입력: RN-1 · RN-2 · RN-9 · RN-8.
34. **렌더러 규모** — 광원 뷰 컬링 · 예산, 그림자 아틀라스 할당기와 타일별 컬링, GPUScene 오래 가는 칸, 지형 공유 인덱스: RN-3 · RN-4 · RN-5 · RN-6 · RN-7. `load/*` 에 광원 4000 · 오브젝트 20 만 장면을 먼저 더한다.
35. **에셋 런타임** — 공유 에셋 다시 읽기 Detach, WeakInternTable, 프리팹 이진 상태, 이진 번역 표, JSON dump: A-7 · A-14 · A-15 · A-16 · A-17. ReflectionParser 스탬프 A-8.
36. **씬 스트리밍** — 셀별 구간 · 워커 인스턴스화 · 내릴 때 상태 보관: A-13.
37. **물리** — 접촉 이벤트 opt-in, 임시 할당자 설정 · 폴백, 배율 변경 재생성, 캐릭터 속 바디 확인, 캐릭터 일괄 이동: S-1 · S-2 · S-4 · S-7 · S-8.
38. **애니 · 내비 · 오디오** — 블렌드 스페이스 삼각분할, 64 비트 폴리 참조, 클립 캐시 퇴출: S-5 · S-6 · S-3. 리사이즈 미루기 · 모달 루프 틱 · UI 지연 파괴 P-2 · P-7.
39. **게임플레이 규칙** — WorldClock 정수 시각, 인벤토리 거래 형식, 퀘스트 받은 단계, 사망 전이 한 곳, 상호작용 점유: G-2 · G-3 · G-4 · G-5 · G-6.
40. **키트 · 게임** — LanternVale 이동 예산, 시뮬레이션 시간 한 길(FrontierTown), 규칙 자산: K-1 · K-2 · K-4.
41. **에디터 규모** — 최상위 선택 조회, 선택 해시 집합, 형제 순서 목록, 인스펙터 프로퍼티 단위 기록, 에셋 레지스트리, id 버퍼 피킹, 콘텐츠 쓰기 입구: E-4 · E-5 · E-6 · E-7 · E-8 · E-10 · E-11. 8000 큐브 측정(NEXT 3 절)과 같이.

### P2 (10 묶음)

42. **Core 내구성 · 한도** — C-16 ~ C-21.
43. **네트워크** — 혼잡 제어, 멱등 바이트 예산, RPC 호출률, IPv6 판단, 롤백 정수: N-11 ~ N-15.
44. **온라인** — OIDC 챌린지, 실패 카운터, FCM 판정, GM 한도, PROXY 머리, 후보 재확인: O-13 ~ O-18.
45. **RHI** — 힙 CBV 삭제, 스왑체인 형식 역매핑, 셰이더 읽기 전이 + syncval · GBV 스위치, 서브리소스 상태, RTV · DSV 페이지, 상수 버퍼 진단, 파이프라인 캐시, DX11 기능 수준, 인덱스 폭, GL VAO 캐시: R-7 ~ R-17.
46. **렌더러** — 쉬는 뷰의 시간 누적 끄기, AO 를 간접광에만, 별칭 계획 union-find, 추가 뷰 고르기, 글리프 아틀라스, 입자 정렬, `RenderSettings.json`: RN-10 ~ RN-16.
47. **에셋 · 오브젝트** — A-18 ~ A-24.
48. **시뮬레이션** — 경로 요청 소유 핸들, 보이스 뺏기, 잔해 풀, 대화 검증, 접촉 추적기 상태, 액션 등록 세대: S-9 ~ S-14.
49. **플랫폼 · 호스트** — 배경 패드 정책, 접근성 단축키, DPI 틀, 스플래시, 서비스 RUNNING 시점: P-10 ~ P-14.
50. **GameFramework · 키트** — 카탈로그 교차 검증, 프레임 경로 린트, 관통 탄, 카메라 레이어, 확장 dirty, 전환 대기열, `SimTick`, 배속 걸음 수, 뷰 카탈로그, 광석 씨앗, Velmora 준비 훅, `GameSpeedControl` · `SimulationViewSync`, 풀, 복셀 메셔, 프로필 저장 세대, 키트 시연 씬: G-8 ~ G-13, K-5 ~ K-14.
51. **에디터** — 이름 폴백 삭제, 되돌리기 바이트 한도, 미리보기 수명, 외부 프로세스, MVVM 범위: E-12 ~ E-16.

## 폴더 체크리스트

`Source/` 2단계 폴더 122 개. "읽음" 은 대표 파일을 열어 호출 경로를 따라간 것, "훑음" 은 파일 목록 · 검색 · 호출부만 본 것이다.

### Core (32)

- Algorithm — 읽음 — TArray RemoveAtSwap · FindByKey. 이상 없음.
- CommandLine — 읽음 — FCommandLine · FParse. 이상 없음(모르는 인자 · 변환 실패는 기동 정지).
- Compression — 읽음 — FCompression 크기 상한. 이상 없음(뺄셈 비교, 팽창 상한).
- Concurrency — 읽음 — Chase-Lev · Vyukov MPMC · `UE::FMutex` · 주소 대기. C-5. `DataRaceDetector` · `ShardedCounter` 는 안 읽음.
- Console — 빈 폴더(파일 0 개).
- Container — 읽음 — TArray · TMap · TSortedMap · TDeque · 세대 핸들. C-9, C-21.
- Delegate — 읽음 — TMulticastDelegate. C-20. 방송 중 해제는 이상 없음.
- Diagnostics — 읽음 — Breakpad · UE LLM. C-13. `MemoryProfiler.cpp` 뒷부분 · `Web/` 은 안 읽음.
- Encoding — 읽음 — RFC 4648. Hex 는 이상 없음, Base64 는 C-21.
- Event — 읽음 — 이중 버퍼 큐. C-5. 아레나 교체는 이상 없음.
- File — 읽음 — FAsyncFileHandle · IOCP · io_uring · 원자적 교체. C-15 · C-16 · C-18 · C-19. io_uring 백엔드 · Android · Web 은 훑음.
- Foundation — 읽음 — UE check · TValueOrError. 이상 없음.
- GlobalVariable — 읽음 — TAutoConsoleVariable. C-7.
- Hash — 읽음 — SipHash · FNV. C-8. 상수 시간 비교는 이상 없음.
- Log — 읽음 — 비동기 로그 스레드 · backlog. C-19. 종료 시 유실 없음.
- Math — 읽음 — FQuat · DirectXMath. C-1 · C-14.
- Memory — 읽음 — FMalloc · Binned · LLM 태그. C-6.
- Module — 읽음 — FModuleManager. 이상 없음(절대 경로 로드).
- Name — 읽음 — FName. C-8 · C-17. 샤드 잠금은 이상 없음.
- Network — 읽음(하위 Connection · Message · Replication · Security · Transport) — NetDriver · ENet · netcode.io. P0-8, N-1 ~ N-5 · N-8 · N-10 ~ N-12 · N-14. `NetPrioritizer` · `NetInterestGrid` 본문과 Tests 는 훑음.
- Path — 읽음 — FPaths. C-18.
- Predefined — 훑음 — 줄 순서 = 열거값 `static_assert`. 이상 없음.
- Process — 읽음 — FPlatformProcess · FMonitoredProcess. P0-16 · P0-17, C-21.
- Registry — 읽음 — compiled-in 등록. 이상 없음(상수 초기화 사슬).
- Serialization — 읽음 — FBitReader · varint. C-21 의 `readVarUint`.
- Spatial — 읽음 — b2DynamicTree · earcut. C-2. BVH 는 이상 없음.
- String — 읽음 — TStringBuilder · UTF-8 디코더. C-10.
- Subsystem — 읽음 — FSubsystemCollection. 이상 없음(위상 정렬, 역순 종료).
- Task — 읽음 — UE Tasks · JobSystem. C-3 · C-4 · C-17.
- Testing — 읽음 — GoogleTest · UE Automation. 이상 없음.
- Time — 읽음 — FPlatformTime · 고정 스텝. C-11 · C-12.
- UUID — 읽음 — FGuid. 이상 없음(OS 암호학적 난수).

### Engine (41)

- Animation — 읽음 — AnimGraph · Blend Space · 노티파이 · URO. S-5. 상태 기계 전이 · `Retarget/` · `Facial/` 은 안 읽음.
- Asset — 읽음 — StreamableManager · DDC · Addressables. A-4 · A-7 · A-10 · A-11 · A-14 · A-19 · A-20.
- Audio — 읽음 — AudioMixer · 보이스 가상화 · 장치 유실. P0-11, S-3 · S-10. `DSP/` · `Graph/` 는 안 읽음.
- Automation — 읽음 — Functional Test · Gauntlet. 이상 없음(시한 대소 `static_assert`).
- Behavior — 읽음 — 조건 · 액션 컴파일. S-14.
- Character — 훑음 — CharacterMovement. S-7 · S-8(컨트롤러는 Physics 쪽). `Fit/` · `PairedAnimation/` 은 안 읽음.
- Compression — 읽음 — Oodle 문맥 재사용. A-23. 손상 입력 견고성은 이상 없음.
- Config — 읽음 — FConfigCacheIni 계층. A-23. 필수 값 오류는 이상 없음.
- Console — 읽음 — 개발 명령 등록. 이상 없음.
- Content — 읽음 — pak · IoStore 색인 · 서명 · 패치. A-3 · A-9 · A-10 · A-12 · A-23.
- CoreServices — 읽음 — 서브시스템 접착. 이상 없음.
- Curve — 읽음 — FRichCurve. A-24.
- Destruction — 읽음 — Chaos 잔해 수명. S-11. `FractureGraph` 는 안 읽음.
- Dialogue — 읽음 — 그래프 유효성. S-12.
- Environment — 읽음(Terrain · Foliage · Water) — 랜드스케이프 LOD. RN-7 · RN-6. Sky · Fog · Surface 는 안 읽음.
- Foundation — 읽음 — 서브시스템 등록 순서. 이상 없음.
- Graphics — 읽음(RHI 다섯 백엔드 · Material · Shader · Mesh · Texture) — D3D12RHI · VulkanRHI · RenderingDevice. R-1 ~ R-17. 백엔드별 PSO 생성 · 전송 작업, Material 패킹, Debug 도구는 훑음.
- Input — 읽음 — Enhanced Input · Raw Input. P0-15, P-1 · P-10 · P-11. `InputMap` 평가(82 KB)는 훑음.
- Localization — 읽음 — ICU · LocRes. A-16 · A-18, C-8.
- Module — 읽음 — 모듈 타입 등록 · 내림 순서. 이상 없음.
- Navigation — 읽음 — Recast/Detour · 비동기 경로 큐. S-6 · S-9. `NavTileRebaker` 본문은 안 읽음.
- Net — 읽음 — 게임 넷 세션 · HTTP 계약. N-1 · N-6 · N-7.
- Object — 읽음 — FUObjectArray · FTickFunction · 프리팹 변형. A-1 · A-2 · A-15 · A-21 · A-24. `SceneComponent.cpp` 전체는 안 읽음.
- Observability — 읽음 — 지표 레지스트리 · 디버그 채널. N-7. 잠금 없는 카운터는 이상 없음.
- Physics — 읽음 — Chaos · PhysX · Box2D v3. S-1 · S-2 · S-4 · S-13. `Cloth/` · 소프트 바디 · 차량은 안 읽음.
- Presentation — 훑음 — 드라이버 · 싱크 인터페이스와 값 형식뿐이라 결함 축에 해당하는 구현이 없다.
- Profiling — 읽음 — Tracy 백엔드의 Shipping 제외. 이상 없음.
- Reflection — 읽음 — UObject 등록 · 재인스턴싱 · ClassDB. A-22.
- Renderer — 읽음(Frame · Graph · Lighting · Scene · VFX · Pipeline) — RDG · 인스턴스 컬링 · VSM · Niagara. RN-1 ~ RN-16. Cook · Extension · Canvas 일부는 훑음.
- Scene — 읽음 — 레벨 스트리밍 · World Partition. A-13 · A-24.
- Sequencer — 읽음 — Sequencer 평가 범위 · Timeline Signal. P0-12.
- Serialization — 읽음 — FArchive · 커스텀 버전. A-6 · A-17 · A-24. `SerializerUtil.cpp` · `XMLSerializer.cpp` 는 안 읽음.
- Server — 읽음 — 운영 HTTP 끝점 · 관리 채널. N-7. 토큰 · 상수 시간 비교는 이상 없음.
- Telemetry — 읽음 — 분석 공급자 배치 플러시. N-6. 동의 · 상한은 이상 없음.
- Text — 읽음 — Slate 폰트 캐시. RN-14. TextLayout · Bidi · HarfBuzz 는 안 읽음.
- TileMap — 읽음 — TilemapCollider2D · TileMapLayer. 청크 충돌 · 오너 추종은 고침(P0-14 지움).
- UI — 읽음 — Slate 무효화 · 위젯 경로 · ListView. P-7. 레이아웃 · 페인트 캐시 · 가상 목록은 이상 없음. Accessibility · Binding 은 훑음.
- UserSettings — 읽음 — GameUserSettings. A-5.
- VFX — 읽음 — Niagara · VFX Graph. RN-9. CPU 시뮬레이션 결정성은 이상 없음.
- Visual — 읽음 — 텍스처 스트리머 · 방향광. R-6 · RN-4. MeshLODSystem · Decal 은 안 읽음.
- Window — 읽음(Windows · Linux X11 · Android · Web) — FWindowsApplication · GLFW · SDL. P0-9 · P0-10 · P0-15, P-2 ~ P-6 · P-12 · P-13. `WaylandWindow.cpp`(84 KB)는 훑음.

### DevTools (13)

- AssetReload — 읽음 — DirectoryWatcher 넘침 리스캔. 이상 없음.
- Client — 읽음(Automation Runner 의 행 감시 · 골든 판정) — Gauntlet. 이상 없음. Runner 본체(67 KB) · DebugHUD · BugReport 는 훑음.
- Console — 훑음 — Host 의 디버그 제어 경유로만.
- Diagnostics — 훑음 — 파일 목록(`CrashInjection` · `StateHashRecorder` · 스위치들).
- File — 읽음 — 읽기 관찰자 · 디스크 흉내. 이상 없음(큐 깊이 1 흉내가 원자 CAS).
- FileWatch — 읽음 — ReadDirectoryChanges 넘침 · 중복 제거. 이상 없음. Linux 판은 안 읽음.
- Host — 읽음 — 원격 디버그 채널(루프백 + 토큰). 이상 없음.
- Maintenance — 읽음 — Saved 보존 정리(규칙은 `Config/Dev/SavedRetention.json`, 이름 바꿔 차지한 더미만 지움). 이상 없음.
- ModuleReload — 읽음 — Live Coding · 도메인 리로드. 이상 없음(섀도 복사, ABI 지문). `ModuleCompiler` 는 안 읽음.
- Net — 읽음 — 리슨 포트 개발 변수(기본 루프백). 이상 없음. 회선 흉내는 머리말만.
- Profiling — 훑음 — 파일 목록. Shipping 제외만 확인.
- RenderDoc — 읽음 — 캡처 연동. 이상 없음.
- ServerPose — 훑음 — 머리말 설계(4 Hz 읽기 태스크, 토큰). 본문은 안 읽음.

### Editor (5)

- AssetActions — 읽음 — Place Actor 트랜잭션. 이상 없음(다중 오브젝트 프리팹 redo 는 E-16 에 추정으로).
- Common — 읽음(Workspace · Commands · GUI · Asset/Reference · Task · Input) — 트랜잭션 버퍼 · AssetRegistry · PIE. P0-1 ~ P0-4, E-1 · E-3 · E-5 · E-6 · E-8 · E-9 · E-11 ~ E-15. Localization · Widgets · 임포터 본문은 훑음.
- Panels — 읽음(Scene · Inspector · ContentBrowser · Material · Autosave · Packaging · UIPreview · TileMap · Console) — Details 패널 · Outliner · Undo History. E-2 · E-7 · E-13 · E-16. 32 개 패널 가운데 Animation · AudioMixer · Catalog · MapCheck · Mesh · Physics · Prefab · ProjectSettings(워크트리에서 수정 중) · RenderTarget · Settings · VFX 본문은 훑음.
- SelfTest — 읽음 — 트랜잭션 커버리지. E-16.
- Viewport — 읽음 — 히트 프록시 · 그룹 기즈모. E-4 · E-10. 카메라 · 그리드 · 지형 붓은 안 읽음.

### GameFramework (4)

- Client — 읽음(HUD · Marker · Ability) — 시뮬과 표현 분리. G-9. Flow 는 훑음.
- Kits — 읽음(Feature/Online · Network · Storage · World · Content, Genre 여섯 갈래) — OnlineSubsystem · PlayFab · Lyra · 템플릿. P0-8, N-3 · N-5 · N-9 · N-13 · N-15, O-1 ~ O-18, K-3 ~ K-14. 장르 키트 가운데 MechArena · AsymmetricHorror · Metroidvania · ActionAdventure · ActionPlatformer · KartRacing · Rhythm · GhostHunt · SurvivalHorror · MonsterHuntRPG · CreatureLife · RestaurantSim · SideScrollConquest · TacticsSRPG 는 패턴 검색만 했다.
- Plugin — 읽음(Editor/MeshModifiers · CharacterEditor · GameplayTools/Ability) — 에셋 에디터 dirty · 트랜잭션. G-12, E-9. Modifiers(59 파일) · CameraPresets 는 훑음.
- Runtime — 읽음(Gameplay · Foundation/Framework · World · Character 일부 · Online) — Gameplay Framework · GAS · SaveGame · Lyra. P0-5 · P0-6, G-1 ~ G-13, O-2 · O-9 · O-17. `ScheduleSystem` · BehaviorTree · Appearance · Vehicle · Movement 는 안 읽음.

### Games (14)

- AbilityArena — 읽음 — 탄 판정 · 세이브. K-11.
- Empty — 읽음 — 모드 준비 훅(계약대로). K-4 · K-14.
- FrontierTown — 읽음 — 시간 배율. K-2.
- HarvestValley — 읽음 — 하루 넘김 · 출하 · 세이브. 이상 없음.
- LanternVale — 읽음 — 서버 권위 · 저장. K-1 · K-13.
- MeadowVillage — 읽음 — 시계 알림 진행. 이상 없음. K-4.
- NileCity — 읽음 — 뷰 표 · 배속. K-7 · K-10.
- PixelTown — 읽음 — 시작 검증. 이상 없음.
- Shooter3D — 읽음 — 발사 · 풀 · 서버 되감기. N-4, K-4. 적 AI 본문은 안 읽음.
- StarSkirmish — 읽음 — 락스텝 체크섬 · 배속 · 맵. K-6 · K-7.
- ThemeParkTycoon — 읽음 — 세이브 · 설정. K-4.
- Velmora — 읽음 — 확장 팩 · 지역 표. K-9.
- VoxelCraft — 읽음 — 틱 자리 · 세이브 · 생성. K-3 · K-8 · K-12.
- WildWest — 읽음 — 틱 뒤 큐 사용. 이상 없음.

### App (5) · ModuleAPI (3) · ModuleManager (2) · Programs (2) · Server (1)

- App/Android — 읽음 — native_app_glue 수명. P0-9.
- App/Dev — 훑음 — 백엔드 교체 · 대화형 단언(배포 제외 확인).
- App/Resources — 해당 없음(아이콘 · rc).
- App/Tests — 훑음 — 파일 하나.
- App/Web — 훑음 — 영속 루트 flush 호출부.
- ModuleAPI/ABI — 읽음 — C ABI 버전 + 헤더 SHA 스탬프. 이상 없음.
- ModuleAPI/Export — 훑음 — 내보내기 매크로.
- ModuleAPI/Service — 읽음 — `IModuleStateStore` 바이트 인계. 이상 없음.
- ModuleManager/Host — 훑음 — 서버 디버그 제어 계약과 App 호출부. 루트의 `GameModuleManager.cpp` 리로드 구간은 읽음(직렬화 실패 때 옛 모듈 유지 — 이상 없음).
- ModuleManager/Tests — 훑음.
- Programs/OnlineLoadBot — 읽음 — k6 · Gatling 실행 모델. O-12.
- Programs/ReflectionParser — 읽음 — UHT 증분 판정. A-8. `CodeGenerator.cpp` 는 안 읽음(NEXT 5-4 통합 검증과 겹친다).
- Server/Windows — 읽음 — SCM 서비스 호스트. P-14. 루트 `ServerApp.cpp` 의 틱 루프 · 드레인 · 종료는 이상 없음(고정 스텝, 따라잡기 상한).

## 깊이 못 본 곳

다음 감사가 이어서 볼 자리다. 여기의 결함은 이 문서에 없다.

- **RHI:** 백엔드별 PSO 생성의 스레드 안전과 전송 티켓 계약, WebGPU · GL CommandContext, Vulkan 장치 고르기, EGL · GLX 문맥 생성.
- **렌더러:** `FramePassExecutorPasses.cpp`(86 KB) 대부분, `RenderGraph::executeParallel` 본문과 배리어 묶음, 모프 · 정점 애니메이션 풀, 실시간 프로브, forward 계열 파이프라인 XML 과 나머지 셰이더.
- **직렬화 · 리플렉션:** `SerializerUtil.cpp`(69 KB) · `XMLSerializer.cpp` · `DataInheritance.cpp` · `ResourceComposer.cpp`, ReflectionParser 의 `CodeGenerator.cpp`(W-R3 · W-R4 와 겹친다).
- **입력 · 창:** `InputMap` 평가와 고정 틱 사이의 엣지 유실, `WaylandWindow.cpp`, UI 접근성 브리지.
- **온라인:** `OnlineCore/Server/HTTP`(HPACK · HTTP/2 약 90 KB — 동적 표 · 프레임 길이 상한 미확인), Apple · Google 영수증 검증기 본문, RESP 재연결 뒤 응답 짝 맞추기.
- **게임플레이:** `ScheduleSystem.cpp`, BehaviorTree · Blackboard, Appearance(55 파일), 장르 키트 14 개의 내부 로직.
- **에디터:** 임포터(Model · Texture · Vrm 약 9 천 줄), Animation 패널 49 파일, 프리팹 오버라이드 적용, 도킹 · 레이아웃.
- **실행으로만 확인되는 것:** 이 문서의 성능 항목 전부(측정 전), 외부 API 동작에 기댄 항목(P0-9 · P0-10 · P0-15, R-1, P-5 · P-12 · P-13, O-15), 추정으로 표시한 항목.
