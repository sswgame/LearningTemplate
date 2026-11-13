# 상용 엔진 대비 감사 (2026-11-12)

> 질문: 언리얼 5 · 유니티 6 · Godot 4 의 같은 시스템과 견주어 이 엔진에 남은 결함 · 성능 병목 · 구조 빈틈은 무엇이고 무엇부터 고치는가?
> 독자: 다음 묶음을 고르는 통합 담당과 그 일을 받는 작업자(에이전트와 사람).
> 결론: P0 는 모두 닫혔다. 열린 것은 P1 50 건 · P2 63 건이고 실행 단위는 "다음 할 일" 이다. 닫힌 항목은 지우고, 코드 주석이 번호로 가리키는 것만 맨 아래 "닫힌 항목" 에 한 줄씩 남긴다.

기준 커밋은 main `e480b07498` 이고 줄 번호는 그 커밋의 것이다. 감사 범위(`Source/` 2단계 폴더 122 개 체크리스트)와 닫힌 항목의 원문은 그 커밋의 이 문서에 있다.

## 읽는 법

- **우선순위:** P0 — 지금 코드 경로로 확인된 오동작 중 데이터 · 진행 · 기동 · 보안에 닿는 것. P1 — 조건부 · 잠재 결함과 확장 · 성능 병목. P2 — 개선과 작은 결함.
- **확신:** "확인" 은 그 줄과 호출자를 읽었다는 뜻이다(실행 재현 없음). "추정" 은 코드만으로 확정하지 못한 것, "측정 필요" 는 구조는 확인했지만 비용을 재지 않은 것이다.
- **외부 API:** 감사 때 context7 이 없어 Win32 · DXGI · Vulkan · NDK · Emscripten · FCM 동작 주장은 기억 기준이다. 고치기 전에 그 판의 문서로 대조한다.
- **항목 한 줄:** 제목(분류 · 확신) — 근거(`파일:줄`), 상용 엔진, 제안, 확인(먼저 써서 지는 것을 볼 테스트). 경로는 저장소 루트 기준이고, 앞부분이 뻔한 경로는 파일 이름만 적는다.
- **다른 문서와의 관계:** 순서 · 다음 일의 정본은 [NEXT](NEXT.md), 설계 판단은 [DesignBacklog](DesignBacklog.md) 다. "(보강)" 은 그 문서들에 있던 일에 새 근거를 단 항목이다.

## 되풀이되는 모양

증상을 하나씩 고치지 말고 이 모양을 쓸 수 없게 형식을 바꾼다(괄호의 닫힌 번호는 그 모양의 앞 사례).

1. **세대 없는 번호 · 순번 · 경로 글을 정체로 쓴다.** 컴포넌트 `이름#순번`(A-2), 재시작마다 1 부터인 id(O-7), 32 비트 이름 해시(A-22 · 닫힌 C-8).
2. **같은 사실을 두 곳이 따로 안다.** 쓰는 쪽 · 읽는 쪽의 판(A-5), 개수는 CPU · 목록은 GPU(RN-1), 캐시 기록과 서버 메모리 표(O-6).
3. **실패 · 잘림을 값으로 돌려주지 않거나 호출자가 버린다.** 삼각 분할 실패(C-2), 매치 명단 발행(O-6).
4. **플랫폼 창이 "빠뜨려도 컴파일되는" 선택 훅을 가진다.** 포커스 · 글 입력 · 쉬기 · 수명 구독(P-3 ~ P-6).
5. **플레이어 세이브 · 설정이 판 올림 없이 엄격한 형식을 쓴다.** 사용자 설정 판(A-5), 컴포넌트 정체(A-2).
6. **상태 변화를 값이 아니라 호출 순서 규칙으로 다룬다.** 배속(K-6 · K-10), 전환 중 전환(G-13).

## P1 — 조건부 결함과 확장 · 성능 병목

### Core

- **C-2 삼각 분할 실패가 "빈 목록" 이 아니라 구멍을 덮는 부채꼴을 남긴다** (결함 — 계약 확인, 반례는 손 시뮬레이션) — `Source/Core/Spatial/PolygonTriangulation.cpp:194-201` 은 실패 때 남은 부분을 부채꼴로 붙이고 false 를 돌려주는데 호출자 둘(`MeshFracture.cpp:310` · `PolygonFracture.cpp:273`)은 결과를 버린다. 다리 놓기(`:79-84`)는 구멍 둘이 같은 바깥 꼭짓점을 고르면 막히고 귀 자르기는 최악 O(n³). 실패 계약만 먼저 바꾼 시도(064d2a93a9)는 파쇄 단면에 구멍을 내 되돌렸다(70f813a588) — 막힘을 먼저 없앤다. 상용: earcut `locallyInside`, polypartition `InCone`. 제안: 다리 후보에 모서리 원뿔 검사, 귀 후보를 오목 꼭짓점 목록으로 좁힌 뒤 실패하면 결과를 되돌린다. 확인: 바깥 사각형 + 같은 바깥 꼭짓점을 고르는 구멍 둘에서 `triangulate` 가 true 이고 넓이 합이 맞는지.
- **C-6 출하 전역 할당기가 CRT `malloc` 그대로다** (성능 — 측정 필요) — `Source/Core/Memory/Memory.cpp:148-158 · 224-234`. 스레드 캐시 · 크기 등급 할당기가 없다. NEXT 의 "메모리 풀은 측정 뒤" 의 측정 대상이다. 상용: 언리얼 `FMallocBinned2/3`, 유니티 TLSF + 스레드 버킷. 제안: `Memory::allocate` 한 입구라 병목이면 출하 구성만 mimalloc(vcpkg overlay). 확인: Release 프레임당 할당 수 · 시간(`MemoryProfiler` · Tracy), 워커 N 개 16 ~ 256 B 벤치.
- **C-7 `gv_` 문자열 변수의 잠금을 직접 읽는 쪽이 잡을 수 없다** (결함 · 경쟁 — 코드 확인) — 매크로가 보통 전역을 만들고(`GlobalVariableManager.h:461-464`) 문자열은 쓰는 쪽만 `s_stringVarMutex` 를 잡는다. `FrameRenderer.cpp:177` 이 `gv_renderPipeline` 을 렌더 스레드에서 `string_view` 로 쥐어 콘솔이 값을 바꾸면 해제된 버퍼를 읽는다. 상용: 언리얼 `TAutoConsoleVariable` 은 게임 · 렌더 스레드 값을 따로 둔다. 제안: 잠금 안에서 사본을 돌려주는 접근자 형식(`GlobalString::get()`)으로 직접 참조가 컴파일되지 않게, 렌더 스레드 변수는 프레임 스냅샷 한 길로. 확인: TSan 에서 `gv_renderPipeline` 반복 변경 시나리오.
- **C-9 컨테이너가 std 와 같은 이름 · API 인데 복잡도가 다르다** (성능 — 측정 필요) — `vector.h:977-988` 범위 `insert` 가 원소마다 삽입(`ByteStream.h:54` · `Archive.cpp:112`), `deque.h:14` 는 `std::deque` 래퍼(MSVC 에서 큰 원소마다 힙 할당), `unordered_map.h:645` `reserve` 는 버킷만, `map.h:30` 정렬 벡터라 대량 구축 O(n²). 상용: `TArray::Append` · `TDeque` 링 버퍼 · `TMap::Reserve`. 제안: 한 번 예약 → 한 번 밀기, 2 의 거듭제곱 링 `deque`, `map` 범위 구축은 붙이고 `stable_sort`. 확인: 1 MB 범위 삽입 · `deque` 1 만 push 할당 수 · 10 만 키 `map` 구축 벤치.
- **C-14 결정성 표시가 손으로 적은 TU 목록이라 빠진 것이 있다** (구조 — 목록 확인, 비트 차이 추정) — `Source/Core/CMakeLists.txt:190` 의 다섯 TU 에 `MatrixMath.cpp`(결정성 물리 TU 가 부름) · `ValueNoise.cpp` 가 없고 libm 초월 함수를 막는 검사도 없다. 상용: 유니티 Burst `FloatMode.Deterministic`. 제안: Core Math 티어 통째 `-ffp-contract=off`(비용은 재서). 확인: `quaternion::operator*` · `slerp` · `ValueNoise::sampleFractal` 기록 비트 고정 케이스를 Debug · Release · Linux 에서.

### 네트워크 · 텔레메트리

- **N-7 게임 UDP 포트의 지표가 운영 끝점에 없다** (구조 · 관측 — 확인) — 등록 지표는 `ServerObservability.cpp:81-85` 의 틱 · 드롭 · 사용률뿐이고 `NetEndpoint` 의 거절 · 인증 실패 · 재생 거절 수(`NetEndpoint.h:332-338`)는 테스트만 읽는다. 접속 로그가 클라이언트 IP 를 Info 로 남긴다(`GameNetSession.cpp:624`). 상용: 언리얼 NetDriver 통계, 유니티 Transport NetworkMetrics. 제안: `GameNetSession` 이 `MetricRegistry` 에 샤드 라벨로 등록, 끊김 사유 카운터는 `NetDisconnectReason` 에서 생성, 로그 주소는 가림 형식. 확인: `OpsHTTPEndpointTest` 가 `/metrics` 의 `net_connections` · `net_rejected_packets_total` 을 본다.

### 온라인 서비스 · 저장소

- **O-3 비밀번호 해시(Argon2id)가 저장소 워커 스레드에서 돌고 저장소 대기열에 상한이 없다** (성능 · DoS — 측정 필요) — `LoginService.cpp:83-108` 의 로그인 일 안에서 `LoginStoreLogic.cpp:554` 가 해시(19 MiB × 2, 워커 4)를 내 원장 · 우편 · 거래가 뒤에 선다. `SQLConnectionPool.cpp:138-149` 대기열 무제한. 상용: OWASP — KDF 는 DB 연결과 분리된 연산 풀 + 세마포어. 제안: 읽기(저장소) → 해시(상한 있는 연산 풀, 넘치면 `kBusy`) → 커밋(저장소), 저장소 대기 상한. 확인: 부하 봇 `login_storm` 을 `_bLightPasswordHash` 없이 돌려 `service_store_pending_peak` 와 다른 키트 지연(O-12).
- **O-6 매치 명단이 "최대 한 번" 버스로만 게임 서버에 가고 받는 쪽 버퍼는 읽는 곳이 없다** (결함 + 구조 — 확인) — `MatchQueueService.cpp:565-566` 이 `(void)` 발행 뒤 Found 를 보낸다. 게임 서버는 버스로 채운 메모리 표(`MatchServerAgent.cpp:36-43 · 80-84`)뿐이라 메시지 하나를 잃으면 전원이 접속에서 거절되고, 표 수명 2 분이라 경기 중 재접속도 거절된다. `pollNewMatches` · `findExpected` · `requestBackfill` 호출자 0. 상용: Agones Allocation · PlayFab MPS 는 배정을 영속 기록으로. 제안: 명단을 캐시 기록(`mm/match/<서버>/<경기>`, 수명 = 경기 시한)으로, 소비자 없는 버퍼는 지운다. 확인: 발행 실패 가짜 버스로 "Found 인데 `isAccepted` false".
- **O-7 티켓을 낸 서버가 죽으면 파티가 "줄 섬" 에 갇힌다** (결함 — 확인) — id 순번을 난수에서 시작하는 앞몫은 들어갔다(fb0b786ee2). 남은 것: `PartyLobbyService` `:304` 는 `_queuedTicketID != 0` 이면 AlreadyQueued, `MatchQueueService.cpp:743-747` `leaveQueue` 는 로컬 표에 없으면 NotQueued 라 다른 서버로 붙은 파티장이 줄을 서지도 떠나지도 못한다. 상용: EOS · 유니티 Lobby 는 멤버 하트비트로 낡은 상태를 걷는다. 제안: 로컬 티켓이 없으면 파티 기록의 티켓을 푼다. 확인: `TestMatchQueueService` "A 에서 티켓 → A 버림 → B 에서 leave → join".
- **O-12 부하 봇이 원장 경로를 치지 않고 가벼운 해시 · 닫힌 루프로 잰다** (구조 · 검증 공백 — 확인) — 동작 표(`LoadBotScenario.cpp:33-53`)에 구매 · 영수증 · 우편 수령 · 거래 · 길드 창고 · 점수 제출이 없고, `Shooter3D.online.json` 의 `_bLightPasswordHash: true` 라 O-3 이 안 보이며, 다음 요청을 답 뒤에 내 coordinated omission 이다(`LoadBot.cpp:521`). 상용: k6 · Gatling 도착률 고정 실행기. 제안: `economy_purchase` · `mailbox_claim_all` · `trade_pair` · `leaderboard_submit` · `guild_storage` 동작, `<Load>` 도착률 모드(지연은 보내려던 시각 기준), 끝에 `LedgerAuditReport` 균형 검사. 확인: PostgreSQL 구성(`Shooter3D.external.json`)의 Conflict 재시도율 · `kBusy` 비율.

### RHI · 그래픽스 자원

- **R-1 DXGI Alt+Enter 를 막지 않는다 — 전체 화면 전환 뒤 테어링 Present 가 실패할 수 있다** (결함 — API 부재 확인, 증상 추정) — `MakeWindowAssociation` · `SetFullscreenState` 호출이 없고 스왑체인은 늘 `ALLOW_TEARING` 이다(`D3D12RHISwapChain.cpp:61`, `D3D11RHIDevice.cpp:499-505`). 실패하면 "Present failed" 한 번 뒤 계속 돈다(`D3D12RHIDevice.cpp:1826-1845`). 상용: 언리얼 D3D12RHI `DXGI_MWA_NO_WINDOW_CHANGES`. 제안: DX11 · DX12 공용 스왑체인 생성 입구에서 창 연결을 끄고 전체 화면은 테두리 없는 창 하나로. 확인: vsync 끔 + Alt+Enter 뒤 `D3D12.PresentFailedHr…` 로그.
- **R-3 정적 메시 버퍼가 CPU 가시 메모리에 산다** (성능 · 구조 — 비용 측정 필요) — DX12 `createVertexBuffer` · `createIndexBuffer` 가 UPLOAD 힙(`D3D12RHIResourceFactory.cpp:726-729 · 765-786`), Vulkan 은 모든 버퍼가 `HOST_VISIBLE | HOST_COHERENT`(`VulkanRHIDevice.cpp:695`), `findMemoryType` 은 매번 속성을 다시 묻는다. 상용: 언리얼 · Godot 정적 버퍼는 DEFAULT/DEVICE_LOCAL + 스테이징. 제안: 용도(정적 · 동적)를 형식으로 받고 정적은 장치 로컬 + 전송 링(`RHIStagingAllocator`). 확인: 외장 GPU 메시 많은 bench 의 깊이 프리패스 · G버퍼 시간, DXGI non-local 사용량.
- **R-4 (보강) 자원마다 전용 할당 — 머티리얼 상수 버퍼 하나가 커밋 자원 · `VkDeviceMemory` 하나다** (구조 — 확인, NEXT RHI 5-2 "Vulkan 서브할당" 의 근거) — `MaterialRenderProxy.cpp:48` 이 인스턴스마다 `createConstantBuffer`, DX12 는 `CreateCommittedResource`(64 KB 단위), `PassConstantRing.h:34` 4096 칸이면 최악 256 MB. Vulkan 은 버퍼 · 텍스처마다 `vkAllocateMemory` 이고 `maxMemoryAllocationCount` 를 읽지 않는다(데스크톱도 흔히 4096). 상용: 언리얼 `FD3D12FastConstantAllocator` · Vulkan `FMemoryManager`, 유니티 SRP Batcher, Godot VMA. 제안: RHI 공용 `RHIBufferSlice{ buffer, offset, size }` — 상수 버퍼는 구간으로만, 메모리는 D3D12MA / VMA(vcpkg overlay) 또는 `SpanAllocator`. 그 전이라도 할당 수가 한도 근처면 오류로 멈춘다. 확인: 머티리얼 인스턴스 5000 개의 버퍼 바이트 · Vulkan 할당 수.
- **R-6 텍스처 스트리밍이 "나중에 올리기" 뿐이고 전역 세대 하나가 모든 머티리얼을 다시 돌린다** (성능 · 구조 — 측정 필요) — 대기 줄이 텍스처마다 `DDSImageData` 전체를 쥐고 프레임당 제출 2(`TextureCache.h:58`, C++ 상수)라 500 장이면 250 프레임 동안 RAM 에 남는다. 화면 크기는 순서만 정하고 모두 전체 밉으로 올라가며 내리는 길이 없다(`TextureStreamingSystem.cpp:30-75`, `MemoryBudget.json` Texture 1024 MB 를 읽지 않음). 승급마다 `_reloadGeneration` 이 모든 `Material::refreshTextureBindings` · `MaterialInstance` 재패킹을 부른다. 상용: 언리얼 `FRHITextureReference` + 풀 예산 밉 계산, 유니티 Mipmap Streaming. 제안: `Texture2D` 가 bindless 번호를 수명 내내 유지하고 승급은 같은 번호의 디스크립터를 다시 쓴다. 대기 줄은 경로 + 밉 범위, 쿠킹 때 UV 밀도로 원하는 밉, 예산 넘으면 낮은 우선도부터 내린다(블록 읽기 A-12 위). 확인: `load/*` 의 `RT.TextureStreaming` · 머티리얼 `sync` 횟수, 4K 300 장을 지난 뒤 텍스처 메모리가 예산 아래로 오는지.

### 렌더러

- **RN-1 압축을 포기한 투명 배치(Preserve)가 쓰지 않은 가시 칸을 읽어 그린다** (결함 · 조건부 — 경로 확인, 화면 추정) — `gpu_cull.hlsl:308-325` 는 절두체 밖 인스턴스가 제자리 쓰기 전에 돌아가는데 개수는 CPU 값 전체이고(`GPUScene.cpp:440-442`) 가시 버퍼는 초기 내용이 없다(`:295`). 조건은 투명 배치 하나가 512 인스턴스를 넘을 때(`:417`) — 2D 스프라이트 · 타일 장면. 상용: 언리얼 `FInstanceCullingContext`, 유니티 BRG 는 목록을 전부 쓴다. 제안: Preserve 배치는 컬링 판정 앞에서 제자리 쓰기를 하거나 CPU 가 항등 값을 올린다. 확인: 투명 600 개 한 배치 · 절반 화면 밖 골든(dx12 · vk) + GPU assert.
- **RN-2 TAA 에 서브픽셀 지터가 없어 안티에일리어싱이 되지 않는다** (결함 · 화질 — 확인) — `taa.hlsl` 은 재투영 · 3x3 클램프 · `kHistoryWeight = 0.9` 뿐이고 jitter · Halton 은 Grep 0 건, FXAA · SMAA · MSAA 도 없다. 상용: 언리얼 TAA/TSR · URP/HDRP · Godot TAA 는 Halton 지터. 제안: 뷰 설정에 지터 오프셋 형식, `applyViewProjection` 이 지터 든 · 안 든 행렬을 함께 싣고 속도 · 컬링 · 그림자는 안 든 쪽만 받게 서명을 가른다. 확인: `TAAJitterTest`(평균 0, 속도에 지터 없음), 가는 선 격자 16 프레임 골든.
- **RN-3 광원을 뷰와 무관하게 전부 싣고 1024 를 넘으면 목록 뒤부터 자른다** (구조 · 확장 — 확인) — `GPULightBuffer.cpp:223-305` 는 절두체 판정 없이 싣고 `limitCPULightCount` 가 뒤(파티클 라이트 · 프로브 · 반사 프로브)를 버리며 2D 빛 꼬리 원소가 중간에서 잘릴 수 있다. 상한 · 그리드는 HLSL `#define`(`binding_slots.hlsli:341 · 392-394`). 상용: 언리얼 · URP · Godot 는 뷰마다 가시성 → 중요도 → 클러스터. 제안: 광원 경계 등록부 + 뷰 컬링 + 중요도 예산, 자르는 단위는 "빛 + 꼬리", 상한은 Config → 쿠킹 define. 확인: 광원 2000 중 화면 안 50 이 남는지, `load/*` 광원 4000.
- **RN-4 그림자 아틀라스가 2x2 고정 사분면이다** (구조 — 확인) — `ShadowAtlasLayout::make` 는 로컬 타일이 하나라도 있으면 캐스케이드를 2 로 줄이고 반 해상도로 한다(`ShadowSetup.h:56`). 로컬 용량(점광 1 + 스폿 2)을 넘으면 로그 없이 `continue`(벽 너머로 빛이 샌다), 순서가 뒤집히면 `ShadowCache::update` 가 캐시 층 전체를 다시 그린다. 상용: URP 주광 캐스케이드와 추가 광 가변 아틀라스 분리, 언리얼 VSM. 제안: 방향광 · 로컬 텍스처를 가르고 로컬은 화면 크기 할당기, 캐시 키는 빛 정체(컴포넌트 id, 면), 못 받은 빛은 통계 · 로그. 확인: `ShadowSetupTest` — 스폿 추가에도 캐스케이드 그대로, 우선순위가 뒤집혀도 서 있는 빛은 Reuse.
- **RN-5 점광 · 스폿 타일과 추가 뷰 캐스케이드가 합친 컬링 칸 하나를 쓴다** (성능 — 측정 필요) — `FramePassExecutorDraw.cpp:247-249` — 점광 하나가 방향광 볼륨 전체를 6 번 정점 처리한다. 인스턴스 배치(지형 · 식생)는 정적이 아니라 봐서(`GPUSceneBuilder.cpp:462`) 캐시 타일에서도 매 프레임 다시 그린다. 상용: 언리얼 · 유니티는 그림자 절두체마다 컬링. 제안: 타일마다 컬링 칸 또는 인스턴스 타일 비트마스크, 식생 · 지형에 "정적 그림자 캐스터" 선언. 확인: Shadow `GPU.PassMs` · `RT.Draw.gpuBatches` 를 점광 0 · 1 · 2(Release 식생 장면).
- **RN-6 포함 여부가 하나라도 바뀌면 GPUScene 을 통째로 다시 모은다** (구조 · 확장 — 측정 필요) — `GPUSceneBuilder.cpp:830-835`(집합 세대 `:726` 도 같다). 식생 셀 토글(`FoliageComponent.cpp:763`) · 스폰 · 소멸 · 스트리밍이 모두 이 길이다. 상용: 언리얼 `FScene` 프리리스트 칸, 유니티 GPU Resident Drawer. 제안: 인스턴스 번호를 "오래 가는 칸 + 프리리스트" 로, 배치 소속은 인스턴스 id 목록으로 간접화, 숨김은 플래그. 확인: `load/*` 20 만 + 매 프레임 스폰 · 소멸 10, Tracy `GT.GPUScene.build.collect`.
- **RN-7 지형 청크 LOD 가 바뀔 때마다 CPU 가 메시를 새로 만든다** (성능 · 구조 — 측정 필요) — `TerrainComponent.cpp:866-966`(`updateLODs` → `rebuildChunkMeshes`), 프레임 상한 없음(컷이면 전 청크), 시점 하나(`:270-272`). 상용: 언리얼 랜드스케이프 LOD 별 공유 인덱스 + 정점 모핑. 제안: 청크당 최고 해상도 정점 한 벌 + (LOD, 이웃 마스크)별 공유 인덱스. 확인: `rebuildChunkMeshes` 구간을 2 km 지형 20 m/s 횡단 · 컷에서.
- **RN-8 자동 렌더 배율이 벽시계 프레임 시간으로 판단하고 단계마다 뷰 자원 전체를 다시 만든다** (결함 + 구조, 기본 끔 — 확인) — `RenderPacketViewSetup.cpp:62-97` 은 vsync 60 Hz 에서 "여유" 조건이 서지 않아 내려간 배율이 돌아오지 않는다. `FrameViewResources.cpp:214-256` 은 크기 변경마다 첨부 · TAA · 노출 기록을 다시 만든다. 상용: 언리얼 Dynamic Resolution · 유니티 `FrameTimingManager` 는 GPU 시간, 타깃은 최대 크기 + 뷰 사각형. 제안: 입력을 `GPUPassTimeline` GPU 프레임 시간으로, 풀은 최대 배율로 한 번. 확인: `RenderScaleAdjusterTest` "16.67 ms 고정에서 배율 복귀", 배율 바꾼 프레임 `RT.Frame.transientAllocations` 0.
- **RN-9 GPU 방출기가 한 프레임 빠졌다 돌아오면 처음부터 빨리감기한다** (결함 — 경로 확인) — 비활성 컴포넌트는 실리지 않고(`SceneVFXProvider.cpp`) 렌더 쪽은 상태를 놓아(`VFXGPUSimulation.cpp:289-306`) 다시 실리면 `_stepCount` 전체를 프레임당 32 단계로 따라잡는다 — 10 분 돈 횃불은 약 19 초 32 배속. 상용: 나이아가라는 리셋 · warm-up. 제안: `stepCount - ceil( lifetimeMax / step )` 부터만 디스패치, 상태 해제는 재생 토큰이 죽었을 때. 확인: 3600 단계 요청의 첫 프레임 디스패치 수가 수명 창 이하.

### 에셋 · 직렬화 · 리플렉션 · 오브젝트 · 씬

- **A-2 컴포넌트 · 중첩 프리팹 부품의 영속 정체성이 순번이다** (구조 · 잠재 데이터 오염 — 확인) — `ComponentStableKey.cpp:32-58` 키가 `이름#몇 번째` 이고 프리팹 덮어쓰기(`PrefabOverrides.cpp:85-103`) · 세이브 컴포넌트 핸들(`ObjectStateSerializer.cpp:346 · 453`)이 쓰며, 중첩 부품 키는 노드 인덱스다(`PrefabAsset.cpp:499`). 앞 형제를 지우거나 끼우면 덮어쓰기와 쌓인 세이브가 경고 없이 다른 대상으로 간다. 상용: 유니티 fileID, 언리얼 SCS `VariableGuid`, Godot 유일 노드 이름. 제안: 저작 시점 고정 지역 id(64 비트)로 키 · 부품 경로 · 세이브 핸들, 옛 형식은 `SchemaMigrate`(에셋 GUID 작업과 같은 줄기, A-20). 확인: `PrefabOverridesTest.OverrideFollowsComponentWhenAnEarlierSiblingIsRemoved`, `ObjectStateSerializerTest.SavedHandleSurvivesSiblingInsertion`.
- **A-5 사용자 설정 판 번호가 스키마 여럿의 최댓값 하나다** (결함 · 잠재 — 첫 `<Upgrade>` 에서 드러남) — `UserSettingsSchema.cpp:661` `max( _version, overlay._version )` 하나를 파일에 쓴다(`UserSettingsFileEntry.cpp:139`). 엔진 3 · 게임 1 파일에서 게임이 2 로 올려도 단계가 돌지 않고, 새 판 파일을 옛 빌드가 옛 번호로 다시 써 멱등이 아닌 `Scale` 이 또 돈다. 상용: 언리얼 `FCustomVersion`(주인 GUID 별 번호). 제안: 주인별 판(`"versions": { "engine": 3, "game": 2 }`), 빌드보다 새 파일은 덮지 않고 옆에 보관, 모르는 키는 원문 보존. 확인: `UserSettingsFileEntryTest.GameUpgradeRunsWhenEngineVersionIsHigher` · `NewerFileIsNotRewrittenAsOlder`.
- **A-7 공유 에셋의 제자리 다시 읽기가 `shared_ptr<const T>` 를 쥔 다른 스레드와 겹칠 수 있다** (결함 · 잠재 경쟁) — `SharedAssetTable.h:104-115` `*live = std::move( fresh )`. 캐시는 워커 호출을 허용하고 배포에서도 매 프레임 `reloadComposedDependents`(`Engine.cpp:452-455`) · `ModSession.cpp:322` 가 다시 읽는다. 상용: 언리얼은 새 객체 + 게임 스레드 참조 교체. 제안: 다시 읽기 방식을 `Detach`(새 객체 + 다시 읽은 횟수) 하나로. 확인: TSan `SharedAssetCacheTest.ReloadWhileWorkerReads`.
- **A-8 ReflectionParser 증분 판정에 명령줄이 없다** (결함 — 확인) — `GeneratedFiles.cpp:226-249` 스탬프는 도구 입력 파일 시각뿐이라 `--module` · `-D…` · `--include` 를 바꿔도 옛 `.gen.cpp` 가 남는다. 상용: UBT 는 UHT 매니페스트 변경으로 무효화. 제안: 스탬프에 `args <해시>` 필수 줄. 확인: `RunParserToolInputs.cmake` `MODE=args-changed`.
- **A-9 패치 서명 검증이 기동마다 패치 팩 전체를 읽어 해시한다** (성능 · 확장 — 측정 필요) — `PackManager.cpp:685-703` · `PackPatch.cpp:86-99`(2 GiB 이상 거절), 사슬 전체 · 매 기동, 검증한 바이트와 마운트 핸들이 다르다. 상용: 언리얼 pak 블록 해시 표(`.sig`). 제안: 목록이 "FAT + 스트링 풀 해시" 를 서명하고 항목은 FAT 의 항목 해시로 읽을 때 확인. 확인: 1 GiB 패치 기동 시간 · 최고 메모리(`Asset.mount`).
- **A-10 팩 쓰기가 원본 · 압축 결과 · 팩 이미지를 모두 메모리에 든다** (확장 — 확인) — `PackWriter.cpp:132-139 · 446 · 507`, `CookStore.cpp:90-98` 은 쓴 팩을 다시 읽어 해시하고 DDC 사본, 팩은 도메인당 하나(`PackCooker.cpp:642-656`). 상용: 언리얼 IoStore 청크 스트림 쓰기 · 청크 id 분할. 제안: 항목을 하나씩 압축해 이어 쓰고 끝에 색인, 분할 규칙(크기 · 폴더 · 청크 id)은 PackConfig. 확인: 10 GB 합성 콘텐츠 `--cook-packs` 최고 메모리.
- **A-11 증분 쿠킹의 "바뀐 것 없음" 판정이 매번 모든 입력 · 출력을 읽는다** (성능 + 잠재 결함) — `CookRecord.cpp:154-158`, 해시 캐시는 프로세스 메모리뿐(`ContentHash.cpp:122-158`). `computeBytes`(`:55-66`)는 바이너리에서도 0x0D 를 건너뛰어 그 자리만 다른 두 파일이 같은 해시다. 상용: git 색인 · 언리얼 `FFileHashCache`(크기, 시각 → 해시). 제안: `ContentHashCache` 를 `DerivedData` 에 영속, CR 정규화는 텍스트만, 해시는 xxh3. 확인: 변경 없는 `--cook-all` 두 번째 읽은 바이트, `ContentHashTest.BinaryWithMovedCRHashesDifferently`.
- **A-12 팩 색인 메모리와 항목 통째 압축** (확장 · 성능 — 측정 필요) — `PackReader.cpp:464-510` 마운트 때 항목마다 경로 `string` 사본을 `unordered_map` 에, 읽기는 통째(`readFile` · `readFileAsync`)라 부분 읽기 불가(R-6 이 막힌다). 상용: IoStore 완전 해시 TOC + 고정 블록, 유니티 LZ4 청크. 제안: 정렬 FAT 이진 탐색 + 스트링 풀 오프셋, 큰 항목은 64 ~ 256 KB 블록 표 + `readRange`. 확인: 항목 30 만 팩 마운트 시간 · 색인 메모리.
- **A-13 씬 스트리밍: 모든 셀 서술이 메모리에 있고 셀 짓기가 게임 스레드 동기이며 내릴 때 상태를 버린다** (확장 · 성능 — 히치 측정 필요) — `SceneStreamingSubsystem.cpp:61-66 · 186-190 · 222-235`, `Scene.cpp:624-640`. 상용: World Partition 셀 패키지 비동기 + AddToWorld 시간 예산. 제안: 쿠킹이 셀 구간을 쓰고 런타임은 셀 색인만, 셀 로드는 워커 파싱 · 인스턴스화 + `processHeldRegistration` 예산, 내릴 때 `SaveGame` 상태를 셀 키로 보관. 확인: `load/` 셀 400 × 오브젝트 2000, `GT.Scene.service.SceneStreamingSubsystem` 최대 프레임.
- **A-16 쿠킹된 이진 번역 표가 어디서도 쓰이지 않는다** (성능 · 구조 — 사용처 없음 확인) — `LocalizationManager.cpp:418 · 449 · 461` 의 `loadBinaryTable` · `cookBinaryTable` 호출이 테스트 밖에 없고, 배포는 모든 문화권 XML 을 읽어 마운트마다 전부 다시 채운다(`:174-250 · 541-636`). 상용: 언리얼 `.locres` — 지금 문화권 + 폴백 사슬만. 제안: 쿠킹이 문화권마다 표를 굽고 배포는 조회 사슬만 읽는다. 안 쓸 것이면 형식을 지운다. 확인: 키 20 만 × 문화권 15 의 `Localization.mount` · 메모리.

### 물리 · 캐릭터 · 내비

- **S-2 Jolt 임시 할당자가 고정 16 MB 이고 넘치면 `std::abort()`** (결함 · 잠재 크래시 + 구조 — 헤더 확인) — `JoltPhysicsScene.h:352` · `.cpp:42-45` 의 바디 · 쌍 · 접촉 수 · 16 MB 하드코딩, Jolt 5.6.0 `TempAllocator.h:68-72` 는 넘치면 abort(크래시 처리기 안 탐). 같은 할당자를 스텝 · 캐릭터 · 소프트 바디가 쓰고 미리보기 씬마다 또 잡는다. 상용: `TempAllocatorImplWithMallocFallback`(같은 헤더 `:168`). 제안: 네 값을 `PhysicsSettings` 데이터로(없으면 로드 오류), 폴백형으로 바꾸고 폴백 스텝을 카운터 · 경고로. 확인: `PhysicsScene3DTest.LargeActivePileDoesNotAbort`, `Physics.TempAllocator.peakBytes`.
- **S-6 내비메시가 32 비트 폴리 참조(22 비트)에 묶이고 에이전트 종류당 한 장이다** (구조 · 확장 한도 — 헤더 확인) — `DetourNavMesh.h:28` `DT_POLYREF64` 꺼짐, `RecastNavMesh.cpp:34 · 405-420` 타일 × 폴리 ≤ 4M, `DetourNavCrowd.cpp:249` 가 uint64 → uint32 로 자른다. 스트리밍 없음. 상용: 언리얼 64 비트 참조 + 내비 스트리밍, 유니티 `NavMeshSurface` 조각. 제안: vcpkg overlay 로 `DT_POLYREF64`, 자르는 캐스트는 `static_assert`, `INavMeshTileStore` 위 관심 영역 타일 층. 확인: `RecastNavMeshTest.LargeGridFitsPolyRef`(8192 타일).
- **S-7 캐릭터 컨트롤러는 기본 설정에서 월드에 바디가 없다** (결함 후보 — 코드 확인, 게임 동작은 프리팹 구성에 달림) — `_bInnerBody` 기본 false(`PhysicsDesc.h:168`)이고 켜는 곳이 없으며 `SetCharacterVsCharacterCollision` · `CharacterContactListener` 등록도 없다 — 캐릭터끼리 통과하고 트리거에 잡히지 않는다. 상용: 언리얼 캡슐은 실제 바디, Jolt 샘플은 속 바디 + `CharacterVsCharacterCollisionSimple`. 제안: 속 바디 기본 + 씬이 캐릭터끼리 충돌 등록. 먼저 프리팹이 따로 키네마틱 바디를 다는지 확인. 확인: 캐릭터 둘 마주 걷기 거리 유지, 트리거 통과 `Physics.ContactLog` Begin · End.
- **S-8 (보강) 캐릭터 이동이 스텝마다 게임 스레드에서 한 명씩 돈다** (성능 — NEXT 5-3 "캐릭터 컨트롤러 입력 SoA" 의 보강) — `CharacterControllerComponent.cpp:310-377` 이 차례로 `moveCharacter`, `ExtendedUpdate` 는 씬 공용 `_tempAllocator`(S-2)를 받아 SoA 만으로는 병렬화가 안 된다. NEXT 5-3 의 "군중 `NavAgentCrowdSync` 일괄 쓰기" 는 이미 `applyTransformBatch` 라 그 줄은 낡았다. 상용: 유니티 DOTS Character Controller 잡. 제안: `moveCharacters( span )` 일괄 API + 레인별 임시 할당자. 확인: `load/*` 캐릭터 500 의 PreStep(워커 1 · 4 · 8).

### 플랫폼 · 창 · 입력 · 빌드

- **P-2 리사이즈를 메시지 처리 안에서 동기로 하고 Win32 모달 이동 · 크기 루프 동안 프레임이 멈춘다** (구조 · 성능) — `EngineRenderDriver.cpp:187-196` 크기 통보마다 `waitIdle` + `resize`, X11 은 `ConfigureNotify` 마다(`X11Window.cpp:642-654`), Win32 는 모달 루프에서 `tickMainLoop` 정지(리슨 호스트 틱 · 오디오 갱신 정지). 상용: Godot `WM_ENTERSIZEMOVE` 타이머로 `Main::iteration()`. 제안: 창은 대기 크기만 적고 프레임 시작에 한 번 적용, Win32 모달 루프는 타이머로 틱(재진입 가드는 창 형식에). 확인: `RHI.ResizeCount`, `windowdrag` 시나리오에서 `Engine.FrameIndex` 증가.
- **P-3 웹 · Android: 포커스를 잃어도 입력 관리자가 몰라 눌린 키가 남는다** (결함 — 확인) — `PlatformInputKind` 에 포커스 종류가 없고 웹 · Android 창은 `IWindow::setFocusState` 만 부른다. `makeFocusChange` 는 Win32 · X11 뿐. 상용: 언리얼 `OnWindowActivationChanged` 공통 경로. 제안: `setFocusState` 가 값이 바뀌면 포커스 델리게이트를 부르고 `InputManager` 가 그것만 구독, 개별 `makeFocusChange` 는 지운다. 확인: `PlatformInputTest.FocusLossReleasesHeldKeys`(`CallbackInputWindow`).
- **P-4 웹 · Android: 글 입력 경로가 없다(소프트 키보드 · IME 조합)** (결함 — 확인) — `applyTextInputWanted` · `applyTextInputArea`(`IWindow.h:494-496`)를 두 창이 재정의하지 않고, 웹은 `keypress` 만 받는다. 상용: 유니티 `TouchScreenKeyboard` · WebGL 숨은 input, Godot `virtual_keyboard_show`. 제안: 순수 가상으로 바꿔 구현하거나 "지원 안 함" 을 선언하게, 웹은 숨은 `<input>` 의 `beforeinput` · `composition*`, Android 는 GameTextInput. 확인: `WindowTextInputTest` 플랫폼별 케이스, `web-check` 에 `compositionend` 화면.
- **P-5 웹: 배경 페이싱이 브라우저 메인 스레드를 막고 Android 는 멈춘 동안 10 ms 폴링한다** (성능 — 측정 필요) — `FramePacer.cpp:62-66` 의 `sleepUntilNanoseconds` 는 Emscripten 메인 스레드에서 바쁜 대기(기억 기준), `App.cpp:639-644` 10 ms 폴링. 상용: 유니티 WebGL `emscripten_set_main_loop_timing`, NDK `ALooper_pollOnce( -1 )`. 제안: `IWindow::waitForNextFrame( deadline )` 가상 하나로. 확인: blur 뒤 메인 스레드 long task(chrome-devtools trace).
- **P-6 앱 멈춤 수명 이벤트를 렌더러만 받는다** (구조 — 구독자 하나 확인) — `_onLifecycle` 이 `DelegateSlot`(`IWindow.h:441 · 522`)이고 구독자는 `EngineRenderDriver` 뿐이라 멈춤 동안 햅틱이 꺼지지 않고 입력이 리셋되지 않으며 오디오가 계속될 수 있다. 상용: 언리얼 `ApplicationWillEnterBackgroundDelegate` 멀티캐스트, 유니티 `OnApplicationPause`. 제안: 멀티캐스트 + 순서 선언(표면 해제는 렌더러가 마지막), 입력 리셋 · 햅틱 정지 · 오디오 서스펜드를 구독자로. 확인: `simulateAppInterruption` 뒤 `Input.AnyDown` · `Haptics.Active` · `Audio.DeviceRunning` 0.
- **P-9 (보강) 웹 · Android 프리셋이 Shipping 뿐이라 플랫폼 결함을 자동으로 볼 길이 없다** (구조 — 확인) — `CMakePresets.json` 에 `Web-Shipping` · `Android-Shipping` 만 있다. P-3 ~ P-6 이 모두 이 틈에서 나왔다([PlatformTargets](PlatformTargets.md) "배포 빌드에 시나리오 실행기가 없다"). 상용: 언리얼 Development + Gauntlet, 유니티 Development Build. 제안: `Web-Release` · `Android-Release` 개발 프리셋(DevTools 정적 링크)으로 `-scenario`, 그 전에라도 가짜 창 단위 테스트. 확인: `PlatformInputTest.*` 를 `ctest -L nogpu` 에.

### 장르 키트 · 게임

- **K-4 (보강) 키트 규칙 설정을 게임이 C++ 기본값으로 만든다** (구조 — 확인, DesignBacklog "데이터 구동 구조 후보 판별" 5 번의 근거) — `RTSSettings{}`(`SkirmishMatch.cpp:64`, `FrontierSkirmishGameModeComponent.cpp:84`), `CitySettings{}` · `CreatureTownSettings{}` · `SRPGSettings{}` · `PlatformerSettings{}` · `CoasterPhysicsParams{}`. 구조체는 `PROPERTY( Min · Max )` 까지 있는데 값을 줄 데이터 길이 없다. `ThemeParkSettings` 는 리플렉션 · 범위 검사가 없고 분모 칸이 있다(`ThemePark.cpp:301-302`). 슈터 적 체력 곡선 · 복셀 광석 확률도 리터럴. 상용: Lyra `ExperienceDefinition` · `DataAsset`, 유니티 `ScriptableObject`. 제안: 키트 `initialize` 가 `AssetRef<…Settings>`(규칙 자산)를 받고 기본 생성으로는 열 수 없게. 확인: `ResourceDataSchemaTest` 규칙 자산, `ThemeParkSimulationTest.RejectsZeroDivisorSettings`.

### 에디터

- **E-1 복구가 미저장 · 플레이 확인을 건너뛰고 새 씬과 문서 패널은 자동 저장 대상이 아니다** (결함 + 구조 — 확인) — `EditorAutosaveCommands.cpp:400` `restore` 가 `loadScene` 을 바로 불러 지금 편집이 묻지 않고 사라진다. `:197` 저장한 적 없는 새 씬은 사본이 없고 대상은 활성 씬뿐이라 dirty 문서 패널(머티리얼 그래프 · 타일맵 · 시퀀서)은 잃는다. 쓰기는 UI 스레드 동기이고 끌기 중인지 보지 않는다. 상용: 언리얼은 Untitled 포함 dirty 패키지 전부, 상호작용 중이면 미룬다. 제안: 복구는 `tryOpenScene` 입구로, 대상은 "dirty 문서 목록", 새 씬은 임시 id, 상호작용 중이면 한 프레임 미룸. 확인: `autosave` 시나리오 "편집 → restore → 미저장 모달", "새 씬 편집 → `autosave.now` → 사본 1".
- **E-2 History 패널의 되돌리기가 편집 잠금과 명령 큐를 우회한다** (결함 — 확인) — `HistoryViewModel.cpp:43-69` 가 그리기 도중 `pStack->undo()` 를 바로 부른다. `edit.undo` 는 `isBlockedByEditLock`(플레이 · 베이크 중 꺼짐) + 다음 프레임 실행인데 패널은 둘 다 건너뛴다. `CommandStack::undo` 는 진행 중 상호작용도 보지 않아 기즈모 끌기 중 Ctrl+Z 가 redo 를 자른다. 상용: 언리얼 `UTransBuffer::CanUndo` 는 열린 트랜잭션이면 거절. 제안: 되돌리기 입구 하나 — History 는 `edit.undo` · `edit.redo` · `edit.jumpTo` 를 큐에 넣기만, 스택에 상호작용 구간(`DocumentEditSpan` 과 같은 구간)이 열려 있으면 거절. 확인: `lightprobebake` "베이크 중 `history.step.0` → `Editor.UndoIndex` 그대로".
- **E-6 형제 순서 한 줄을 옮기면 형제 전부가 스냅샷 · 기록 · 씬 파일 변경이 된다** (성능 + 팀 병합 — 확인) — `EditorSiblingOrder.cpp:40-52` `renumber` 가 형제마다 전체 XML 스냅샷 → `commitModify`. 루트 8000 이면 직렬화 16000 회가 한 단계에 들고 씬 파일 8000 줄이 바뀐다(`uint16`, `:22`). 상용: 유니티 2022.2 가 `m_RootOrder` 병합 충돌로 `SceneRoots` 목록 하나로 옮겼다. 제안: 순서를 "부모가 든 자식 id 목록 한 칸" 으로, 그때까지는 분수 색인으로 한 오브젝트만. 확인: `hierarchyorder` 8000 루트 `Editor.UndoCount` 1 · 프레임 시간 · 저장 diff 줄 수.
- **E-7 인스펙터 편집 기록이 위젯을 누를 때마다 선택 전체를 직렬화하고 편집 없이 풀리면 스냅샷이 남는다** (성능 + 누수 — 확인) — `InspectorEditRecorder.cpp:61-66` `beginEdit` 이 선택 전부를 `captureSnapshot`, `InspectorPropertyUndo.cpp:23-27` 은 `IsItemDeactivatedAfterEdit` 에서만 지워 클릭만 하고 나오면 세션 끝까지 남는다. 상용: 언리얼 `Modify()`, 유니티 `Undo.RecordObject` 프로퍼티 diff. 제안: 기록 단위를 (오브젝트 id, 컴포넌트 안정 키, 프로퍼티 경로, 전 값, 후 값)으로. 그 전까지 `IsItemDeactivated()` 에서 pending 을 지우고 스냅샷은 첫 실제 쓰기 직전에. 확인: `InspectorEditRecorderTest.DeactivateWithoutEditDropsPending`, 8000 선택 칸 클릭 프레임.
- **E-8 에셋 목록 · 참조 색인이 규모에 비례해 다시 돈다** (성능 — 측정 필요) — `AssetReferenceTracker.cpp:55-63` 은 생성 · 삭제가 하나라도 있으면 전체 재스캔, `EditorReferenceIndex.cpp:297-314` `refreshFile` 이 전체 `removeIf` + 정렬을 UI 스레드에서, 색인은 디스크에 없어 기동마다 전체. Open 단추 · 에셋 선택 팝업은 `collectEntries` 로 트리 전체를 동기로 걷는다. 상용: 언리얼 AssetRegistry(디스크 캐시 + 증분), 유니티 AssetDatabase. 제안: 에셋 레지스트리 하나 — 경로 intern 정수 id, 양방향 해시 표, 증분 추가 · 삭제, `Saved/Editor` 캐시, 피커 · Open · 검색은 이것만 읽는다. 확인: 에셋 1 만 · 10 만 트리의 `ReferenceIndexReady` ms, `EditorReferenceIndexTest.AddedFileRefreshesIncrementally`.
- **E-9 문서 편집 알림마다 문서 전체를 직렬화해 비교한다** (성능 — 측정 필요) — `EditorDocumentPanel.cpp:302-303` `notifyDocumentEdited` 가 매번 `captureDocumentText()` 를 기준 글과 비교(끌기 중 매 프레임), 타일맵 512² 면 MB 단위. `MeshModifierSession::isStackUnsaved()`(`:316-319`)도 같은 모양. 상용: 언리얼 dirty 는 깃발, Godot 프로퍼티 do · undo. 제안: 구간(`DocumentEditSpan`)을 쥔 동안은 dirty 만 표시해 직렬화는 시작 · 끝 두 번, 미저장 판정은 편집 개정 번호로. 확인: `-gv_editorPanelTimes` 로 `tilemaptools` 256² · 512² 칠하기.

## P2 — 개선과 작은 결함

### Core

- **C-16 (보강) 원자적 쓰기가 fsync 를 하지 않는다** (결함 · 내구성) — `FileUtil.cpp:96-121` 은 `fclose` 뒤 바로 이름을 바꾼다. 봉투 없는 장치 키 파일(`LocalDeviceKeyProvider.cpp:122-137`)이 깨지면 그 사용자는 지울 때까지 저장 · 읽기가 IOError 다. 제안: `writeAtomically` 에 내구성 등급(`Durable` | `Fast`) 필수 인자, 세이브는 한 세대 전 파일 보존. 확인: 0 바이트 · 쓰레기 키 파일로 기동해 복구.
- **C-19 IO · 로그의 실패 경로** (결함 · 작음) — IOCP 실패 뒤에도 큐가 요청을 받아 영영 끝나지 않고(`WindowsAsyncFileIOBackend.cpp:113-117`), `flushForCrash` 가 막히는 잠금을 잡으며(`AsyncLogSink.cpp:262-265`), 큐가 찬 줄이 큐의 줄보다 먼저 나간다(`:444-449`). 제안: 백엔드 실패를 큐 상태로 올려 새 요청 즉시 실패, 크래시 flush 는 `try_lock`. 확인: 가짜 백엔드 단위 테스트.
- **C-20 구독의 약한 역참조** (결함 — 추정) — `Subscription::reset`(`Subscription.h:208-223`)이 출처 포인터를 읽은 뒤 잠금을 잡아 그 사이 소멸이면 해제 뒤 사용. `LockedMulticastDelegate` 머리말("약 2 초 뒤 경고")과 코드(끝까지 기다림)도 어긋난다. 제안: 같은 스레드 규칙을 `ThreadOwners` 표지로 형식에. 확인: TSan 단위 테스트.
- **C-21 다시 열어 확인하지 않은 후보** (미확인) — `SlotHandleTable` 세대 31 비트 + LIFO 재사용, `DynamicBitset` 범위 밖 쓰기 뒤 계속, Base64 `=` 개수 무제한, `wrapAngle` 무한대, `SpatialHashGrid2D` 셀 크기 조용한 대체, `readVarUint` 넘친 비트, `PositionWelder` NaN 캐스트, Windows `fromSearchPath` 가 작업 폴더를 PATH 보다 먼저, POSIX `Process` 소멸자 좀비, Windows 크래시 필터의 로더 락 · 힙 사용. 고치기 전에 줄을 다시 읽는다.

### 네트워크

- **N-11 손실 · RTT 에 반응하는 송신률 조절과 재전송 백오프가 없다** (구조) — `NetEndpoint.cpp:534-544` 토큰 버킷 고정, `NetConnection.cpp:376-379` 재전송 간격이 횟수와 무관. 상용: ENet RTT 분산 스로틀, 유니티 Transport. 제안: 연결별 상한을 손실률 · RTT 로, 재전송은 SRTT + 4·RTTVAR. 확인: `NetEmulation` 손실 20 % 의 `_resentMessageCount`.
- **N-13 RemoteCall 에 연결별 호출률 제한이 없고 인자가 글로 오간다** (구조) — `RemoteCall.cpp:281-343`. 상용: 언리얼 RPC DoS Detection + NetFieldExport 인덱스. 제안: 함수 메타데이터에 초당 상한(기본값 Config), 접속 때 함수표 해시로 인덱스 합의. 확인: `TestRemoteCall` 상한 초과.
- **N-14 주소 형식과 소켓이 IPv4 전용이다** (구조) — `NetTypes.h:158-161`, `PlatformSocketUtil.cpp:75 · 275`. 제안: 요구 여부를 [PlatformTargets](PlatformTargets.md) 에 한 줄로 정하고 `NetAddress` 를 주소군 + 16 바이트로. 확인: `TestNetTransport` `::1` 루프백.

### 온라인

- **O-13 OIDC 로그인 nonce 를 클라이언트가 토큰과 함께 보낸다** (결함 · 인증 약화) — `OidcLoginProvider.cpp:33-36 · 142`. 토큰을 얻은 쪽이 `exp` 까지 재사용한다. 상용: 서버가 nonce 발급 · 1 회 소비. 제안: 로그인 전 챌린지 메서드 + 휘발 저장소 TTL. 확인: 같은 `토큰|nonce` 두 번째 거절.
- **O-14 로그인 실패 횟수가 동시 시도에서 한 번만 세어진다** (결함) — `LoginStoreLogic.cpp:1341-1344` 가 Conflict 를 버리고 주소당 버킷은 프로세스 메모리(`LoginService.h:226`). 제안: 휘발 저장소 INCR + TTL. 확인: 워커 4 SQL 구성 틀린 비밀번호 20 건 동시.
- **O-15 FCM 404 한 번에 기기 등록을 지우고 토큰이 계정 사이에서 유일하지 않다** (결함) — `FCMPushProvider.cpp:127`, `PushNotificationDispatcher.cpp:165-169`. 상용: FCM v1 `errorCode == "UNREGISTERED"`. 제안: 응답 오류 코드 판정 + 토큰 → 계정 역색인. 확인: `TestPushProviders` 두 케이스.
- **O-16 GM: 기간 정지에 상한이 없고 대상 계정을 확인하지 않는다** (결함) — `AdminStoreLogic.cpp:306-314`. Operator 의 `INT64_MAX` 정지는 영구 정지와 같고 오타 계정에 재화가 Ok 로 들어간다. 제안: 등급별 최대 기간(Config) + 계정 존재 확인. 확인: `TestAdminService` 두 케이스.
- **O-17 주소 단위 제한이 프록시 뒤 배치를 전제하지 않는다** (구조 — 추정) — `OnlineServiceHost.cpp:25 · 540-545` 주소당 동시 연결 64. 상용: PROXY protocol v2. 제안: "신뢰 프록시" 설정일 때만 PROXY 머리. 확인: 한 주소 연결 100.
- **O-18 다시 읽지 않은 후보** (미확인) — 매칭 취소가 로컬에서 끝남(`MatchQueueService.cpp:669`), 좌석 예약이 프로세스 로컬(`ServerRegistryReader.cpp:127`), 공지 128 · 이벤트 256 뒤 조용한 잘림, 점검 목록 fail-open(`ServerDirectoryService.cpp:204-205`), 차단 상대의 글이 History 로 읽힘(`ChatService.cpp:580-581`), 길드 추방이 처리 서버의 채팅 멤버십만 고침(`GuildService.cpp:935`), "모두 받기" 가 최근 50 통만(`MailboxStoreLogic.cpp:362`), APNs 403 재서명 최소 간격 없음, 정책 값(도배 · 거래 시한 · 매칭 유예)이 C++ 상수, `TokenBucketMap.h` 상한에서 전체 훑기, `SQLMigrationRunner.cpp:282-289` 가 더 새 DB 면 기동 거절(순차 배포 막힘). 길드 창고 목록은 128 개에서 자른다(쪽 읽기 없음 — DesignBacklog "길드 창고").

### RHI

- **R-7 DX12 가 아무도 읽지 않는 힙 CBV 를 프레임마다 다시 쓴다** (성능) — 상수 버퍼는 루트 CBV 인데 등록 때 힙 CBV 둘(`D3D12RHIResourceFactory.cpp:1310-1311`), `beginFrame` 마다 `refreshConstantBufferViews`. 제안: 지운다. 확인: 네 백엔드 골든 그대로.
- **R-8 Vulkan 스왑체인: 선호 형식이 없으면 실제와 다른 형식을 보고한다** (결함 · 드문 표면) — `VulkanRHISwapChain.cpp:292-312 · 445-448`, 재생성 때 렌더 패스 · `oldSwapchain` 없음. 제안: `VkFormat → RHIFormat` 역매핑 한 함수, 없으면 생성 실패. 확인: 형식 고르기 순수 함수 단위 테스트.
- **R-9 텍스처 "셰이더 읽기" 전이가 픽셀 단계만 덮는다** (결함 · 잠재 — 호출처 없음) — DX12 `PIXEL_SHADER_RESOURCE` 만(`D3D12RHICommandContext.cpp:298`), Vulkan `FRAGMENT_SHADER_BIT` 만, 검출용 syncval · GBV 스위치 없음. 상용: 언리얼 `SRVGraphics/SRVCompute`. 제안: 모든 단계 읽기로 정의, syncval · GBV 스위치는 `Config/Dev`. 확인: "RT 를 컴퓨트 SRV 로" GPU 테스트.
- **R-10 텍스처 상태와 첨부 뷰가 자원 단위다** (구조) — `RHITextureTransition` 에 서브리소스가 없고 RTV · DSV 는 `MipSlice = 0` 고정. 상용: 언리얼 `FRHITransitionInfo`, Godot RD 슬라이스 뷰. 제안: `RHITrackedState` 를 "전체 + 필요할 때 서브리소스 배열" 로. 확인: `RHITrackedStateTest` 서브리소스.
- **R-11 DX12 오프스크린 RTV 128 · DSV 64 가 C++ 상한이고 넘으면 뷰 없는 텍스처가 유효 핸들로 나간다** (구조) — `D3D12RHIDevice.h:342-343`. 큐브 그림자 10 + CSM 이면 닿는다. 제안: 페이지 목록 + 뷰 실패 시 생성 실패. 확인: 큐브 RT 30 개 렌더.
- **R-12 "상수 버퍼는 프레임에 한 번만 쓴다" 가 글 규칙이다** (구조) — `IRHICommandList.h:158-165`. 두 번 쓰면 앞 드로우도 마지막 값. 상용: 언리얼 `RHIUpdateUniformBuffer` 는 갱신마다 새 할당. 제안: R-4 구간 할당과 함께 쓰기가 구간을 돌려준다. 그 전엔 두 번째 쓰기를 진단 오류로. 확인: 가짜 장치 단위 테스트.
- **R-13 Vulkan 파이프라인 캐시 파일이 장치 구분 없이 하나이고 종료 때만 저장한다** (성능) — `VulkanRHIDevice.cpp:754-757 · 1798-1831`. 제안: 파일 이름에 vendor · device · driverVersion, 주기 저장. 확인: 하드웨어 → software → 하드웨어 컴파일 시간 합.
- **R-14 DX11 이 기능 수준 10_0 으로 내려가도 진행한다** (결함) — `D3D11RHIDevice.cpp:512-516 · 550`, Debug 는 SDK 레이어 유무를 보지 않는다. 제안: 10_0 을 빼고 요구 판 표와 대조. 확인: WARP 초기화 로그.
- **R-15 인덱스가 항상 32 비트이고 Packed 정점 색이 float4 다** (성능 · 메모리) — `MeshRenderData.cpp:60-62`, CPU 사본(60 B/정점)을 놓는 길 없음. 제안: 굽는 단계에서 인덱스 폭을 정해 `.mesh` 에. 확인: `MeshIndicesTest` 폭 선택.
- **R-16 GL 은 드로우마다 정점 속성을 전부 다시 지정한다** (성능 — 측정 필요) — `OpenGLRHICommandContext.cpp:672-732`. 제안: (PSO 정점 형식, 슬롯 버퍼) 열쇠 VAO 캐시 — 3 순위 백엔드라 측정 뒤. 확인: Linux GL apitrace 호출 수.
- **R-17 작은 동시성 틈 둘** (결함 — 추정) — WebGPU `_bDeviceLost` 비트필드를 `AllowSpontaneous` 콜백이 쓴다(`WebGPURHIDevice.h:494-501`), `broadcastInternal` 이 잠금을 놓은 뒤 통보(`RHIRenderResource.cpp:57-75`). 제안: 분실 상태는 R-2 의 한 입구로, 통보 전 에셋 로드 비우기를 단언. 확인: TSan "로드 중 백엔드 교체".

### 렌더러

- **RN-10 갱신 주기를 낮춘 추가 뷰에서 물체의 지난 행렬과 뷰의 지난 뷰-투영이 다른 프레임이다** (결함 — 증상 추정) — `GPUScene.cpp:144 · 347-351`, `FrameGraphExecutor.cpp:724` `bViewCut` 은 죽은 조건. 상용: 언리얼 캡처 뷰는 시간 누적 끔. 제안: 매 프레임 그리지 않는 뷰는 TAA · 모션 블러를 형식으로 끈다. 확인: 10 Hz CCTV 앞 큐브 골든.
- **RN-11 SSAO 를 최종 색 전체에 곱한다** (결함 · 화질) — `post_bloom.hlsli:147-149 · 157-159`. 상용: AO 는 간접광에만. 제안: AO 를 Shading 입력 계약에 넣어 환경광 · 프로브 항에만. 확인: 유리 + 발광 큐브에서 AO 켬/끔 차이 0.
- **RN-12 트랜지언트 별칭 계획의 수명 합치기가 한 단계만 전파된다** (결함 · 잠재 — 게임 파이프라인 XML 에서만) — `FramePassExecutor.cpp:787-820`. 제안: union-find. 확인: `RenderGraphAliasPlanTest` 우회 셋 사슬.
- **RN-13 추가 뷰 한도와 뷰당 고정 비용** (구조 · 성능) — `RenderViewCollector.cpp:184-187` 이 등록 순서로 8 개에서 로그 없이 끊는다. 제안: 보이는 것 먼저 → 늦은 정도로 고른 뒤 자르고 넘친 수를 감시 값으로. 확인: `RenderViewSchedulerTest` 카메라 12.
- **RN-14 글리프 아틀라스 페이지 비우기가 CJK 규모에서 흔들릴 수 있다** (구조 — 측정 필요) — `GlyphAtlas.h:58-59`(1024² × 8 상수), `GlyphCache.cpp:73-87`. 상용: Slate 는 아틀라스를 늘린다. 제안: 페이지 수 · 크기 Config, 비우기 전 최근 글리프 옮김. 확인: 한글 3000 자 로그의 `Text.GlyphsRasterized`.
- **RN-15 입자 정렬이 방출기 단위다** (구조 — 주석에 적힌 한도) — `GPUSceneBuilder.cpp:1254`. 상용: 나이아가라 · VFX Graph 출력별 정렬 모드. 제안: 방출기 정의에 정렬 모드, GPU 시뮬은 `instance_sort.hlsl`. 확인: 알파 연기 180° 골든.
- **RN-16 렌더 정책 값이 C++ · HLSL 상수에 흩어져 있다** (구조) — 그림자 바이어스 · 캐스케이드 람다 · TAA 기록 비율 · 그리드 16x9x24 · 광원 1024 · 추가 뷰 8 · PSO 예산 2000 µs. `Config/Engine` 에 렌더 설정이 없다. 상용: 언리얼 `r.*` + 스케일러빌리티, 유니티 SRP 에셋. 제안: `Config/Engine/RenderSettings.json`(품질 단계별), 셰이더 값은 쿠킹 define. 확인: 로드 검증 단위 테스트.

### 에셋 · 직렬화 · 오브젝트

- **A-19 비동기 로딩의 취소 · 우선순위가 요청자 단위가 아니고 프리로드가 같은 바이트를 두 번 읽는다** (구조 · 성능) — `AssetStreamingQueue.cpp:388-457` `cancelRequest( 경로 )` 가 모든 대기자를 실패로, `PreloadSet.cpp:58-64` 는 읽고 버린 뒤 다시 읽는다. 상용: 언리얼 `FStreamableHandle`. 제안: 요청이 요청자 토큰을 돌려준다. 확인: `AssetStreamingQueueTest.CancelLeavesOtherRequesters`.
- **A-20 (보강) 공유 캐시 키가 요청 글 그대로라 같은 에셋이 두 벌 오를 수 있다** (구조) — `SharedAssetTable.h:145`. 제안: GUID 2 단계에서 키를 풀린 정체성으로. 확인: `SharedAssetCacheTest.RedirectedAndCanonicalIdsShareOneEntry`.
- **A-21 수명 불변식이 주석에만 있는 세 자리** (결함 · 잠재) — `GameObjectStore::clear()` 중 해체 콜백의 생성(`:524-571`), 삭제 대기 오브젝트의 즉시 `removeComponent` 뒤 틱 파도 포인터(`GameObject.cpp:763-806`), `PrimitiveRegistry` 대기자 순회 중 제거(`:152-169`). 제안: 순회 중 구조 변경은 `StructuralChangeBuffer` 로만 받게 단언. 확인: ASan 단위 테스트 셋.
- **A-22 타입 등록의 조용한 덮기와 재잠금** (결함 · 잠재) — `registerTypeAlias` 가 살아 있는 이름을 덮고(`TypeRegistry.cpp:688-705`), 32 비트 타입 이름 해시 충돌 검사가 없으며(`:188 · 203`), `getDerivedTypes` 가 공유 잠금을 쥔 채 같은 `shared_mutex` 를 다시 잡는다(교착은 추정). 제안: 충돌 · 덮기는 오류, 잠금 안에서는 포인터만. 확인: 단위 테스트 셋.
- **A-23 팩 · 쿠킹 설정의 빈 끝** (구조) — DLC 소유권 검사는 `_dlcAppID` 를 쿠커가 적지 않고 검증자가 없으면 통과(`PackManager.cpp:395-403`), `PackConfig.json` 이 게임 이름 · 키트 카탈로그 칸을 엔진 설정에, `ZstdDictionaryCompressor` 호출 0 · 기본 zlib 9, 호스트 설정에 플랫폼 덮어쓰기 층 없음(`ConfigManager.cpp:73-78`). 제안: 게임별 쿠킹 규칙 층, 안 쓰는 형식은 지운다. 확인: 쿠킹 단위 테스트.
- **A-24 그 밖의 작은 것** — `FloatCurve` 키 1 개 + NaN 시간 범위 밖 읽기(`FloatCurve.cpp:30-42`), JSON 중복 키 · XML 중복 속성을 엄격 읽기가 못 잡음, `JSONDocument` 깊이 한도 없음(추정), `TagIndex` 첫 질의 깊은 복사, `PrefabInstanceRegistry` 매 프레임 전체 훑기, `NetObjectTable` 씬 번호가 이름 해시 31 비트, 씬 비동기 로드가 다른 요청에도 끝까지 짓고 버림(`SceneManager.cpp:249-263`), 리플렉션 묶음 TU 가 헤더 하나에 전체 재파싱(NEXT 5-4 보강).

### 물리 · 애니메이션 · 내비 · 오디오

- **S-9 경로 요청 핸들이 소유 타입이 아니고 프레임당 예산이 없다** (구조) — `PathRequestQueue.h:94-131`, `SceneNavigation.cpp:692 · 726`. 상용: 유니티 `pathfindingIterationsPerFrame`, Detour `dtPathQueue`. 제안: 이동 전용 `PathRequest`(소멸자 취소) + 프레임 상한. 확인: `PathRequestQueueTest.DroppedTicketFreesSlot`.
- **S-10 보이스 풀이 차면 우선순위와 상관없이 새 소리를 버리고 경고를 오디오 스레드에서 찍는다** (결함 · 부하 때) — `AudioVoicePool.cpp:24-36`, `AudioEngine.cpp:712-717`, 가상 보이스도 256 칸을 차지. 상용: 언리얼 · 유니티 낮은 우선순위 뺏기. 제안: 가상 보이스는 풀 밖 기록, 고갈 때 (우선순위, 들림) 낮은 칸을 한 블록 페이드로 뺏는다. 확인: `AudioVoiceTest.HighPriorityStealsWhenPoolIsFull`.
- **S-11 전역 잔해 한도를 먼저 틱한 오브젝트가 치르고 한도는 콘솔 변수다** (구조) — `FractureComponentBase.cpp:1435-1479`. 상용: Chaos 조각별 제거 타이머 · 거리. 제안: 잔해 바디를 `ScenePhysics` 풀 하나에 (나이, 부피, 거리) 키로. 확인: `DestructionStateTest.GlobalBudgetEvictsOldestAcrossObjects`.
- **S-12 대화 그래프 로드 검증이 링크 끝점 · 핀을 보지 않는다** (구조) — Start 정확히 하나 · 폴백 제거는 들어갔다(572d8617dd). 남은 것: `DialogueGraphAsset.cpp:102-155` 의 링크 끝점 · 핀 · 기다림 없는 순환 검사. 상용: Yarn Spinner 는 끊긴 링크를 컴파일 오류로. 확인: `DialogueGraphTest.RejectsDanglingLink`.
- **S-14 컴파일한 액션 · 조건식이 등록 엔트리를 원시 포인터로 든다** (구조 — 추정) — `ActionRegistry.cpp:181-189`, "핫 리로드 뒤 다시 컴파일" 이 주석 규칙뿐. 제안: 등록표 세대를 `invoke` 가 대조. 확인: LiveReload 뒤 대화 Action 노드(ASan).

### 플랫폼 · 입력 · 호스트

- **P-10 창이 배경일 때도 패드 입력이 게임에 들어간다** (구조 · 정책 누락) — `InputManager.cpp:367-378`(XInput 전역). 상용: SDL `JOYSTICK_ALLOW_BACKGROUND_EVENTS` 기본 0. 제안: `Config/Engine` 에 `backgroundGamepadInput`(필수 값). 확인: `InputManagerTest.BackgroundPadFollowsPolicy`.
- **P-11 접근성 단축키 끄기 API 의 호출자가 없다** (죽은 코드) — 사용자가 켠 기능을 끄던 결함은 고쳤다(5781c0994b). 남은 것: `InputManagerWin32.cpp:558-583` 을 포커스 획득 · 유실에 잇는다. 확인: `InputAccessibilityTest.KeepsEnabledStickyKeys`.
- **P-13 스플래시 창이 상태를 바꿀 때만 메시지를 처리한다** (결함 · UX — 추정) — `Win32SplashWindow.cpp:157-183`, 5 초를 넘는 단계면 "응답 없음". 상용: 언리얼 `FWindowsSplash` 전용 스레드. 제안: `ScopedProgressReport` 간격 콜백이 펌프. 확인: `updateStatus` 사이 최대 간격.

### GameFramework Runtime

- **G-8 어빌리티 · 상점 카탈로그가 "조용히 무효가 되는" 값을 받는다** (구조) — 부여 태그 없는 쿨다운 이펙트는 연타가 되고(`Ability.cpp:110-115`, `AbilityCatalog.cpp:451-452`), 주기 하한은 `Min = 0.0` 뿐, 상점은 사는 값 < 파는 값도 받는다(`Shop.cpp:392-422`). 상용: GameplayEffect `IsDataValid`. 제안: 카탈로그 읽기 끝 교차 검증을 쿠커도 돈다. 확인: `AbilityCatalogTest.CooldownEffectWithoutGrantedTagIsRejected`.
- **G-10 관통 탄이 "방금 맞힌 하나" 만 기억한다** (결함 — 추정) — `ProjectileSimulation.cpp:117-139`. 제안: `ProjectileState` 에 맞힌 대상 고정 배열. 확인: `ProjectileSimulationTest.PierceHitsOverlappingTargetsOnceEach`.
- **G-11 카메라 충돌 탐침 레이어가 C++ 이름 둘이고 없으면 충돌이 조용히 꺼진다** (구조) — `CameraCollisionProbe.cpp:72-83`. 상용: SpringArm `ProbeChannel`. 제안: 카메라 프리셋에 레이어 칸, 비면 오류. 확인: `CameraCollisionProbeTest.MissingLayersIsAnError`.
- **G-12 키트 에디터 확장의 미저장 보호가 패널 UI 에만 있다** (결함) — `MeshModifierSession::setTarget`(`:287-289`)이 스택 · 기록을 무조건 지우고, `CharacterEditorModel.h:118` `isDirty()` 에 몸 데이터 · 외형이 없다. 제안: 미저장이면 `setTarget` 이 `expected` 오류, 패널은 "이 문서가 여는 파일 목록" 하나로 dirty · 저장. 확인: `MeshModifierSessionTest.SetTargetRefusesWhenUnsaved`.
- **G-13 ScreenTransitionManager: 전환 중 새 전환이 앞 전환의 동작을 지운다** (결함 · 잠재 — 호출처 없음) — `ScreenTransitionManager.cpp:91-98`. 제안: 진행 중이면 거절하거나 대기열 하나. 확인: `ScreenTransitionTest.SecondTransitionDuringFadeOutDoesNotDropFirstAction`.

### 키트 · 게임

- **K-5 누적 시각을 float32 로 든다** (결함 · 잠재) — `CitySimulation.cpp:510` `_time += 0.25f` 는 2^22 초(NileCity 8 배속 146 시간)에서 멈춰 서비스 만료가 영영 참, `RTSWorld.cpp:500` 은 2^20 초. 같은 모양의 `GameTimer` 는 float64 로 고쳤다(C-12). 제안: 키트 공용 `SimTick`(int64 걸음 수). 확인: `CitySimulationTest.ServiceExpiresAfterLongRun`.
- **K-6 StarSkirmish 8 배속이 프레임률에 묶이고 AI 시계가 월드와 어긋난다** (결함) — `FixedStepTimer.h:48` `_maxFrameTime` 이 `deltaTime * _timeScale` 을 자르고(30 fps 에서), AI · 타이머는 자르지 않은 델타(`SkirmishMatch.cpp:237-241`). 제안: 배속을 걸음 수로(`update( dt, speed )`). 확인: `RTSWorldTest.SpeedScaleKeepsStepRatio`.
- **K-7 맵 배치 · 뷰 표가 C++ 에 있고 모델 경로를 문자열로 조립한다** (구조 — DesignBacklog "구조와 시스템" 의 `-cooked-content` 줄 · NEXT 7 절 "경로를 조립해 읽는 에셋" 의 실제 자리) — `NileBuildingComponent.cpp:25-27 · 82-116 · 131-134`, `SkirmishMatch.cpp:154-175`. 쿠킹 도달 계산이 조립 경로를 놓칠 수 있다. 제안: 키트에 "규칙 id → 뷰" 카탈로그(`AssetRef<Mesh>`). 확인: `-cooked-content` 로 두 게임 `slice`.
- **K-8 VoxelCraft 광석 배치가 지형 씨앗을 쓰지 않는다** (결함) — `VoxelGameModeComponent.cpp:33-40 · 236` `hashCoord` 에 씨앗이 없다. 제안: 꾸밈을 `VoxelTerrainSettings` 데이터로. 확인: `VoxelTerrainTest.DecorationsFollowSeed`.
- **K-9 Velmora 확장 팩이 정해진 훅 밖에서 올라간다** (구조) — `VelmoraGame.cpp:67-71` 이 `applyForGame` 을 `onInitialize` 에서 부른다(넷 와이어 해시 0, 세이브 머리에 구성 없음). 제안: 준비 단계 토큰으로만 부를 수 있게. 확인: `GameInstanceTest.ContentIsPreparedBeforeDeclaredData`.
- **K-10 배속 · 일시정지와 "규칙 → 뷰" 동기화가 게임마다 따로 있다** (구조 · 중복) — 배속 코드가 Nile · Skirmish 에 같고 FrontierTown 은 셋째 방식(닫힌 K-2 의 원인), 뷰 동기화는 다섯 벌. 상용: Mass Representation 프로세서 하나. 제안: Runtime 에 `GameSpeedControl` · `SimulationViewSync<TID>`. 확인: `control` · `pause` · `viewsync` 시나리오.
- **K-11 오브젝트 풀이 한 곳에서만 쓰인다** (성능 — 측정 필요) — `PrefabPool` 은 슈터 탄착뿐, 아레나 탄 · 슈터 적은 매번 스폰. 상용: Lyra · Boss Room 풀. 제안: `spawnPrefab` 이 풀 정책(데이터)을 받는다. 확인: Tracy `GameModeFlush`.
- **K-12 복셀 월드가 고정 크기 전량 상주이고 메셔가 면마다 정점 6 개다** (성능 · 구조) — `VoxelGameModeComponent.h:51-52`, `VoxelMesher.cpp:119-123 · 160-188`. 제안: "청크 좌표 → 청크" 사전 + 생성 · 저장 훅, 그리디 메싱. 확인: Tracy `VoxelMesher::fillChunkMesh`.
- **K-13 LanternVale 서버가 변경과 무관하게 전원을 저장하고 매 틱 전 엔티티를 다시 싣는다** (성능) — `ValeNetLink.cpp:343-389`. 상용: Push Model · Iris. 제안: 프로필 저장 세대, 바뀔 때만 `setEntity`. 확인: `net-pair mmo` 자동 저장 쓰기 수.
- **K-14 (보강) 장르 키트 17 개에 쓰는 게임 · 시연이 없다** (구조 · 테스트) — BattleRoyale · Fighting · MechArena · Metroidvania · CardGame · KartRacing · PartyArena · Rhythm · AsymmetricHorror · CoopScavenger · GhostHunt · SurvivalHorror · ClassicJRPG · MonsterCollector · MonsterHuntRPG · RestaurantSim · SideScrollConquest 가 `Source/Games` 에서 include 되지 않는다. 게임 폴더의 규칙 클래스(`SkirmishMatch` · `NileCityPlanner`)는 단위 테스트가 없다. 제안: Empty `KitDemo` 키트마다 한 씬 + `slice` 시나리오를 키트 완료 조건으로. 확인: `game.empty.kit.<이름>`.

### 에디터

- **E-13 되돌리기 한도가 개수 상수이고 일부 문서 패널은 되돌리기가 없다** (구조 — NEXT "표 페이지 Ctrl+Z" 보강) — `CommandStack.cpp:182` 128 · `EditSnapshotHistory.h` 64, 바이트 한도 없음. AudioEvents · InputMap · DataTable 은 undo 0 건. 상용: 언리얼 Undo Buffer Size(MB). 제안: 한도를 `Config/Editor`(개수 + 바이트)로, 세 패널은 문서 스택을 받는다. 확인: `CommandStackTest.EvictsByByteBudget`.
- **E-15 외부 프로세스: 패키징 출력은 끝나야 보이고 넷플레이 자식은 에디터가 죽으면 고아가 된다** (구조) — `PackagingModel.cpp:150-156`(오류 판정이 `"error"` 부분 문자열), `WindowsProcess.cpp:251`(`KILL_ON_JOB_CLOSE` 없음). 제안: `takeLines` + `ProcessOptions` "부모와 함께 종료"(Windows 잡, Linux `PR_SET_PDEATHSIG`). 확인: `packaging` 시나리오 "Running 동안 줄 수 > 0".
- **E-16 (보강) MVVM 강제 범위와 SelfTest 가 덮지 않는 길** (구조 — DesignBacklog "에디터 UI 스레드 분리 검토" 1 번 보강) — `EditorLayers.toml` `[panelViewBan]` 이 문서 패널 View 를 막지 않아 `MaterialPanel.cpp:362-386` · `SequencerPanel.cpp` · `TileMapPanel.cpp:352` 가 문서 모델을 직접 고치고, `EditorUndoCoverage.cpp` 는 레지스트리 거래 명령만 돈다. 제안: `notifyDocumentEdited` 를 Model 창구로 내리고 `editor/undocoverage` 에 문서 패널 전수(열기 → 편집 → undo → 글 해시). 확인: View 파일의 `notifyDocumentEdited` 호출 0.

## 다음 할 일

한 줄이 한 묶음(커밋 서넛)이다. "확인" 의 테스트를 먼저 써서 지는 것을 본 뒤 고친다. 성능 묶음은 측정이 먼저다. 온라인 · 서버 · 보안 묶음은 `security-and-hardening`, 외부 API 묶음은 context7 로 그 판의 문서를 먼저 대조한다. 번호는 NEXT 가 가리키므로 끝난 줄은 지우고 번호는 다시 매기지 않는다.

1. **에디터 되돌리기 입구** — History 우회 E-2, 편집 알림 직렬화 E-9, 바이트 한도 E-13. `editor/undocoverage` 에 문서 패널 전수 단계(E-16)를 먼저 더해 지는 것을 본다.
2. **복구 입구** — 복구 입구 통일과 새 씬 · 문서 패널 자동 저장: E-1.
5. **읽은 것을 커밋 조건으로** — 일 안에서 읽은 것을 커밋 조건으로 붙이는 연결 포장과 Admin · Mailbox · Guild 저장 로직 점검(경제 키트는 영수증 레코드 하나로 묶였다 — O-1).
14. **웹 · Android 개발 프리셋과 가짜 창 테스트** — P-9, 그 위에서 P-3 · P-4 · P-5 · P-6.
15. **세이브 판 올림** — 사용자 설정 주인별 판 A-5.
16. **영속 정체성** — 컴포넌트 · 중첩 노드 고정 지역 id: A-2(에셋 GUID 2 단계와 같은 줄기, A-20).
18. **팩 규모** — 스트림 쓰기 · 분할 · 정렬 배열 색인 · 블록 읽기 · 색인 서명 A-9 · A-10 · A-12, 증분 쿠킹 해시 캐시 A-11. 30 만 항목 합성 팩으로 먼저 잰다.
20. **할당 · 컨테이너 측정** — 출하 할당기 C-6, 범위 삽입 · 링 버퍼 `deque` C-9. 벤치 숫자가 나온 것만 고친다.
21. **Core 작은 결함** — 삼각 분할 막힘 → 실패 계약 C-2.
22. **전역 변수와 결정성** — 문자열 `gv_` 접근자 · 렌더 스레드 스냅샷 C-7, 결정성 TU C-14.
25. **서버 관측** — UDP 지표 등록 N-7.
28. **매칭 영속 기록** — 명단 캐시 기록 O-6, 죽은 서버의 티켓 풀기 O-7.
30. **온라인 부하** — 해시 연산 풀 · 저장소 대기 상한 O-3, 부하 봇 원장 동작 · 열린 루프 O-12.
31. **RHI 자원 모델** — 정적 버퍼 장치 로컬 R-3, 버퍼 구간 할당 R-4, Alt+Enter R-1.
32. **텍스처 스트리밍** — bindless 번호 고정 · 밉 범위 대기 줄 · 예산 R-6(A-12 위).
33. **렌더러 결함** — RN-1 · RN-2 · RN-9 · RN-8.
34. **렌더러 규모** — RN-3 · RN-4 · RN-5 · RN-6 · RN-7. `load/*` 에 광원 4000 · 오브젝트 20 만 장면을 먼저 더한다.
35. **에셋 런타임** — 다시 읽기 Detach A-7, 이진 번역 표 A-16, ReflectionParser 스탬프 A-8.
36. **씬 스트리밍** — A-13.
37. **물리** — 임시 할당자 설정 · 폴백 S-2, 캐릭터 속 바디 S-7, 캐릭터 일괄 이동 S-8.
38. **내비 · 창** — 64 비트 폴리 참조 S-6, 리사이즈 미루기 · 모달 루프 틱 P-2.
40. **키트 규칙 자산** — K-4.
41. **에디터 규모** — 형제 순서 목록 E-6, 인스펙터 프로퍼티 단위 기록 E-7, 에셋 레지스트리 E-8. 8000 큐브 측정(NEXT 3 절)과 같이.
42. **Core 내구성 · 한도** — C-16 · C-19 · C-20 · C-21.
43. **네트워크** — N-11 · N-13 · N-14.
44. **온라인** — O-13 ~ O-18.
45. **RHI** — R-7 ~ R-17.
46. **렌더러** — RN-10 ~ RN-16.
47. **에셋 · 오브젝트** — A-19 ~ A-24.
48. **시뮬레이션** — S-9 · S-10 · S-11 · S-12 · S-14.
49. **플랫폼 · 호스트** — P-10 · P-11 · P-13.
50. **GameFramework · 키트** — G-8 · G-10 ~ G-13, K-5 ~ K-14.
51. **에디터** — E-15 · E-16.

## 깊이 못 본 곳

다음 감사가 이어서 볼 자리다(여기의 결함은 이 문서에 없다).

- **RHI:** 백엔드별 PSO 생성 스레드 안전과 전송 티켓 계약, WebGPU · GL CommandContext, Vulkan 장치 고르기, EGL · GLX 문맥.
- **렌더러:** `FramePassExecutorPasses.cpp` 대부분, `RenderGraph::executeParallel` 배리어 묶음, 모프 · 정점 애니메이션 풀, 실시간 프로브, forward 계열 파이프라인 XML.
- **직렬화 · 리플렉션:** `SerializerUtil.cpp` · `XMLSerializer.cpp` · `DataInheritance.cpp` · `ResourceComposer.cpp`, ReflectionParser `CodeGenerator.cpp`.
- **입력 · 창:** `InputMap` 평가와 고정 틱 사이 엣지 유실, `WaylandWindow.cpp`, UI 접근성 브리지.
- **온라인:** `OnlineCore/Server/HTTP`(HPACK · HTTP/2 동적 표 · 프레임 길이 상한), Apple · Google 영수증 검증기 본문, RESP 재연결 뒤 응답 짝.
- **게임플레이:** `ScheduleSystem.cpp`, BehaviorTree · Blackboard, Appearance, 장르 키트 14 개 내부 로직.
- **에디터:** 임포터(Model · Texture · Vrm), Animation 패널, 프리팹 오버라이드 적용, 도킹 · 레이아웃.
- **실행으로만 확인되는 것:** 성능 항목 전부(측정 전), 외부 API 동작에 기댄 항목(R-1 · P-5 · P-13 · O-15), 추정 항목.

## 닫힌 항목

코드 주석이 `감사 <번호>` 로 근거를 대는 항목이다. 판단 한 줄과 고친 커밋만 남긴다(전문은 `e480b07498` 판 이 문서와 커밋 메시지).

- **P0:** P0-1 · P0-2 문서 되돌리기를 상호작용 구간 하나로(b8473b34f6 · 977e4d28a0) · P0-4 옮긴 문서 · 씬의 경로가 따라간다(45f5a3f2d6 · 8b12e105ed) · P0-5 세션 상태 구간을 못 읽으면 로드 전체 실패(ef6e2a3b74) · P0-6 복원은 검증 → 교체, 저장 막기는 범위형 `SaveLock`(801b62e7cc) · P0-7 분개 보존 정리가 영수증 중복 방지 기록을 지우지 않는다(4493a0df85) · P0-8 끊긴 연결 번호를 같은 갱신에서 재사용하지 않는다(421856cb5a) · P0-9 Android 자체 종료는 `ANativeActivity_finish`(afa347088b) · P0-10 Android 볼륨 키 소비 판정(27c738c316) · P0-11 오디오 백엔드 없는 구성은 CMake 에서 멈추고 Android AAudio · 웹 AudioWorklet 백엔드를 둔다(ec297c8197 · 938e1a9483 · 9a3060de71) · P0-13 오디오 장치 유실 계약(3e883528e7) · P0-14 타일맵 충돌이 오너 이동을 따른다(fd769f5585) · P0-15 Win32 Alt · F10 창 메뉴 모드 차단(996394ca30) · P0-16 · P0-17 POSIX exec 실패 · 줄 쪼개기(821c55c889).
- **Core:** C-1 쿼터니언 → 오일러 피치 ±90° 특이점(0293095f01) · C-3 합류로 잠드는 워커도 유휴 마스크에(86fb9b0e61) · C-4 큐가 차면 잠든 풀을 깨운다(e082c6f7e1) · C-5 적응형 `SpinLock`(b261acc635) · C-8 현지화 키 해시 64 비트 + 충돌 오류(65f98c29d6) · C-10 `appendFormat` 잘림을 실패로(113e42d23a) · C-11 백오프 지터(456fc3a9ea) · C-12 `getTotalTime` float64(df6eeafff0) · C-13 POSIX 크래시 처리기가 앞 처리기를 잇는다(ba3cf4e860) · C-17 이름 풀이 차면 멈춘다(2f20abeafa) · C-18 경로 포함 판정의 `..`(db54280e9d).
- **네트워크:** N-1 접속 증명 태그 PRF(ac944d4075) · N-2 순서 번호를 종류마다(4bb15d623f) · N-3 신뢰 창이 차면 연결을 닫는다(0c6cecfe78) · N-4 서버 틱 추정 float64(da9b332d88) · N-5 파괴 스냅숏 요청 중복 제거(d04ddcd157) · N-6 텔레메트리 업로드 전용 스레드(100835c275) · N-8 기한 큐 · 잠금 밖 AEAD · 슬롯 지연 할당(9283b011a2 · 3fe02c6232 · 290b0d3bea · 66f70c136f) · N-10 TLS 핸드셰이크 시한(6e1dd25570) · N-12 멱등 기억 바이트 예산 · 대기자 상한(66e9dd9e0b · 0e9622787b) · N-15 롤백 상대 정수 검사(6a81d9ac7e).
- **온라인:** O-1 영수증 지급 · 환불이 거래당 레코드 하나를 버전 조건으로 다툰다(5141b93911) · O-2 TLS 클라이언트 서버 이름 검증(4e0c51722e) · O-4 거래 스냅숏을 당사자 밖에 내지 않는다(691fbe486d) · O-5 리더보드 쓰기 멱등 기록(1c1d594f44) · O-9 키트 보존 규칙 · 만료 색인(d1bbe65ace · abe54cfed9) · O-10 길드 창고의 이전 불가 자산은 맡긴 사람의 허용 안에서만(b2a12e5838 · 564fdf6a1d · f11cfdf881 — 설계는 [DesignBacklog](DesignBacklog.md) "길드 창고의 이전 불가 자산").
- **RHI:** R-2 장치 분실을 `IRHIDevice::isDeviceLost` 한 입구로(1a4c9b5c4a) · R-5 bindless 주인별 번호 목록(3662a42b0f).
- **에셋:** A-3 · A-4 팩 쓰기 · 읽기 한도 하나, Cooked 마운트 실패는 기동 실패(7639493bf9) · A-6 세이브 상태 판 올림 사슬(27d0d80bd5 · ab041b228f) · A-15 프리팹 스폰 원형 이진 본문(4339ca7db4) · A-18 실수 복수형 범주(6f121dd5cd).
- **시뮬레이션:** S-1 접촉 Stay opt-in(2017fbb151) · S-3 오디오 클립 캐시 예산 퇴출(d88842069b) · S-4 배율 변경은 바디를 두고 셰이프만 간다(2d764de775 · 56c2b7b086) · S-13 지운 바디가 같은 간격에 다시 닿으면 끝 · 시작(f441c0b842).
- **플랫폼:** P-1 Win32 원시 마우스 등록 · 이동량 출처(acac3ad5fc) · P-8 새니타이저 프리셋(9d91478995) · P-12 Win32 창 틀 DPI(a88446a564) · P-14 Windows 서비스 RUNNING 시점(264161a3e2).
- **GameFramework · 키트:** G-2 세계 시계 시 경계 멈춤(29627bb692) · G-3 제작 재료를 개체 상태째 되돌림(e25f386340) · G-4 단계 진입 보상 재지급(1f22ed288d) · G-5 사망 전이 한 곳(e115e3fd98) · G-6 상호작용 완료가 가용성을 다시 본다(719c228325) · G-7 세이브는 찍은 씬으로 옮긴 뒤 적용(27864fb4c7 · 5918551eaa · 02f53466e1) · G-9 틱 밖 씬 전수 걷기는 순회 열쇠(`SceneIterationKey`)가 컴파일에서 막는다(d4f26b9693) · K-1 LanternVale 이동 시간 예산(8748b7963e) · K-2 FrontierTown 빨리 감기가 도시에도(61580115b1).
- **에디터:** E-3 문서 패널마다 되돌리기 스택(a722e48f58) · E-4 다중 대상을 최상위 집합으로(19c7272302) · E-5 선택 포함 색인(3d9948c165) · E-10 피킹은 OBB · 삼각형까지(04eb4d7aa0) · E-11 콘텐츠 쓰기 확인 한 입구(16006e0cd0 · ce5660498a · 0d507f9fec) · E-12 되돌리기 대상은 id 로만(a644f468c8) · E-14 미리보기 창 닫으면 씬 · 타깃을 놓는다(db3c1ee9fe).
