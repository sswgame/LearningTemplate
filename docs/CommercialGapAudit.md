# 상용 엔진 대비 감사 (2026-11-12)

> 질문: 언리얼 5 · 유니티 6 · Godot 4 의 같은 시스템과 견주어 이 엔진에 남은 결함 · 성능 병목 · 구조 빈틈은 무엇이고 무엇부터 고치는가?
> 독자: 다음 묶음을 고르는 통합 담당과 그 일을 받는 작업자(에이전트와 사람).
> 결론: P0 는 모두 닫혔다. 열린 것은 P1 9 건 · P2 7 건(남은 몫만 있는 것 포함)이고 실행 단위는 "다음 할 일" 이다. 닫힌 항목은 지운다 — 이력은 git log 가 정본이다.

기준 커밋은 main `e480b07498` 이고 줄 번호는 그 커밋의 것이다. 감사 범위(`Source/` 2단계 폴더 122 개 체크리스트)와 닫힌 항목의 원문은 그 커밋의 이 문서에 있다. 2026-11-17 판정 갱신(통합 묶음 5 끝 a9e93f39a2): 열린 항목을 코드 · 커밋과 다시 대조해 닫힌 것을 지우고 남은 몫만 남겼다.

## 읽는 법

- **우선순위:** P0 — 지금 코드 경로로 확인된 오동작 중 데이터 · 진행 · 기동 · 보안에 닿는 것. P1 — 조건부 · 잠재 결함과 확장 · 성능 병목. P2 — 개선과 작은 결함.
- **확신:** "확인" 은 그 줄과 호출자를 읽었다는 뜻이다(실행 재현 없음). "추정" 은 코드만으로 확정하지 못한 것, "측정 필요" 는 구조는 확인했지만 비용을 재지 않은 것이다.
- **외부 API:** 감사 때 context7 이 없어 Win32 · DXGI · Vulkan · NDK · Emscripten · FCM 동작 주장은 기억 기준이다. 고치기 전에 그 판의 문서로 대조한다.
- **항목 한 줄:** 제목(분류 · 확신) — 근거(`파일:줄`), 상용 엔진, 제안, 확인(먼저 써서 지는 것을 볼 테스트). 경로는 저장소 루트 기준이고, 앞부분이 뻔한 경로는 파일 이름만 적는다.
- **닫을 때:** 고친 커밋(병렬 작업이면 그것을 합치는 통합 커밋)에서 이 문서의 항목 줄을 지운다 — 2026-11-17 에 문서가 코드보다 뒤처져 이미 닫힌 항목이 다시 배정됐다. 옛 백로그 번호(A-13 · A-16 등)와 겹치므로 커밋 메시지에는 `감사 <ID>` 꼴로 적는다. 코드 주석의 `감사 <ID>` 가 가리키는 닫힌 항목은 `git log --grep '감사 <ID>'` 로 찾는다(앞 커밋 몇은 `<ID>` 만 적었다 — `git log --grep '<ID>'`).
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

- **C-9 (남은 몫) `unordered_map` · `map` 이 std 와 같은 API 인데 예약 · 범위 구축 복잡도가 다르다** (성능 — 측정 필요) — 연속 링 `deque` 와 `vector` 범위 삽입 한 번 밀기는 끝났다(5bb5b69d58). 남은 것: `unordered_map.h:645` `reserve` 는 버킷만(밀집 배열은 예약하지 않는다), `map.h:308` 정렬 벡터 범위 `insert` 가 원소마다 삽입이라 대량 구축 O(n²). 상용: `TMap::Reserve` · `TSortedMap`. 제안: `reserve` 가 밀집 배열도 예약, `map` 범위 구축은 붙이고 `stable_sort` 뒤 중복 제거. 확인: 10 만 키 `map` 구축 · `unordered_map` 예약 뒤 삽입 할당 수 벤치(`ContainerBenchTest`).

### 온라인 서비스 · 저장소

- **O-12 (남은 몫) 부하 봇이 원장 경로를 치지 않고 가벼운 해시로 잰다** (구조 · 검증 공백 — 확인) — 열린 루프 `_pacing` 은 끝났다(a37479b3ef). 남은 것: 동작 표(`LoadBotScenario.cpp`)에 구매 · 영수증 · 우편 수령 · 거래 · 길드 창고 · 점수 제출이 없고, `Shooter3D.online.json` 의 `_bLightPasswordHash: true` 라 O-3 의 해시 비용이 안 보인다. 상용: k6 · Gatling 도착률 고정 실행기. 제안: `economy_purchase` · `mailbox_claim_all` · `trade_pair` · `leaderboard_submit` · `guild_storage` 동작, `_bLightPasswordHash` 를 끈 측정, 끝에 `LedgerAuditReport` 균형 검사. 확인: PostgreSQL 구성(`Shooter3D.external.json`)의 Conflict 재시도율 · `kBusy` 비율.

### RHI · 그래픽스 자원

- **R-4 (남은 몫) 자원마다 전용 할당 — 머티리얼 상수 버퍼 하나가 커밋 자원 · `VkDeviceMemory` 하나다** (구조 — 확인, NEXT RHI 5-2 "Vulkan 서브할당" 의 근거) — Vulkan 할당 수를 세고 한도에 닿으면 오류로 멈추는 몫은 끝났다(86d6d33a09). 남은 것: `MaterialRenderProxy.cpp:48` 이 인스턴스마다 `createConstantBuffer`, DX12 는 `CreateCommittedResource`(64 KB 단위), Vulkan 은 버퍼 · 텍스처마다 `vkAllocateMemory`. 상용: 언리얼 `FD3D12FastConstantAllocator` · Vulkan `FMemoryManager`, 유니티 SRP Batcher, Godot VMA. 제안: RHI 공용 `RHIBufferSlice{ buffer, offset, size }` — 상수 버퍼는 구간으로만, 메모리는 D3D12MA / VMA(vcpkg overlay) 또는 `SpanAllocator`. 확인: 머티리얼 인스턴스 5000 개의 버퍼 바이트 · Vulkan 할당 수.

### 렌더러

- **RN-4 (남은 몫) 그림자 아틀라스가 방향광 · 로컬을 한 텍스처 사분면으로 나누고 캐시가 타일 번호로 맞춘다** (구조 — 확인) — 로컬 칸을 빛 수에 맞춰 같은 크기로 나누고 캐스케이드 셋을 지키며 모자란 빛을 경고하는 몫은 끝났다(bc4784233f). 남은 것: 방향광 · 로컬 텍스처 분리(RHI 바인딩이 바뀐다), 빛마다 다른 크기 칸(화면 크기 할당기), `ShadowCache::update` 가 타일 번호로 지난 프레임과 견주어 우선순위가 뒤집히면 캐시 층을 다시 그린다. 상용: URP 주광 캐스케이드와 추가 광 가변 아틀라스 분리, 언리얼 VSM. 제안: 로컬은 화면 크기 할당기, 캐시 키는 빛 정체(컴포넌트 id, 면). 확인: `ShadowSetupTest` — 우선순위가 뒤집혀도 서 있는 빛은 Reuse.
- **RN-6 포함 여부가 하나라도 바뀌면 GPUScene 을 통째로 다시 모은다** (구조 · 확장 — 측정 필요) — `GPUSceneBuilder.cpp` ~730(집합 세대가 다르면 `collectAllCandidates` 전체 수집). 식생 셀 토글(`FoliageComponent.cpp:763`) · 스폰 · 소멸 · 스트리밍이 모두 이 길이다. 상용: 언리얼 `FScene` 프리리스트 칸, 유니티 GPU Resident Drawer. 제안: 인스턴스 번호를 "오래 가는 칸 + 프리리스트" 로, 배치 소속은 인스턴스 id 목록으로 간접화, 숨김은 플래그. 확인: `load/*` 20 만 + 매 프레임 스폰 · 소멸 10, Tracy `GT.GPUScene.build.collect`. 설계 판정(2026-11-17): 레지스트리가 인스턴스 칸을 고정하고 프리리스트로 재사용, 바뀐 칸만 더티 알림, 칸 용량이 같으면 바뀐 칸만 부분 수집한다.

### 에셋 · 직렬화 · 리플렉션 · 오브젝트 · 씬

- **A-13 씬 스트리밍: 모든 셀 서술이 메모리에 있고 셀 짓기가 게임 스레드 동기이며 내릴 때 상태를 버린다** (확장 · 성능 — 히치 측정 필요) — `SceneStreamingSubsystem.cpp:61-66 · 186-190 · 222-235`, `Scene.cpp:624-640`. 상용: World Partition 셀 패키지 비동기 + AddToWorld 시간 예산. 제안: 쿠킹이 셀 구간을 쓰고 런타임은 셀 색인만, 셀 로드는 워커 파싱 · 인스턴스화 + `processHeldRegistration` 예산, 내릴 때 `SaveGame` 상태를 셀 키로 보관. 확인: `load/` 셀 400 × 오브젝트 2000, `GT.Scene.service.SceneStreamingSubsystem` 최대 프레임.
- **A-16 (남은 몫) 배포 팩이 원문 번역 XML 도 싣는다** (구조) — 쿠커가 문화권마다 `.locres` 를 굽고 쿠킹본은 조회 사슬의 표만 읽는 몫은 끝났다(56b2ae1ce7, `LocalizationBinaryTableTest.CookedProjectReadsOnlyTheLookupChain`). 남은 것: 쿠킹본이 읽지 않는 저작 XML(`strings` · `translation`)을 팩에서 뺀다. 상용: 언리얼 `.locres` 만 출하. 확인: 쿠킹된 팩 색인에 번역 XML 이 없는지.

### 물리 · 캐릭터 · 내비

- **S-8 (남은 몫) 캐릭터 이동이 스텝마다 게임 스레드에서 한 명씩 돈다** (성능 — 측정 필요) — 일괄 API `moveCharacters( span )` 은 끝났다(bedf02fe30, `MoveCharactersMatchesMoveCharacter`). 남은 것: 구현이 아직 차례로 돌고(`CharacterControllerComponent.cpp`), `ExtendedUpdate` 가 씬 공용 `_tempAllocator` 를 받아 병렬화가 안 된다. 상용: 유니티 DOTS Character Controller 잡. 제안: 레인별 임시 할당자 + 병렬 실행. 확인: `load/*` 캐릭터 500 의 PreStep(워커 1 · 4 · 8)을 먼저 잰다.

### 플랫폼 · 창 · 입력 · 빌드

- **P-4 (남은 몫) Android IME 조합이 없고 웹 조합은 화면으로 확인하지 않았다** (결함 — 확인) — 순수 가상 훅, 웹 숨은 textarea composition, Android 소프트 키보드는 끝났다(6d7ab910ca · 6a10729061). 남은 것: Android 가 IME 조합(미확정 글)을 받지 못한다. 상용: 유니티 `TouchScreenKeyboard`, Godot `virtual_keyboard_show`. 제안: Android 는 GameTextInput. 확인: `WindowTextInputTest` Android 조합 케이스, `web-check` 에 `compositionend` 화면.

## P2 — 개선과 작은 결함

### 네트워크

- **N-11 (남은 몫) 손실 · RTT 에 반응하는 송신률 조절이 없다** (구조) — 재전송 백오프(RTT 흔들림 기준 시한, 시한마다 두 배)는 끝났다(6f4313263d). 남은 것: `NetEndpoint.cpp` 토큰 버킷이 고정이라 손실 · 지연이 늘어도 보내는 양이 같다. 상용: ENet RTT 분산 스로틀(같은 자리), 유니티 Transport. 제안: 연결별 상한을 손실률 · RTT 로. 확인: `NetEmulation` 손실 20 % 에서 `_resentMessageCount` 를 먼저 잰다.

### 렌더러

- **RN-16 (남은 몫) 품질 단계(스케일러빌리티)가 없다** (구조) — 렌더 정책 값과 추가 뷰 한도는 `EngineConfig._render` 로 옮겼다(936afaa804 · c74d0e7c05 · 0e26b5339f, 그리드 16x9x24 · 광원 1024 는 이유를 적고 남겼다). 남은 것: 같은 값을 품질 단계별 묶음으로 고르는 층이 없다. 상용: 언리얼 스케일러빌리티 그룹, 유니티 SRP 품질 에셋. 제안: `_render` 값의 품질 단계별 덮어쓰기 묶음(A-23 플랫폼 층과 같은 합치기 규칙). 확인: 로드 검증 단위 테스트.

### 에셋 · 직렬화 · 오브젝트

- **A-19 (남은 몫) 프리로드가 같은 바이트를 두 번 읽는다** (성능) — 요청 토큰 · 요청자 단위 취소는 끝났다(43b9a4ba6b, `CancelLeavesOtherRequesters`). 남은 것: `PreloadSet` 이 바이트를 읽어 버린 뒤 `pinAsset` 이 다시 읽는다. 상용: 언리얼 `FStreamableHandle`. 제안: `pinAsset` 이 (토큰, 로드 작업, 잰 바이트)를 돌려주고 `PreloadSet` 은 고정할 수 있는 종류면 따로 읽지 않는다. 확인: 프리로드한 에셋 하나의 읽기 횟수가 1.
- **A-20 (남은 몫) 머티리얼 · 텍스처 캐시 키가 요청 글 그대로다** (구조) — 공유 표 · 메시 · 프리팹 캐시 키는 리다이렉트를 푼 지금 id(`AssetRedirect::toCurrentID`)다(e5bc4e70d2). 남은 것: `MaterialCache` · `TextureCache` 키가 같은 함수를 지나지 않아 옛 id 와 지금 id 가 두 벌 오를 수 있다. 제안: 같은 키 함수를 쓰고, GUID 2 단계에서 키를 GUID 로. 확인: `SharedAssetCacheTest.RedirectedAndCanonicalIdsShareOneEntry` 꼴의 머티리얼 · 텍스처 케이스.
- **A-24 (남은 몫) 리플렉션 묶음 TU 가 헤더 하나에 전체를 다시 파싱한다** — `TagIndex` · `PrefabInstanceRegistry` · `SceneManager` 비동기 로드 중단 · `NetObjectTable` 64 비트 번호는 끝났다(08a8789f4c · 924aceae25 · 4d3bb0c51b · cfa4207beb). 남은 것은 NEXT 5-4 보강의 묶음 TU 다.

### 키트 · 게임

- **K-11 (남은 몫) 슈터 적이 매번 스폰된다** (성능 — 측정 필요) — 아레나 투사체는 `PrefabPool` 로 다시 쓴다(12bc84c07b, 쉬게 둘 수는 씬 데이터 `_projectilePoolSize`). 남은 것: 슈터 적. 상용: Lyra · Boss Room 풀. 제안: `spawnPrefab` 이 풀 정책(데이터)을 받는다. 확인: Tracy `GameModeFlush`.
- **K-12 (남은 몫) 복셀 월드가 전량 상주이고 메셔가 면마다 정점 6 개다** (성능 · 구조) — 청크 수는 씬 데이터로 옮겼다(6e993d4bad). 남은 것: 정한 수의 청크를 모두 들고 있다, `VoxelMesher.cpp` 의 면마다 정점 6 개(그리디 메싱은 합친 면의 UV 반복을 셰이더가 받아야 한다). 제안: "청크 좌표 → 청크" 사전 + 생성 · 저장 훅, 그리디 메싱 + 타일 UV 셰이더. 확인: Tracy `VoxelMesher::fillChunkMesh`.

## 다음 할 일

한 줄이 한 묶음(커밋 서넛)이다. "확인" 의 테스트를 먼저 써서 지는 것을 본 뒤 고친다. 성능 묶음은 측정이 먼저다. 온라인 · 서버 · 보안 묶음은 `security-and-hardening`, 외부 API 묶음은 context7 로 그 판의 문서를 먼저 대조한다. 번호는 NEXT 가 가리키므로 끝난 줄은 지우고 번호는 다시 매기지 않는다.

5. **읽은 것을 커밋 조건으로** — 일 안에서 읽은 것을 커밋 조건으로 붙이는 연결 포장과 Admin · Mailbox · Guild 저장 로직 점검(경제 키트는 영수증 레코드 하나로 묶였다 — O-1).
14. **웹 · Android 글 입력** — P-4 남은 몫(Android IME 조합, `web-check` 의 `compositionend` 화면).
16. **영속 정체성** — 머티리얼 · 텍스처 캐시 키를 풀린 정체성으로 A-20 남은 몫(에셋 GUID 2 단계와 같은 줄기).
20. **컨테이너 측정** — `unordered_map` 예약 · `map` 범위 구축 C-9 남은 몫. 벤치(`ContainerBenchTest`, 꺼 둠) 숫자가 문턱을 넘은 것만 고친다.
30. **온라인 부하** — 부하 봇 원장 동작 · 무거운 해시 측정 · `LedgerAuditReport` O-12 남은 몫.
31. **RHI 자원 모델** — 버퍼 구간 할당 · 서브할당 R-4 남은 몫.
34. **렌더러 규모** — RN-4 남은 몫 · RN-6. `load/*` 에 오브젝트 20 만 장면을 먼저 더한다.
35. **에셋 런타임** — 배포 팩에서 번역 XML 빼기 A-16 남은 몫.
36. **씬 스트리밍** — A-13.
37. **물리** — 캐릭터 이동 병렬화 S-8 남은 몫(PreStep 측정 먼저).
43. **네트워크** — 송신률 조절 N-11 남은 몫.
46. **렌더러** — 품질 단계 RN-16 남은 몫.
47. **에셋 · 오브젝트** — A-19 · A-24 남은 몫.
50. **키트** — K-11 · K-12 남은 몫.

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
