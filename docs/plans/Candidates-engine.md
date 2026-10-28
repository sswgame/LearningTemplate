# 엔진 후보

> 상태: 후보 · 선행: 없음 · 영역: engine · 옛번호: B-11, B-13, C-4, C-7, C-22, C-25, C-34, C-47, C-49, C-63, C-64, C-65, C-68, C-69, C-71, C-72, D1-3, D1-4, D1-6, D1-7, D1-8, G-17, H-2 · 진행: 해당없음

엔진 구조 · 물리 · 내비게이션 · 모듈 영역에서 아직 착수하지 않은 일의 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다. 순서는 [로드맵](Roadmap.md)이 가집니다.

설계 판단이나 측정이 먼저인 정리 후보(StableAddressMap · VectorAliasLoops · StlContainerConfig · 이름 읽기 전수 · 에디터 명령 id 형식 · AnimationUpdateAnomalies)는 끝났고 결정은 [결정 기록](../decisions/5-3_CorePlatform.md) 5-3 · 5-4 · 5-11 에 있습니다. 전후 측정이 남은 둘은 아래 `VectorAliasLoopsBench` 절과 조건부의 `AnimationUpdateAnomalies` 절 입니다.
실행만 남은 확인은 검증 매니페스트(`Test/QA/VerifyBatch.json`)의 일회성 항목이 주인입니다([결정 기록](../09_Decisions.md) 5-2 · 5-6, 2026-10-27). 코드는 다 들어갔고 통합이 한 번 돌리면 항목을 지웁니다. 아래 절은 사람이 해야 하거나 장비 · 데이터가 없어서 묶음이 돌리지 못하는 것만 둔다.

## 확인 대기

### ClothSkinnedCapeScreen
제목: 스킨 망토 캐릭터 씬에서 윗변이 뼈를 따르는지
옛번호: D1-4
왜 필요한가: 스킨 망토의 고정 정점을 뼈에 묶었다(천 C3). Jolt 행렬 방향과 질량 중심 기준 뼈 행렬이 맞아야 망토 윗변이 뼈를 따라 움직인다.
남은 것: 사람이 스킨 망토 캐릭터의 윗변이 뼈를 따르는지 화면으로 본다 — 매니페스트 id `cloth-skinned-cape-scene`.
못 하는 이유: 스킨 망토를 단 캐릭터 씬이 저장소에 없고 윗변 위치를 읽는 탐침도 없어 시나리오가 판정하지 못한다. 풀리는 조건: 그 씬과 탐침이 생기면 `manual` 을 떼고 시나리오로 옮긴다.

### PhysicsExtensionsScreens
제목: 천과 캐릭터 · 파워드 래그돌의 화면 확인
옛번호: D1-8
왜 필요한가: 캐릭터 속 바디(`_bInnerBody`)와 파워드 래그돌(`_poweredMaxTorque`)은 빌더 단 테스트가 통과했고 컴포넌트 단(애니메이터가 도는 래그돌)과 화면이 남았다.
남은 것:
- 매니페스트 id `physics-cloth-character-scene` — 천을 단 오브젝트 옆으로 속 바디를 켠 캐릭터가 지나갈 때 천이 밀리는지 본다(제 속 바디를 거르는 질의는 테스트가 본다).
- 매니페스트 id `physics-powered-ragdoll-scene` — `_poweredMaxTorque` 를 올린 래그돌이 쓰러지며 팔다리를 애니메이션 자세로 끄는지, 일어서기로 넘어갈 때 모터가 꺼지는지(`_bMotorsDriven`) 본다. 래그돌 동안 애니메이터가 섞기 전 포즈를 계속 평가한다는 가정이다(멈추면 목표가 마지막 포즈에 머문다).
못 하는 이유: 두 장면과 시나리오가 없고 판정이 "밀린다 · 끈다" 는 눈 확인이다. 풀리는 조건: 장면이 생기고 천 정점 위치 · 관절 목표 오차를 읽는 탐침이 생기면 `manual` 을 떼고 시나리오로 옮긴다.

### WorldStreamingChecks
제목: 월드 스트리밍의 히치 측정과 셀 창 캡처
옛번호: D1-7
왜 필요한가: 셀 격자 · 쿠킹 분할 · 스트리밍 서브시스템 · 서버 여러 원점(`NetMMOTest`) · WildWest 초원 데이터는 테스트와 `--cook-scenes` 쿠킹이 통과했다. 규칙은 결정 기록 5-4 의 월드 스트리밍 줄이다.
남은 것:
- 매니페스트 id `world-streaming-hitch-measure` — Release 로 WildWest 를 돌리며 셀이 오르내리는 프레임의 `GT.Scene.tick.destroyPost` 최대값과 정점 풀 repack 횟수(0 이어야 한다)를 잰다. 정점 · 모프 풀 부분 갱신(C-43)의 전/후 숫자(`RT.GPUScene.vertexPool` · `RT.Deform.rebuildMorphPool` 의 p50 · p99 와 집합이 바뀐 프레임의 최대값)도 이때 같이 [검증과 측정](../08_Verification.md) 2절에 적는다.
- 매니페스트 id `world-cells-panel-capture` — World Cells 창 캡처(`ux-reviewer`)와 `uiaudit` 시나리오에 창이 잡히는지, 문구 열 개의 `--check-text`.
- `MMOReplicator` 를 쓰는 게임 서버가 아직 없다 — 첫 MMO 게임이 틱마다 `replicator.update` 다음에 `feedStreamingSources( 씬의 WorldStreamingSubsystem )` 를 부르고 `initialize` 에 `getGrid()` 를 넘긴다. HLOD 는 메시 LOD 뒤다.
못 하는 이유: 히치 측정은 카메라를 prairie 한 끝에서 다른 끝으로 옮기는 시나리오와 프로파일 구간을 읽는 perf-compare 케이스가 없고 Release 에서 GPU 를 혼자 써야 한다. 캡처는 사람 눈(`ux-reviewer`)이다. 풀리는 조건: 걷는 시나리오와 케이스가 생기면 히치 항목에 명령을 달고 `manual` 을 뗀다.

### VectorAliasLoopsBench
제목: sw::vector 채우기 · 비교 변경의 전후 벤치
옛번호: 없음
왜 필요한가: 1 바이트 타입(`char` 는 무엇과도 별칭)의 저장이 벡터 멤버 `_pData` 와 겹쳐 보이는 문제를 `sw::vector` 채우기(`assign( count, value )` · `resize( count, value )` → `fillConstructRangeInternal`)와 `operator==` 의 버퍼 포인터 지역 뜨기로 고쳤다(`DynamicBitset` 은 고쳤다). 읽기만 하는 비교 루프는 이득이 없을 수 있다.
남은 것: 매니페스트 id `vector-bulk-fill-bench` — `ContainerBenchTest.VectorBulkFillAndCompare` 를 `vector.h` 변경 앞 커밋과 뒤 커밋의 CoreTest 두 벌로 번갈아 3 판 돌려 이득이 없는 쪽은 되돌린다. 뒤 값은 `structural-optimization-benches` 가 Release 측정 때마다 낸다.
못 하는 이유: 전 커밋을 따로 지어야 하는 바이너리 A/B 이고 한 명령에 전 바이너리를 줄 길이 없다. 풀리는 조건: 이득 판정이 필요 없어지면(되돌리지 않기로 하면) 이 절과 항목을 지운다.

### StructuralOptimizationBinaryAB
제목: 구조 최적화 B ③ 해시 태그의 전후 바이너리 측정
옛번호: 없음
왜 필요한가: 구조 최적화는 벤치 커밋을 먼저, 최적화를 따로 커밋해 이득이 없으면 그 커밋만 되돌린다(결정 기록 5-3). 해시 표 노드 해시 태그(B ③)는 들어갔고 측정 전이다.
남은 것: 매니페스트 id `container-hash-tag-bench` — 벤치 커밋과 해시 태그 커밋을 번갈아 지은 CoreTest 두 벌로 `CoreTest.exe --test_filter=ContainerBenchTest.* --load_suites=only` 를 3 ~ 5 회 재고(`UnorderedMapInsertErase` · `IntegerKeyLookup` · `StringKeyLookup`) 이득이 없으면 그 커밋만 `git revert` 한다. `sw::deque`(B ②)는 벤치 `DequeQueueChurn`(8 · 32 · 128 B)만 들어갔다 — 전 값을 본 뒤 직접 구현할지 정한다.
못 하는 이유: 전 커밋을 따로 지어야 하는 바이너리 A/B 이다. 풀리는 조건: 판정을 하지 않기로 하면 이 절과 항목을 지운다.

## 실행 대기

- 구조 이동 S21 · S22(nogpu), 3D 내비메시 확장(막는 식생 · 장애물 캐시 요 부호) — 실행 대기(통합): 매니페스트 id `ctest-nogpu`, `foliage-navmesh-tests`. 단언 대화상자 · 표준 입력 명령 · 게임 팩 네 백엔드 · AbilityArena 내비 이동 시나리오는 통합 S 의 `app-scenarios-release` 통과(1300 s, 시나리오 Warning/Error 0)로 덮여 항목을 지웠다.

## 조건부

### MetalMobileConsole
제목: macOS(Metal RHI) · 모바일 · 콘솔 백엔드
옛번호: G-17
왜 필요한가: RHI 는 DX12 · Vulkan · DX11 입니다. 콘솔은 SDK 계약이 필요합니다.
착수 조건: 해당 플랫폼으로 내는 게임이 생기면.

### AnimationUpdateAnomalies
제목: 애니메이션 갱신 비용 이상 셋의 원인
옛번호: 없음
왜 필요한가: 합성 리그 벤치에서 세 이상이 보였다(본 150 → 300 의 2.5 배, 후처리 리그 300 본의 p99 3 배, 네 발 60 본의 실행 사이 흔들림). `AnimationSystem::evaluate` 의 `BasePose` · `Attachment` · `PostProcess` · `SkinPalette` 단계마다 프로파일 구간(`GT.Animation.basePose` · `attachment` · `postProcess` · `skinPalette`)을 두었고 `SyntheticRigBenchTest` 가 가장 느린 프레임의 번호를 로그에 남긴다.
착수 조건: Release 측정(매니페스트 id `animation-cost-curve`, 합성 리그 벤치 `SyntheticRigBenchTest.*`)의 단계별 구간이 한 단계에 몰리면 그 단계의 코드를 고친다. 가장 느린 프레임이 처음 몇 프레임에 몰리면 첫 접근 비용, 흩어지면 스케줄러 · 판 사이 흔들림이다. 판 사이 흔들림은 이 기계의 일반 잡음(프레임 8 ~ 12 ms)일 수 있어 [결정 기록](../09_Decisions.md) 의 `crowdBuckets` 항목과 같은 방법(전후를 번갈아 5 판 이상)으로 본다.

### ClothDynamicMeshFixedSpan
제목: 천 동적 메시 전용 고정 정점 구간
옛번호: D1-4
왜 필요한가: 동적 메시(천)를 씬 정점 풀이 그 구간만 올리게 했다(천 C2, 결정 기록 5-5). 풀 다시 채우기(`_repackCount`)가 잦으면 동적 메시 전용 고정 구간을 둔다.
착수 조건: 천 쇼케이스를 도는 `perf-compare --cases clothShowcase`(매니페스트 id `cloth-vertex-pool-time`)에서 `RT.GPUScene.vertexPool` 이 프레임 시간에 보이거나 다시 채우기가 정상 상태에서도 매 프레임 일어날 때.

### EnginePartitionCubeBench
제목: 큐브 2만 개 벤치의 GT.Frame 기준값
옛번호: C-1
왜 필요한가: 코어 `ScenePrimitive` 슬롯(엔진 분할 E3)은 트랜스폼 전파 핫 패스에 가상 호출을 넣지 않는 구조이고 `MeshComponent` 의 리플렉션 부모가 바뀌어 모든 모듈이 다시 빌드됐다(결정 기록 5-6). 구조 테스트(`GPUSceneTest` · 부모 사슬 · 인스턴스 배치)는 통과했다. E3 이전 커밋과의 전후 비교는 이제 하지 않고(전 바이너리를 따로 지어야 하고 구조가 이미 들어갔다) 이후 변경이 핫 패스를 느리게 하는지를 기준값으로 지킨다.
착수 조건: `perf-compare --cases cubeBench20k`(매니페스트 id `engine-partition-e3-cube-bench`)의 `GT.Frame` p50 · p99 가 이 기계의 첫 측정값([검증과 측정](../08_Verification.md) 2절)보다 p50 +15 %, p99 +35 % 를 넘으면 그 변경을 되돌리거나 고친다.

## 보류

### FNameNumberSuffix
제목: `hashed_string` 에 FName 숫자 꼬리를 둘지
옛번호: H-2
왜 필요한가: 지금은 비교 · 표시 인덱스 두 칸(8 바이트)이라 `"Enemy_12"` · `"Enemy_13"` 이 이름 표에 각각 영구 적재됩니다. 넣으면 `_숫자`(앞자리 0 제외)를 떼어 정수 칸에 두고 비교는 (인덱스, 숫자) 쌍입니다.
착수 조건: 런타임에 번호 붙은 이름을 대량으로 만드는 경로(복제 · 스폰 이름 자동 부여)가 생기면 다시 봅니다(사용자 결정 2026-10-03 으로 보류).
