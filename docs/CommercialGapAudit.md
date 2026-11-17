# 상용 엔진 대비 감사 (2026-11-12)

> 질문: 언리얼 5 · 유니티 6 · Godot 4 의 같은 시스템과 견주어 이 엔진에 남은 결함 · 성능 병목 · 구조 빈틈은 무엇이고 무엇부터 고치는가?
> 독자: 다음 묶음을 고르는 통합 담당과 그 일을 받는 작업자(에이전트와 사람).
> 결론: P0 는 모두 닫혔다. 열린 것은 P1 12 건 · P2 12 건(남은 몫만 있는 것 포함)이고 실행 단위는 "다음 할 일" 이다. 닫힌 항목은 지우고, 코드 주석이 번호로 가리키는 것만 맨 아래 "닫힌 항목" 에 한 줄씩 남긴다.

기준 커밋은 main `e480b07498` 이고 줄 번호는 그 커밋의 것이다. 감사 범위(`Source/` 2단계 폴더 122 개 체크리스트)와 닫힌 항목의 원문은 그 커밋의 이 문서에 있다. 2026-11-17 판정 갱신(main 1b9293f5ed + 통합 묶음 5): 열린 항목을 코드와 다시 대조해 닫힌 것을 아래로 옮기고, 그때 옮겨 간 줄 번호만 고쳤다(나머지는 `e480b07498` 기준).

## 읽는 법

- **우선순위:** P0 — 지금 코드 경로로 확인된 오동작 중 데이터 · 진행 · 기동 · 보안에 닿는 것. P1 — 조건부 · 잠재 결함과 확장 · 성능 병목. P2 — 개선과 작은 결함.
- **확신:** "확인" 은 그 줄과 호출자를 읽었다는 뜻이다(실행 재현 없음). "추정" 은 코드만으로 확정하지 못한 것, "측정 필요" 는 구조는 확인했지만 비용을 재지 않은 것이다.
- **외부 API:** 감사 때 context7 이 없어 Win32 · DXGI · Vulkan · NDK · Emscripten · FCM 동작 주장은 기억 기준이다. 고치기 전에 그 판의 문서로 대조한다.
- **항목 한 줄:** 제목(분류 · 확신) — 근거(`파일:줄`), 상용 엔진, 제안, 확인(먼저 써서 지는 것을 볼 테스트). 경로는 저장소 루트 기준이고, 앞부분이 뻔한 경로는 파일 이름만 적는다.
- **닫을 때:** 고친 커밋(병렬 작업이면 그것을 합치는 통합 커밋)에서 이 문서의 항목 줄도 지우고 "닫힌 항목" 에 한 줄을 더한다 — 2026-11-17 에 문서가 코드보다 뒤처져 이미 닫힌 항목이 다시 배정됐다. 옛 백로그 번호(A-13 · A-16 등)와 겹치므로 커밋 메시지에는 `감사 <ID>` 꼴로 적는다.
- **다른 문서와의 관계:** 순서 · 다음 일의 정본은 [NEXT](NEXT.md), 설계 판단은 [DesignBacklog](DesignBacklog.md) 다. "(보강)" 은 그 문서들에 있던 일에 새 근거를 단 항목이다.

## 되풀이되는 모양

증상을 하나씩 고치지 말고 이 모양을 쓸 수 없게 형식을 바꾼다(괄호의 번호는 그 모양의 앞 사례이고 P-4 남은 몫 말고는 모두 닫혔다).

1. **세대 없는 번호 · 순번 · 경로 글을 정체로 쓴다.** 컴포넌트 `이름#순번`(A-2), 재시작마다 1 부터인 id(O-7), 32 비트 이름 해시(A-22 · C-8).
2. **같은 사실을 두 곳이 따로 안다.** 쓰는 쪽 · 읽는 쪽의 판(A-5), 개수는 CPU · 목록은 GPU(RN-1), 캐시 기록과 서버 메모리 표(O-6).
3. **실패 · 잘림을 값으로 돌려주지 않거나 호출자가 버린다.** 삼각 분할 실패(C-2), 매치 명단 발행(O-6).
4. **플랫폼 창이 "빠뜨려도 컴파일되는" 선택 훅을 가진다.** 포커스 · 글 입력 · 쉬기 · 수명 구독(P-3 ~ P-6).
5. **플레이어 세이브 · 설정이 판 올림 없이 엄격한 형식을 쓴다.** 사용자 설정 판(A-5), 컴포넌트 정체(A-2).
6. **상태 변화를 값이 아니라 호출 순서 규칙으로 다룬다.** 배속(K-6 · K-10), 전환 중 전환(G-13).

## P1 — 조건부 결함과 확장 · 성능 병목

### Core

- **C-9 컨테이너가 std 와 같은 이름 · API 인데 복잡도가 다르다** (성능 — 측정 필요) — `vector.h:977-988` 범위 `insert` 가 원소마다 삽입(`ByteStream.h:54` · `Archive.cpp:112`), `deque.h:14` 는 `std::deque` 래퍼(벤치 `ContainerBenchTest.*` 넷만 꺼 둔 채 들어갔다 — b5ba46b9c0)(MSVC 에서 큰 원소마다 힙 할당), `unordered_map.h:645` `reserve` 는 버킷만, `map.h:30` 정렬 벡터라 대량 구축 O(n²). 상용: `TArray::Append` · `TDeque` 링 버퍼 · `TMap::Reserve`. 제안: 한 번 예약 → 한 번 밀기, 2 의 거듭제곱 링 `deque`, `map` 범위 구축은 붙이고 `stable_sort`. 확인: 1 MB 범위 삽입 · `deque` 1 만 push 할당 수 · 10 만 키 `map` 구축 벤치.

### 온라인 서비스 · 저장소

- **O-12 (남은 몫) 부하 봇이 원장 경로를 치지 않고 가벼운 해시로 잰다** (구조 · 검증 공백 — 확인) — 열린 루프 `_pacing` 은 끝났다(a37479b3ef). 남은 것: 동작 표(`LoadBotScenario.cpp`)에 구매 · 영수증 · 우편 수령 · 거래 · 길드 창고 · 점수 제출이 없고, `Shooter3D.online.json` 의 `_bLightPasswordHash: true` 라 O-3 의 해시 비용이 안 보인다. 상용: k6 · Gatling 도착률 고정 실행기. 제안: `economy_purchase` · `mailbox_claim_all` · `trade_pair` · `leaderboard_submit` · `guild_storage` 동작, `_bLightPasswordHash` 를 끈 측정, 끝에 `LedgerAuditReport` 균형 검사. 확인: PostgreSQL 구성(`Shooter3D.external.json`)의 Conflict 재시도율 · `kBusy` 비율.

### RHI · 그래픽스 자원

- **R-4 (남은 몫) 자원마다 전용 할당 — 머티리얼 상수 버퍼 하나가 커밋 자원 · `VkDeviceMemory` 하나다** (구조 — 확인, NEXT RHI 5-2 "Vulkan 서브할당" 의 근거) — Vulkan 할당 수를 세고 한도에 닿으면 오류로 멈추는 몫은 끝났다(86d6d33a09). 남은 것: `MaterialRenderProxy.cpp:48` 이 인스턴스마다 `createConstantBuffer`, DX12 는 `CreateCommittedResource`(64 KB 단위), Vulkan 은 버퍼 · 텍스처마다 `vkAllocateMemory`. 상용: 언리얼 `FD3D12FastConstantAllocator` · Vulkan `FMemoryManager`, 유니티 SRP Batcher, Godot VMA. 제안: RHI 공용 `RHIBufferSlice{ buffer, offset, size }` — 상수 버퍼는 구간으로만, 메모리는 D3D12MA / VMA(vcpkg overlay) 또는 `SpanAllocator`. 확인: 머티리얼 인스턴스 5000 개의 버퍼 바이트 · Vulkan 할당 수.

### 렌더러

- **RN-3 광원을 뷰와 무관하게 전부 싣고 1024 를 넘으면 목록 뒤부터 자른다** (구조 · 확장 — 확인) — `GPULightBuffer.cpp:223-305` 는 절두체 판정 없이 싣고 `limitCPULightCount` 가 뒤(파티클 라이트 · 프로브 · 반사 프로브)를 버리며 2D 빛 꼬리 원소가 중간에서 잘릴 수 있다. 상한 · 그리드는 HLSL `#define`(`binding_slots.hlsli:341 · 392-394`). 상용: 언리얼 · URP · Godot 는 뷰마다 가시성 → 중요도 → 클러스터. 제안: 광원 경계 등록부 + 뷰 컬링 + 중요도 예산, 자르는 단위는 "빛 + 꼬리", 상한은 Config → 쿠킹 define. 확인: 광원 2000 중 화면 안 50 이 남는지, `load/*` 광원 4000.
- **RN-4 그림자 아틀라스가 2x2 고정 사분면이다** (구조 — 확인) — `ShadowAtlasLayout::make` 는 로컬 타일이 하나라도 있으면 캐스케이드를 2 로 줄이고 반 해상도로 한다(`ShadowSetup.cpp:107`). 로컬 용량(점광 1 + 스폿 2)을 넘으면 로그 없이 `continue`(벽 너머로 빛이 샌다), 순서가 뒤집히면 `ShadowCache::update` 가 캐시 층 전체를 다시 그린다. 상용: URP 주광 캐스케이드와 추가 광 가변 아틀라스 분리, 언리얼 VSM. 제안: 방향광 · 로컬 텍스처를 가르고 로컬은 화면 크기 할당기, 캐시 키는 빛 정체(컴포넌트 id, 면), 못 받은 빛은 통계 · 로그. 확인: `ShadowSetupTest` — 스폿 추가에도 캐스케이드 그대로, 우선순위가 뒤집혀도 서 있는 빛은 Reuse.
- **RN-5 점광 · 스폿 타일과 추가 뷰 캐스케이드가 합친 컬링 칸 하나를 쓴다** (성능 — 측정 필요) — `FramePassExecutorDraw.cpp:248-249` — 점광 하나가 방향광 볼륨 전체를 6 번 정점 처리한다. 인스턴스 배치(지형 · 식생)는 정적이 아니라 봐서(`GPUSceneBuilder.cpp:462`) 캐시 타일에서도 매 프레임 다시 그린다. 상용: 언리얼 · 유니티는 그림자 절두체마다 컬링. 제안: 타일마다 컬링 칸 또는 인스턴스 타일 비트마스크, 식생 · 지형에 "정적 그림자 캐스터" 선언. 확인: Shadow `GPU.PassMs` · `RT.Draw.gpuBatches` 를 점광 0 · 1 · 2(Release 식생 장면).
- **RN-6 포함 여부가 하나라도 바뀌면 GPUScene 을 통째로 다시 모은다** (구조 · 확장 — 측정 필요) — `GPUSceneBuilder.cpp` ~730(집합 세대가 다르면 `collectAllCandidates` 전체 수집). 식생 셀 토글(`FoliageComponent.cpp:763`) · 스폰 · 소멸 · 스트리밍이 모두 이 길이다. 상용: 언리얼 `FScene` 프리리스트 칸, 유니티 GPU Resident Drawer. 제안: 인스턴스 번호를 "오래 가는 칸 + 프리리스트" 로, 배치 소속은 인스턴스 id 목록으로 간접화, 숨김은 플래그. 확인: `load/*` 20 만 + 매 프레임 스폰 · 소멸 10, Tracy `GT.GPUScene.build.collect`.
- **RN-7 지형 청크 LOD 가 바뀔 때마다 CPU 가 메시를 새로 만든다** (성능 · 구조 — 측정 필요) — `TerrainComponent.cpp:917 · 962`(`updateLODs` → `rebuildChunkMeshes`), 프레임 상한 없음(컷이면 전 청크), 시점 하나(`:270-272`). 상용: 언리얼 랜드스케이프 LOD 별 공유 인덱스 + 정점 모핑. 제안: 청크당 최고 해상도 정점 한 벌 + (LOD, 이웃 마스크)별 공유 인덱스. 확인: `rebuildChunkMeshes` 구간을 2 km 지형 20 m/s 횡단 · 컷에서.

### 에셋 · 직렬화 · 리플렉션 · 오브젝트 · 씬

- **A-13 씬 스트리밍: 모든 셀 서술이 메모리에 있고 셀 짓기가 게임 스레드 동기이며 내릴 때 상태를 버린다** (확장 · 성능 — 히치 측정 필요) — `SceneStreamingSubsystem.cpp:61-66 · 186-190 · 222-235`, `Scene.cpp:624-640`. 상용: World Partition 셀 패키지 비동기 + AddToWorld 시간 예산. 제안: 쿠킹이 셀 구간을 쓰고 런타임은 셀 색인만, 셀 로드는 워커 파싱 · 인스턴스화 + `processHeldRegistration` 예산, 내릴 때 `SaveGame` 상태를 셀 키로 보관. 확인: `load/` 셀 400 × 오브젝트 2000, `GT.Scene.service.SceneStreamingSubsystem` 최대 프레임.
- **A-16 쿠킹된 이진 번역 표가 어디서도 쓰이지 않는다** (성능 · 구조 — 사용처 없음 확인) — `LocalizationManager.cpp:418 · 449 · 461` 의 `loadBinaryTable` · `cookBinaryTable` 호출이 테스트 밖에 없고, 배포는 모든 문화권 XML 을 읽어 마운트마다 전부 다시 채운다(`:174-250 · 541-636`). 상용: 언리얼 `.locres` — 지금 문화권 + 폴백 사슬만. 제안: 쿠킹이 문화권마다 표를 굽고 배포는 조회 사슬만 읽는다. 안 쓸 것이면 형식을 지운다. 확인: 키 20 만 × 문화권 15 의 `Localization.mount` · 메모리.

### 물리 · 캐릭터 · 내비

- **S-8 (남은 몫) 캐릭터 이동이 스텝마다 게임 스레드에서 한 명씩 돈다** (성능 — 측정 필요) — 일괄 API `moveCharacters( span )` 은 끝났다(bedf02fe30, `MoveCharactersMatchesMoveCharacter`). 남은 것: 구현이 아직 차례로 돌고(`CharacterControllerComponent.cpp`), `ExtendedUpdate` 가 씬 공용 `_tempAllocator` 를 받아 병렬화가 안 된다. 상용: 유니티 DOTS Character Controller 잡. 제안: 레인별 임시 할당자 + 병렬 실행. 확인: `load/*` 캐릭터 500 의 PreStep(워커 1 · 4 · 8)을 먼저 잰다.

### 플랫폼 · 창 · 입력 · 빌드

- **P-4 (남은 몫) Android IME 조합이 없고 웹 조합은 화면으로 확인하지 않았다** (결함 — 확인) — 순수 가상 훅, 웹 숨은 textarea composition, Android 소프트 키보드는 끝났다(6d7ab910ca · 6a10729061). 남은 것: Android 가 IME 조합(미확정 글)을 받지 못한다. 상용: 유니티 `TouchScreenKeyboard`, Godot `virtual_keyboard_show`. 제안: Android 는 GameTextInput. 확인: `WindowTextInputTest` Android 조합 케이스, `web-check` 에 `compositionend` 화면.

## P2 — 개선과 작은 결함

### 네트워크

- **N-11 (남은 몫) 손실 · RTT 에 반응하는 송신률 조절이 없다** (구조) — 재전송 백오프(RTT 흔들림 기준 시한, 시한마다 두 배)는 끝났다(6f4313263d). 남은 것: `NetEndpoint.cpp` 토큰 버킷이 고정이라 손실 · 지연이 늘어도 보내는 양이 같다. 상용: ENet RTT 분산 스로틀(같은 자리), 유니티 Transport. 제안: 연결별 상한을 손실률 · RTT 로. 확인: `NetEmulation` 손실 20 % 에서 `_resentMessageCount` 를 먼저 잰다.

### RHI

- **R-16 GL 은 드로우마다 정점 속성을 전부 다시 지정한다** (성능 — 측정 필요) — `OpenGLRHICommandContext.cpp:692-720`. 제안: (PSO 정점 형식, 슬롯 버퍼) 열쇠 VAO 캐시 — 3 순위 백엔드라 측정 뒤. 확인: Linux GL apitrace 호출 수.

### 렌더러

- **RN-14 글리프 아틀라스 페이지 비우기가 CJK 규모에서 흔들릴 수 있다** (구조 — 측정 필요) — `GlyphAtlas.h:58-59`(1024² × 8 상수), `GlyphCache.cpp:73-87`. 상용: Slate 는 아틀라스를 늘린다. 제안: 페이지 수 · 크기 Config, 비우기 전 최근 글리프 옮김. 확인: 한글 3000 자 로그의 `Text.GlyphsRasterized`.
- **RN-15 입자 정렬이 방출기 단위다** (구조 — 주석에 적힌 한도) — `GPUSceneBuilder.cpp:1257`. 상용: 나이아가라 · VFX Graph 출력별 정렬 모드. 제안: 방출기 정의에 정렬 모드, GPU 시뮬은 `instance_sort.hlsl`. 확인: 알파 연기 180° 골든.
- **RN-16 (남은 몫) 추가 뷰 한도가 상수이고 품질 단계가 없다** (구조) — PSO 예산 · 캐스케이드 람다 · 국소 그림자 바이어스 · TAA 기록 비율은 `EngineConfig._render` 로 옮겼다(936afaa804 · c74d0e7c05, 그리드 16x9x24 · 광원 1024 는 이유를 적고 남겼다). 남은 것: `kMaxExtraRenderView = 8`(`RenderView.h:37`) 상수, 품질 단계(스케일러빌리티) 없음. 상용: 언리얼 `r.*` + 스케일러빌리티, 유니티 SRP 에셋. 제안: 추가 뷰 한도를 `_render` 로, 품질 단계별 값 묶음. 확인: 로드 검증 단위 테스트.

### 에셋 · 직렬화 · 오브젝트

- **A-19 (남은 몫) 프리로드가 같은 바이트를 두 번 읽는다** (성능) — 요청 토큰 · 요청자 단위 취소는 끝났다(43b9a4ba6b, `CancelLeavesOtherRequesters`). 남은 것: `PreloadSet` 이 바이트를 읽어 버린 뒤 `pinAsset` 이 다시 읽는다. 상용: 언리얼 `FStreamableHandle`. 제안: 프리로드가 읽은 결과를 핀 요청에 넘긴다. 확인: 프리로드한 에셋 하나의 읽기 횟수가 1.
- **A-20 (보강) 공유 캐시 키가 요청 글 그대로라 같은 에셋이 두 벌 오를 수 있다** (구조) — `SharedAssetTable.h` `makeKey`(요청 경로 글을 `normalizeSeparators` 만). 제안: GUID 2 단계에서 키를 풀린 정체성으로. 확인: `SharedAssetCacheTest.RedirectedAndCanonicalIdsShareOneEntry`.
- **A-23 (남은 몫) 호스트 설정에 플랫폼 덮어쓰기 층이 없다** (구조) — DLC 검사기가 없으면 거절, Zstd 사전 삭제, 게임별 쿠킹 규칙 층은 끝났다(8699fd978b · 6b1b5edeb2 · 274cab2f19 · e423e8156f). 남은 것: `ConfigManager` 에 플랫폼 층이 없어 압축 기본(zlib)이 모든 플랫폼에서 같다. 상용: 언리얼 플랫폼별 `*Engine.ini` 층. 제안: 플랫폼 덮어쓰기 층. 확인: 설정 로드 단위 테스트.
- **A-24 (남은 몫) 그 밖의 작은 것** — `FloatCurve` NaN · JSON 중복 키 · `JSONDocument` 깊이 256 · XML 중복 속성은 끝났다(96abfe4836 · 8d33f5d4db · 5f62d275ef). 남은 것: `TagIndex` 첫 질의 깊은 복사, `PrefabInstanceRegistry` 매 프레임 전체 훑기, `NetObjectTable` 씬 번호가 이름 해시 31 비트, 씬 비동기 로드가 다른 요청에도 끝까지 짓고 버림(`SceneManager.cpp:637`), 리플렉션 묶음 TU 가 헤더 하나에 전체 재파싱(NEXT 5-4 보강).

### 키트 · 게임

- **K-11 오브젝트 풀이 한 곳에서만 쓰인다** (성능 — 측정 필요) — `PrefabPool` 은 슈터 탄착뿐, 아레나 탄 · 슈터 적은 매번 스폰. 상용: Lyra · Boss Room 풀. 제안: `spawnPrefab` 이 풀 정책(데이터)을 받는다. 확인: Tracy `GameModeFlush`.
- **K-12 복셀 월드가 고정 크기 전량 상주이고 메셔가 면마다 정점 6 개다** (성능 · 구조) — `VoxelGameModeComponent.h:51-52`, `VoxelMesher.cpp:119-123 · 160-188`. 제안: "청크 좌표 → 청크" 사전 + 생성 · 저장 훅, 그리디 메싱. 확인: Tracy `VoxelMesher::fillChunkMesh`.
- **K-13 LanternVale 서버가 변경과 무관하게 전원을 저장하고 매 틱 전 엔티티를 다시 싣는다** (성능) — `ValeNetLink.cpp:343-389`. 상용: Push Model · Iris. 제안: 프로필 저장 세대, 바뀔 때만 `setEntity`. 확인: `net-pair mmo` 자동 저장 쓰기 수.

## 다음 할 일

한 줄이 한 묶음(커밋 서넛)이다. "확인" 의 테스트를 먼저 써서 지는 것을 본 뒤 고친다. 성능 묶음은 측정이 먼저다. 온라인 · 서버 · 보안 묶음은 `security-and-hardening`, 외부 API 묶음은 context7 로 그 판의 문서를 먼저 대조한다. 번호는 NEXT 가 가리키므로 끝난 줄은 지우고 번호는 다시 매기지 않는다.

5. **읽은 것을 커밋 조건으로** — 일 안에서 읽은 것을 커밋 조건으로 붙이는 연결 포장과 Admin · Mailbox · Guild 저장 로직 점검(경제 키트는 영수증 레코드 하나로 묶였다 — O-1).
14. **웹 · Android 글 입력** — P-4 남은 몫(Android IME 조합, `web-check` 의 `compositionend` 화면).
16. **영속 정체성** — 공유 캐시 키를 풀린 정체성으로 A-20(에셋 GUID 2 단계와 같은 줄기).
20. **컨테이너 측정** — 범위 삽입 · 링 버퍼 `deque` C-9. 벤치(`ContainerBenchTest`, 꺼 둠) 숫자가 문턱을 넘은 것만 고친다.
30. **온라인 부하** — 부하 봇 원장 동작 · 무거운 해시 측정 · `LedgerAuditReport` O-12 남은 몫.
31. **RHI 자원 모델** — 버퍼 구간 할당 · 서브할당 R-4 남은 몫.
34. **렌더러 규모** — RN-3 · RN-4 · RN-5 · RN-6 · RN-7. `load/*` 에 광원 4000 · 오브젝트 20 만 장면을 먼저 더한다.
35. **에셋 런타임** — 이진 번역 표 A-16(쓰거나 형식을 지운다).
36. **씬 스트리밍** — A-13.
37. **물리** — 캐릭터 이동 병렬화 S-8 남은 몫(PreStep 측정 먼저).
43. **네트워크** — 송신률 조절 N-11 남은 몫.
45. **RHI** — GL VAO 캐시 R-16(측정 뒤).
46. **렌더러** — RN-14 · RN-15, RN-16 남은 몫.
47. **에셋 · 오브젝트** — A-19 · A-23 · A-24 남은 몫.
50. **키트** — K-11 · K-12 · K-13.

## 깊이 못 본 곳

다음 감사가 이어서 볼 자리다(여기의 결함은 이 문서에 없다).

- **RHI:** 백엔드별 PSO 생성 스레드 안전과 전송 티켓 계약, WebGPU · GL CommandContext, Vulkan 장치 고르기, EGL · GLX 문맥.
- **렌더러:** `FramePassExecutorPasses.cpp` 대부분, `RenderGraph::executeParallel` 배리어 묶음, 모프 · 정점 애니메이션 풀, 실시간 프로브, forward 계열 파이프라인 XML.
- **직렬화 · 리플렉션:** `SerializerUtil.cpp` · `XMLSerializer.cpp` · `DataInheritance.cpp` · `ResourceComposer.cpp`, ReflectionParser `CodeGenerator.cpp`.
- **입력 · 창:** `InputMap` 평가와 고정 틱 사이 엣지 유실, `WaylandWindow.cpp`, UI 접근성 브리지.
- **온라인:** `OnlineCore/Server/HTTP`(HPACK · HTTP/2 동적 표 · 프레임 길이 상한), Apple · Google 영수증 검증기 본문, RESP 재연결 뒤 응답 짝 · O-7 죽은 서버의 파티 티켓 풀기 · 비교 후 쓰기(748bca0b0d) · O-6 명단 캐시 기록(13d9d54758 · 6d8fc7620d).
- **게임플레이:** `ScheduleSystem.cpp`, BehaviorTree · Blackboard, Appearance, 장르 키트 14 개 내부 로직.
- **에디터:** 임포터(Model · Texture · Vrm), Animation 패널, 프리팹 오버라이드 적용, 도킹 · 레이아웃.
- **실행으로만 확인되는 것:** 성능 항목 전부(측정 전), 외부 API 동작에 기댄 항목(R-1 · P-5 · P-13 · O-15), 추정 항목.

## 닫힌 항목

코드 주석이 `감사 <번호>` 로 근거를 대는 항목이다. 판단 한 줄과 고친 커밋만 남긴다(전문은 `e480b07498` 판 이 문서와 커밋 메시지).

- **P0:** P0-1 · P0-2 문서 되돌리기를 상호작용 구간 하나로(b8473b34f6 · 977e4d28a0) · P0-4 옮긴 문서 · 씬의 경로가 따라간다(45f5a3f2d6 · 8b12e105ed) · P0-5 세션 상태 구간을 못 읽으면 로드 전체 실패(ef6e2a3b74) · P0-6 복원은 검증 → 교체, 저장 막기는 범위형 `SaveLock`(801b62e7cc) · P0-7 분개 보존 정리가 영수증 중복 방지 기록을 지우지 않는다(4493a0df85) · P0-8 끊긴 연결 번호를 같은 갱신에서 재사용하지 않는다(421856cb5a) · P0-9 Android 자체 종료는 `ANativeActivity_finish`(afa347088b) · P0-10 Android 볼륨 키 소비 판정(27c738c316) · P0-11 오디오 백엔드 없는 구성은 CMake 에서 멈추고 Android AAudio · 웹 AudioWorklet 백엔드를 둔다(ec297c8197 · 938e1a9483 · 9a3060de71) · P0-13 오디오 장치 유실 계약(3e883528e7) · P0-14 타일맵 충돌이 오너 이동을 따른다(fd769f5585) · P0-15 Win32 Alt · F10 창 메뉴 모드 차단(996394ca30) · P0-16 · P0-17 POSIX exec 실패 · 줄 쪼개기(821c55c889).
- **Core:** C-1 쿼터니언 → 오일러 피치 ±90° 특이점(0293095f01) · C-3 합류로 잠드는 워커도 유휴 마스크에(86fb9b0e61) · C-4 큐가 차면 잠든 풀을 깨운다(e082c6f7e1) · C-5 적응형 `SpinLock`(b261acc635) · C-8 현지화 키 해시 64 비트 + 충돌 오류(65f98c29d6) · C-10 `appendFormat` 잘림을 실패로(113e42d23a) · C-11 백오프 지터(456fc3a9ea) · C-12 `getTotalTime` float64(df6eeafff0) · C-13 POSIX 크래시 처리기가 앞 처리기를 잇는다(ba3cf4e860) · C-17 이름 풀이 차면 멈춘다(2f20abeafa) · C-18 경로 포함 판정의 `..`(db54280e9d) · C-2 삼각 분할 구멍 다리의 모서리 원뿔 검사(3198393a5e) · C-7 문자열 `gv_` 잠금 형식 GlobalString(fc98beda2e) · C-6 출하 할당기는 `MemoryBackend` 한 입구, `SW_MEMORY_BACKEND`(crt · mimalloc)로 고른다(7d45366fc3, 벤치 b5ba46b9c0) · C-14 Math 폴더 통째 결정성 표시 + 기록 비트 테스트, libm 초월 함수 검사는 두지 않는다(결정 D7-8, CMake 주석)(1a8c789a8b) · C-16 사용자 데이터 쓰기 fsync, 세이브 직전 세대 `.prev` 보존(2cc4e11ad6 · a50470020a) · C-19 IO 스레드가 죽으면 큐를 실패로 멈추고 크래시 flush 는 장치 락을 기다리지 않는다(3904a3ddde · f20b583e50) · C-20 구독 제어 블록 — 출처 소멸과 해제의 겹침(7a2fdb301c · 82019d36b9) · C-21 다시 연 후보 정리, 마지막은 크래시 필터가 자세한 이유를 덤프 뒤 보고 스레드에서 만든다(fc0ed885eb).
- **네트워크:** N-1 접속 증명 태그 PRF(ac944d4075) · N-2 순서 번호를 종류마다(4bb15d623f) · N-3 신뢰 창이 차면 연결을 닫는다(0c6cecfe78) · N-4 서버 틱 추정 float64(da9b332d88) · N-5 파괴 스냅숏 요청 중복 제거(d04ddcd157) · N-6 텔레메트리 업로드 전용 스레드(100835c275) · N-8 기한 큐 · 잠금 밖 AEAD · 슬롯 지연 할당(9283b011a2 · 3fe02c6232 · 290b0d3bea · 66f70c136f) · N-10 TLS 핸드셰이크 시한(6e1dd25570) · N-12 멱등 기억 바이트 예산 · 대기자 상한(66e9dd9e0b · 0e9622787b) · N-15 롤백 상대 정수 검사(6a81d9ac7e) · N-7 게임 UDP 지표를 /metrics 에, 끊김 사유는 열거에서, 접속 로그 주소 가림(4cfa420041) · N-13 RemoteCall 연결별 호출률 상한 · 함수 번호(3926b651e9) · N-14 `NetAddress` 주소군 + 16 바이트, IPv6 소켓(9fb508a62f).
- **온라인:** O-1 영수증 지급 · 환불이 거래당 레코드 하나를 버전 조건으로 다툰다(5141b93911) · O-2 TLS 클라이언트 서버 이름 검증(4e0c51722e) · O-4 거래 스냅숏을 당사자 밖에 내지 않는다(691fbe486d) · O-5 리더보드 쓰기 멱등 기록(1c1d594f44) · O-9 키트 보존 규칙 · 만료 색인(d1bbe65ace · abe54cfed9) · O-10 길드 창고의 이전 불가 자산은 맡긴 사람의 허용 안에서만(b2a12e5838 · 564fdf6a1d · f11cfdf881 — 설계는 [DesignBacklog](DesignBacklog.md) "길드 창고의 이전 불가 자산") · O-3 서비스 대기 상한 · 해시 동시 실행 상한(kBusy), 해시는 저장소 워커 밖 상한 있는 줄로(65e5d95e66 · 256e4296bc) · O-6 명단 기록 쓰기가 Ok 일 때만 Found, 백필 입구(6d8fc7620d) · O-13 외부 로그인 nonce 를 서버가 내고 한 번만 쓴다(b820729d9f) · O-14 주소 시도 창을 휘발 저장소 INCR + TTL 로 나누고 Conflict 면 다시 읽어 센다(b820729d9f · 5cb9716cfd) · O-15 FCM 은 `UNREGISTERED` 일 때만 기기 삭제, 토큰은 역색인으로 한 계정에만(47af719e3e) · O-16 GM 제재 등급별 최대 길이 · 대상 계정 확인(cf08fa551d) · O-17 신뢰 프록시의 PROXY protocol v2 만 받는다(8873da03c0) · O-18 다시 읽은 후보 — 목록 쪽 읽기 한 입구, 토큰 버킷 청소 문턱, 마이그레이션 호환성 표시 외(24d308a704 · 13cda011df · 9f47996888 외).
- **RHI:** R-2 장치 분실을 `IRHIDevice::isDeviceLost` 한 입구로(1a4c9b5c4a) · R-5 bindless 주인별 번호 목록(3662a42b0f) · R-1 DXGI 창 연결에 `NO_WINDOW_CHANGES · NO_ALT_ENTER`(af9f2c71e8) · R-3 정적 메시 버퍼는 장치 로컬(e734a3758a · ff7947ffbc · 9f76266c8e · 08c706885d) · R-6 텍스처 스트리밍 풀 예산 · 밉 범위 읽기 · SRV 번호 고정 · 밉 내리기 · 밉 요청 대기 줄(1cccb5dac3 · 3ebc4a76cf · 92caa26b99 · e7275a2d50 · e85123e290 · f2a7bd8227 · d5ec621ff5 · d0739ed69c) · R-7 DX12 는 힙 CBV 를 만들지 않는다(dcae672736) · R-8 Vulkan 스왑체인 형식 역매핑, 재생성 때 oldSwapchain(50e719a4fd · d7edfdbfbb) · R-9 모든 단계 읽기 전이, `_rhi` GBV · syncval 스위치(0690db9b8f · 1277fd07d0) · R-10 텍스처 전이 · 첨부 뷰 서브리소스, 공용 형식 + 백엔드 전부(07ff989e6e · d04fd71572 · 5cfc96d50e · 82771710a2 · 518de6ad1a · b2ca60642b · 4582c3f8f8) · R-11 RTV · DSV 칸은 쪽 목록(53fffdb8da) · R-12 같은 프레임 상수 버퍼 두 번째 쓰기는 진단 오류 `RHI.ConstantBufferRewrittenInFrame`(8cc002e5d3) · R-13 파이프라인 캐시 파일 이름에 vendor · device · driver, 주기 저장(cc817ecee4 · 0dbb5c8ec5) · R-14 DX11 기능 수준 11_0 만, Debug 레이어 부재 처리(1d8808160f) · R-15 인덱스 uint16(.mesh 판 7) · 정점 색 half4 · `MeshCPUView`(da825564d9 · 0162c06a22 · 634e5b286e) · R-17 통보 중 파괴는 소멸자가 단언(65af391c37).
- **렌더러:** RN-1 Preserve 투명 배치가 절두체 밖 가시 칸을 비운다(b0e9d9fef6) · RN-2 TAA 서브픽셀 지터(739acac593) · RN-8 자동 렌더 배율은 GPU 프레임 시계로(w-rs RS-1 ~ RS-4, 0192f36c87 등) · RN-9 GPU 방출기가 다시 실릴 때 빨리감지 않는다(ba059374f5) · RN-10 `FrameViewTarget::beginViewHistory` · `markFrameSkipped`(a359f8d80d) · RN-11 AO 는 조명 패스 앰비언트 · 프로브에만(f02632c7de) · RN-12 별칭 계획 수명 합치기 전파(3fcf85dc7c) · RN-13 보이는 뷰 먼저, `RenderView.OverflowCount`(b250bfd994).
- **에셋:** A-3 · A-4 팩 쓰기 · 읽기 한도 하나, Cooked 마운트 실패는 기동 실패(7639493bf9) · A-6 세이브 상태 판 올림 사슬(27d0d80bd5 · ab041b228f) · A-15 프리팹 스폰 원형 이진 본문(4339ca7db4) · A-18 실수 복수형 범주(6f121dd5cd) · A-5 사용자 설정 주인별 판 · 새 판 파일 보관(5d225f4c90) · A-2 컴포넌트 고정 지역 id · 중첩 부품 `#<16진>` · 이전 도구(89b32e551a · 3cc472259c · cced0ea6f7 · f9ba06b0a6 · 7e7d016002 · 409d47d457) · A-7 제자리 다시 읽기는 떼어 내기 하나로(0d0fe6bb3d · bdac6bedfe · 4318fcc848) · A-8 스탬프에 `args <해시>` 필수 줄(b0fc6b1a65) · A-9 색인 구역 요약 서명 · 블록 해시 표(5034821aee) · A-10 팩 형식 3 스트림 쓰기 · 도메인 팩 나누기(9291f812a2 · 5e3ca160b2 · a97b1d03b3 · 4f1231936f) · A-11 원시 바이트 해시 · 영속 `ContentHashCache`, FNV-1a 64(bb0302bc64) · A-12 정렬 FAT 색인 · 블록 범위 읽기(92caa26b99 · d7879d98b2 · 0e9998941f · c0de5273e5) · A-21 수명 불변식 셋을 코드로(dab63d852f · 8efc8f3f8e) · A-22 별칭 덮기는 오류, 잠금 중첩 제거(432cdbe687).
- **시뮬레이션:** S-1 접촉 Stay opt-in(2017fbb151) · S-3 오디오 클립 캐시 예산 퇴출(d88842069b) · S-4 배율 변경은 바디를 두고 셰이프만 간다(2d764de775 · 56c2b7b086) · S-13 지운 바디가 같은 간격에 다시 닿으면 끝 · 시작(f441c0b842) · S-2 Jolt 임시 할당자 설정 · 폴백(bedf02fe30) · S-6 64 비트 폴리 참조 강한 형식, `DT_POLYREF64`(1058e32452) · S-7 캐릭터 속 바디 기본 · 캐릭터끼리 막기(bedf02fe30) · S-9 이동 전용 `Ticket` + `_maxPathRequestsPerUpdate`(d76ccda9cc) · S-10 보이스 우선순위 뺏기 · 한 블록 페이드(ad50521115 · fba7b4fc08) · S-11 `DebrisBudget`, 상한은 물리 설정 필수 값(1001c35d4f) · S-12 끊긴 링크 · 없는 핀 · 기다림 없는 고리 거절(ecc00ede4c · 572d8617dd) · S-14 등록표 세대 대조(ff535f30c5 · 7a69a0427c).
- **플랫폼:** P-1 Win32 원시 마우스 등록 · 이동량 출처(acac3ad5fc) · P-8 새니타이저 프리셋(9d91478995) · P-12 Win32 창 틀 DPI(a88446a564) · P-14 Windows 서비스 RUNNING 시점(264161a3e2) · P-3 웹 · Android 포커스 입력(4435fa3230) · P-6 앱 멈춤을 입력 · 오디오도 받는다(2818b73417) · P-10 배경 패드 입력 정책(ae98475c0a) · P-11 접근성 단축키 포커스 연결, 배포 빌드만(789415fe26 · 3a8f3c8bc9) · P-2 리사이즈는 프레임 시작으로 미루고 모달 루프 동안 프레임을 돌린다(b2057904f8) · P-5 `IWindow::waitForNextFrame`(c838926134) · P-9 Web-Release · Android-Release 프리셋 · `SW_STATIC_MODULES`(322ba3c30f) · P-13 스플래시 전용 스레드(73119293be · 8feae4be02).
- **GameFramework · 키트:** G-2 세계 시계 시 경계 멈춤(29627bb692) · G-3 제작 재료를 개체 상태째 되돌림(e25f386340) · G-4 단계 진입 보상 재지급(1f22ed288d) · G-5 사망 전이 한 곳(e115e3fd98) · G-6 상호작용 완료가 가용성을 다시 본다(719c228325) · G-7 세이브는 찍은 씬으로 옮긴 뒤 적용(27864fb4c7 · 5918551eaa · 02f53466e1) · G-9 틱 밖 씬 전수 걷기는 순회 열쇠(`SceneIterationKey`)가 컴파일에서 막는다(d4f26b9693) · K-1 LanternVale 이동 시간 예산(8748b7963e) · K-2 FrontierTown 빨리 감기가 도시에도(61580115b1) · G-8 조용히 무효가 되는 카탈로그 값은 읽기에서 거절(2d6a78780c) · G-10 관통 탄이 맞힌 대상을 고리 8 칸에(0cca01fca0) · G-11 카메라 충돌 레이어는 프리셋 데이터, 없으면 오류(c3a5f15122) · G-12 미저장 보호를 세션 · 문서가 쥔다(342b9d60f3 · cdc38c4dda) · G-13 어두워지는 중의 새 전환은 거절(5f519e2590) · K-4 키트 규칙 설정을 자산으로(fe2d414a8d · b59af575e1 · 2bd88bcb14 · d9136393ed · 6a4759e855 외) · K-5 float32 유지 + `TimeRolloverUtil::rebase`(cd74ad5bcb · 75450a022f) · K-6 `FixedStepTimer::consumeScaled`(2d85efef47) · K-7 `RuleViewCatalog` 로 모델 경로 조립 제거(8b0296fcd5) · K-8 광석 · 눈이 `_seed` 를 쓴다(15aa1aca31) · K-9 `ContentPreparationToken`(029c3f9fc0 · baaaa19035) · K-10 `GameSpeedControl` · `SimulationViewSync`(81b9e5c9c3 · 5f73d33dab) · K-14 장르 키트 17 개 규칙 시연 씬 · 시나리오(ee5b4102ff … 46f88d46a0).
- **에디터:** E-3 문서 패널마다 되돌리기 스택(a722e48f58) · E-4 다중 대상을 최상위 집합으로(19c7272302) · E-5 선택 포함 색인(3d9948c165) · E-10 피킹은 OBB · 삼각형까지(04eb4d7aa0) · E-11 콘텐츠 쓰기 확인 한 입구(16006e0cd0 · ce5660498a · 0d507f9fec) · E-12 되돌리기 대상은 id 로만(a644f468c8) · E-14 미리보기 창 닫으면 씬 · 타깃을 놓는다(db3c1ee9fe) · E-1 문서 패널 자동 저장 사본(c6cfffdecd · de3e2710ec) · E-2 열린 편집이 있으면 되돌리기 거절(3b8c0715bc) · E-6 형제 순서 `renumber` 제거(2dedf077aa · 9e09eb48e0 · 0570bb915c · 202752449f) · E-7 편집 없이 풀린 대기 기록은 버린다(b5d41ba310 · f628a08bfc · 8a936f0fa3) · E-8 `EditorAssetIndex`, `EditorReferenceIndex` 제거(5735b0e93d · 9882baddff · a5c1b68391 · 2931d1876e) · E-9 편집 알림은 직렬화 함수를 받는다(dc9f05ded5 · ae25a5727a) · E-13 되돌리기 한도는 개수 + 바이트, 문서 되돌리기(de3e2710ec · a36b93f499 · 87528773bb · 6d801e7454 · e543bfe66b · 93c1b6302c) · E-15 `_bEndWithParent` 로 자식 트리를 부모와 묶는다(cf2fb8097a) · E-16 머티리얼 편집 연산이 스스로 알린다(e567f4476e).
