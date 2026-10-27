# 엔진 후보

> 상태: 후보 · 선행: 없음 · 영역: engine · 옛번호: B-11, B-13, C-4, C-7, C-22, C-25, C-34, C-47, C-49, C-63, C-64, C-65, C-68, C-69, C-71, C-72, D1-3, D1-4, D1-6, D1-7, D1-8, G-17, H-2 · 진행: 해당없음

엔진 구조 · 물리 · 내비게이션 · 모듈 영역에서 아직 착수하지 않은 일의 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다. 순서는 [로드맵](Roadmap.md)이 가집니다.

## 확인 대기

### IntellisenseAnnotationCheck
제목: 어노테이션 키 완성을 IntelliSense 에서 손으로 확인
옛번호: B-11
왜 필요한가: `ReflectionMacros.h` 는 `__INTELLISENSE__` 에서도 풀이(`ReflectionIntellisense.h`)를 읽습니다. 풀이는 MS 호환 모드의 dependent base 찾기 · `_Pragma( "clang diagnostic" )` · static_assert 안의 제네릭 람다 지역 클래스에 기대므로 Visual Studio IntelliSense(EDG)가 오류 없이 읽는지 봐야 합니다.
못 하는 이유: 사람이 VS 를 열어 눈으로 봐야 합니다(DTE `ErrorList` 로는 IntelliSense 오류가 안 나옴).
남은 것: VS 로 저장소를 연다 → `CameraComponent.h` 의 `PROPERTY( ` 안에서 Ctrl+Space 로 키 목록 → 오류 목록(IntelliSense 만)에서 어노테이션 헤더에 새 오류가 없는지. 오류가 나면 조건에서 `__INTELLISENSE__` 를 뺍니다.

### StructureMoveTests
제목: 구조 리팩토링 이동(S12 ~ S22)의 남은 테스트
옛번호: B-13
남은 것: S21 · S22 는 Debug 빌드 · 링크까지만 확인했습니다. `-L nogpu`(`NavMesh*` · `UserSettingsFileTest` · `AppHeadlessTasksTest` · `EditorFolderListing`), Shipping(단일 백엔드 정적 링크, `SW_PROFILE_COMPILED` 꺼짐), Linux(GLX 컨텍스트 · 다중 상속 경고), `OpenGLRHICommandList.cpp`(compile_commands 에 없어 문법 검사도 못 했음), 에디터 실기동(Present 훅), `assertdialog.scenario.xml`. `-gv_standardInputCommands` 는 에디터 `netplay` 시나리오가 두 프로세스에 `-gv_standardInputCommands=1` 을 걸어 매 통합 돌아 따로 두지 않는다.
실행 대기(통합): 매니페스트 id `ctest-nogpu`, `assertdialog-scenario`. Linux · `OpenGLRHICommandList.cpp` 는 CI, Shipping 은 Shipping 빌드 한 번, 에디터 실기동은 GPU 확인 때 같이 봅니다.
함정: S18 의 새 클래스는 ImGui 에 의존해 단위 테스트가 없습니다 — 에디터 자체 테스트 · `AppScenarioTest` 에디터 케이스가 봅니다.

### MultiGamePackVerification
제목: 게임 여럿 빌드 · 선택 실행의 네 백엔드 확인
옛번호: C-25
왜 필요한가: 게임 여럿을 빌드 한 번에 짓고 실행할 때 고르며 키트는 필요한 것만 싣는 구조입니다(2026-10-13 사용자 확정, [결정 기록](../09_Decisions.md) 5-2, `Source/Games/README.md`).
남은 것: 새 호스트 테스트 `AppScenarioTest.<게임>.<시나리오>.<백엔드>`(`--test_filter=AppScenarioTest.<게임>.*`)를 네 백엔드로 아직 안 돌렸습니다(DX12 직접 실행으로 10 시나리오 통과만 확인).
실행 대기(통합): 매니페스트 id `game-pack-scenarios`(네 백엔드). 통과하면 이 절을 지웁니다.

### NavMeshExtensions
제목: 3D 내비메시 확장(막는 식생 · AbilityArena 적 · 장애물 캐시)의 실행 확인
옛번호: D1-3
왜 필요한가: 셋 다 코드는 들어갔고(2026-10-22) 빌드 · 실행 확인이 남았습니다. 막는 식생은 `FoliageLayer::_navBlockRadius`(`FoliageNavGeometry.cpp`), AbilityArena 적은 프리팹의 `NavMeshAgentComponent`, 장애물 캐시는 에이전트 종류의 `_obstacleMode = TileCache`(`Recast/RecastTileCache.*` — 장애물은 `INavMesh::addCacheObstacle`)입니다.
남은 것: `FoliageTest.BlockingLayerAddsNavObstaclePerInstance` · `NavMeshTileCacheTest.*`, AbilityArena `ai.scenario.xml` · `control.scenario.xml` 이 내비메시 이동으로도 통과하는지, 장애물 캐시의 상자 요 부호(`NavMeshObstacleComponent::makeCacheObstacle` 의 atan2 와 Detour `addBoxObstacle` 의 `yRadians`)가 돌린 직사각 장애물에서 맞는지, 장애물이 많은 장면에서 다시 굽기와 캐시의 `GT.Navigation.*` 시간 견주기(층은 압축하지 않는다 — 메모리는 타일마다 층 크기 × 3 바이트).
실행 대기(통합): 매니페스트 id `foliage-navmesh-tests`, `ability-arena-ai-control-scenarios`, `navmesh-obstacle-yaw-and-time`. 요 부호 확인과 시간 견주기는 직사각 장애물 장면 · 측정이 필요해 시나리오가 없으면 테스트부터 만듭니다.

### StorageOwnershipLeftovers
제목: 저장소 소유 정리의 잰 뒤에 정할 것
옛번호: 없음
왜 필요한가: 종료 전용 해제 함수는 모두 없앴습니다(규칙은 `Core/Memory/LifetimeScope.h` 머리 주석). 아래는 재 봐야 고칠지 정해집니다(자라지 않으면 아무것도 바꾸지 않는 것이 답).
남은 것: (1) `BinarySerializer` 의 `thread_local` 버퍼 넷 · `ShaderParameterBinder` 의 `s_cbBytes` — 기준선 뒤에 자라는지 먼저 잰다(자라면 스레드 슬롯 · 바인더가 소유). (2) `LiveReloadManager::clearContexts` · `ModuleDependencyGraph::clear` 의 표 용량 — 소유자 소멸이 종료 보고 뒤인지 본다.
함정: `SceneTransformStorage::get` · `ResourcePaths` 경로 캐시는 엔진 수명 범위가 닫힌 뒤 쓰면 멈추거나(트랜스폼) 캐시하지 않는다(Debug 단언). 한 프로세스에서 엔진을 두 번 세우는 테스트 호스트가 생기면 이 계약이 먼저 걸린다.

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

## 정리

설계 판단이나 측정이 먼저인 정리 후보(StableAddressMap · VectorAliasLoops · StlContainerConfig · 이름 읽기 전수 · 에디터 명령 id 형식 · AnimationUpdateAnomalies)는 끝났고 결정은 [결정 기록](../decisions/5-3_CorePlatform.md) 5-3 · 5-4 · 5-11 에 있습니다. 전후 측정이 남은 둘은 위 `VectorAliasLoopsBench` 절과 조건부의 `AnimationUpdateAnomalies` 절 입니다.

### FunctorLambdaCommonization
제목: 함수 객체 · 람다 공통화 2단계
옛번호: C-68
왜 필요한가: `containers` 가 남긴 후보 58 파일 80 건(`find_if` · `remove_if` · 해시 구조체).
끝난 것(2026-10-21 ~ 24): 디렉터리별로 `GameFramework/Kits` · `Feature/Online` · `GameFramework/Base` · `Editor/Common` · `Editor/Panels/Inspector` 와 Engine · Games · Editor · Core 의 손 찾기 · erase-remove 를 `VectorUtil`(`removeIf` · `findBy` · `containsBy` · `findIndex` · `removeFirst` · `removeValue` · `findIf` · `containsIf`)로 옮겼고, `std::remove_if` 는 `CheckStdRemoveIf` 게이트가 `VectorUtil.h` 밖에서 막는다. 이름 붙은 지역 람다는 Core · Base · PlugIn · UI · Input · Games · Editor · Kits · Engine(Graphics · Renderer · Destruction · Animation · Automation · Serialization · Reflection · Object)에서 20줄 넘는 것 · 공용 위치가 있는 것을 함수로 뺐고 캡처가 본질인 짧은 것만 남겼다(Kits 카탈로그 `is` 11 곳은 `GameDataXML::isElement`, ID 목록 읽기 · 쓰기는 `StateArchiveUtil::writeIDList` · `readIDList`).
남은 것: 손 멤버 루프(`find_if`)를 못 쓰게 하는 형식 또는 린트(형식으로 못 막는 이유를 적는다)와 해시 구조체. 남은 손 루프는 목록이 C 배열 · 맵이거나 원소가 포인터이거나 조건이 둘 이상인 것이다(`Editor/Common` 의 `_id == id && _pInstance != nullptr`, `kArrKindInfo`). Base 에 남긴 것: 계산한 키로 정렬하는 람다(`TimingJudge` · `Inventory` · `ScheduleCatalog`), `hashed_string` 키 정렬(`GameFlags` — `CompareUtil` 에 비교 함수를 받는 키 비교자가 생기면 옮김, `Crafting` 은 대소문자 구분 저장 순서라 그대로), 반복자를 쓰는 `std::find`(`OnlineServiceClient`), 조건이 둘인 찾기 루프(`TimerQueue::findTimer`).

### ValidationIssueLogWorldOwned
제목: `ValidationIssueLog` 를 엔진 수명 범위의 창구에서 월드(게임 오브젝트 매니저) 소유로 옮기기
옛번호: 없음
왜 필요한가: `ValidationIssueLog::get()` 은 `ScopeOwned` 편의 창구라 단일체 예외 표에 `ScopeOwnedHotPath` 로 남았습니다(2026-10-24 유지 판정). 그러나 "출처(오브젝트)마다의 검증 결과" 라 월드가 둘이면(에디터 미리보기 창 · 씬 둘) 결과가 한 표에 섞입니다(언리얼 Map Check 는 월드별 메시지 로그).
남은 것: 표를 `GameObjectManager` 의 서브시스템(또는 `Scene` 이 든 값)으로 옮기고 파괴 경로(`GameObject` 소멸자)는 자기 매니저의 표를 직접 부릅니다. 맵 검사 패널은 활성 씬의 표를 읽습니다. `get()` 호출 9 곳(`MapCheckPanel` · `ObjectValidation` · `EditorSelfTestPanelCases`)이 소유자를 받는 모양이 되고, 옮긴 뒤 `CheckServiceAccess.toml` 의 `ValidationIssueLog` 줄을 지웁니다.

### ReflectedInstanceOwner
제목: 리플렉션이 만든 형식 소거 인스턴스의 소유 형식
옛번호: 없음
왜 필요한가: 맨 `sw_new` · 거친 `Memory::allocate` 는 `make_unique` · `MemoryBlock` 으로 바꿨고 `Style/RawNew` · `CheckRawAllocation` 이 막습니다. 남은 틈은 외부 라이브러리 할당 콜백과 리플렉션이 생성자를 불러 만든 인스턴스(블록 + `TypeInfo::_destroyInstance` 짝)입니다 — `void*` 를 맨 손으로 들고 `destroyInstance` 를 손으로 짝지어 부르는 모양이라 누수 · 이중 해제가 형식에 안 보입니다.
남은 것: 블록과 `const TypeInfo*` 를 함께 쥐고 소멸자가 `_destroyInstance` 후 블록을 푸는 이동 전용 소유 형식을 `Engine/Reflection` 에 두고 `CheckRawAllocation.toml` 의 `ProjectSettingsDocument.cpp`(`createInstance` 호출 8곳) · `UIDocumentLoader.cpp`(`createWidget`)를 옮긴 뒤 두 줄을 지웁니다. `GameObject.cpp` 의 컴포넌트 저장소는 풀 항목이 짝이라 따로 봅니다. `hashed_string.h` 의 청크 표(`atomic<Entry*>`)는 락 없이 읽는 표라 소유 형식을 담지 못하니 `atomic` 소유 칸 형식이 필요한지 함께 따집니다.

### WhenAnyHiddenEdge
제목: `whenAny` 트리거 → 후속 간선을 사이클 검사가 보게 하기
옛번호: 없음
왜 필요한가: `whenAny` 는 입력마다 트리거 태스크를 두고 트리거가 후속의 의존 수를 직접 내려서(`resolveDependency`) 후속 목록의 간선이 아닙니다. 그래서 `runBefore` 의 사이클 검사가 "입력 → 트리거 → 후속 → 입력" 을 못 보고 `waitAll` 이 멈춥니다(`TaskTypes.h` 의 알려진 빈틈).
남은 것: 설계 둘 중 선택. (가) 후속 노드에 "선행 중 하나면 된다" 표시(`_bAnyOfPredecessors` + 한 번만 내리는 `exchange` 칸)를 두고 `whenAny` 가 입력에서 후속으로 보통 `runBefore` 간선을 건다 — 간선이 목록에 있어 검사가 보고 경쟁이 없지만 완료 경로가 `TaskNode` 의 모든 후속에 한 분기를 더한다. (나) 목록 간선은 두고 사이클 검사만 노드의 `_listAnyInput` 을 `canReachThroughSuccessors` 가 따라가게 한다 — 완료 경로는 그대로이나 노드 크기와 수명(참조)이 는다. (가)가 같은 모양의 간선을 모두 보이게 하므로 낫고 분기 비용은 `TaskManagerBenchTest.SmallTaskThroughput` 으로 잰 뒤 정한다. 구현은 하지 않았다(완료 경로의 락 없는 코드는 빌드 · 테스트 없이 바꾸지 않는다).

## 조건부

### PerfConditionalCandidates
제목: 측정해서 이기면 하는 성능 후보 묶음
옛번호: C-7
왜 필요한가: 병렬 틱 임계값의 교차점, GameObject 레이아웃, 적응형 틱 임계값, 스폰 비용(~1.1 us, 잠금 여섯), 시퀀서 성능 수치.
착수 조건: 측정해서 이기면. TickItem 인라인 재시도는 오브젝트의 틱 부기 49 B 를 먼저 줄여야 합니다.
남은 것: 무버 일괄 틱의 다음 단계는 무버 데이터를 타입 배열(SoA)로 옮겨 일괄 함수가 컴포넌트를 읽지 않게 하는 것(일괄 틱은 `GT.Frame` p99 0.75 배로 들어갔고 더 줄일 필요가 보이면). 파괴 잎 셰이프는 Physics 에 셰이프 직렬화 함수가 생기면 쿠킹 때 Jolt 셰이프를 `.fracture` 에 싣는 안을 다시 봅니다.

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
