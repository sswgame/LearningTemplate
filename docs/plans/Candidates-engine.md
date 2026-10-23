# 엔진 후보

> 상태: 후보 · 선행: 없음 · 영역: engine · 옛번호: B-11, B-13, C-4, C-7, C-22, C-25, C-34, C-47, C-49, C-63, C-64, C-65, C-68, C-69, C-71, C-72, D1-3, D1-4, D1-6, D1-8, G-17, H-2 · 진행: 해당없음

엔진 구조 · 물리 · 내비게이션 · 모듈 영역에서 아직 착수하지 않은 일의 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다. 순서는 [로드맵](Roadmap.md)이 가집니다.

## 확인 대기

### IntellisenseAnnotationCheck
제목: 어노테이션 키 완성을 IntelliSense 에서 손으로 확인
옛번호: B-11
왜 필요한가: `ReflectionMacros.h` 는 `__INTELLISENSE__` 에서도 풀이(`ReflectionIntellisense.h`)를 읽습니다. 풀이는 MS 호환 모드의 dependent base 찾기 · `_Pragma( "clang diagnostic" )` · static_assert 안의 제네릭 람다 지역 클래스에 기대므로, Visual Studio IntelliSense(EDG)가 오류 없이 읽는지 봐야 합니다.
남은 것: 자동 확인 길이 없습니다(DTE `ErrorList.ErrorItems` 로는 IntelliSense 오류가 나오지 않음). 손 확인 — VS 로 저장소 폴더를 연다 → `CameraComponent.h` 의 `PROPERTY( ` 안에서 Ctrl+Space 로 키 목록을 본다 → 오류 목록(IntelliSense 만)에서 어노테이션 헤더에 새 오류가 없는지 본다. 오류가 나면 조건에서 `__INTELLISENSE__` 를 뺍니다.

### StructureMoveTests
제목: 구조 리팩토링 이동(S12 ~ S22)의 남은 테스트
옛번호: B-13
왜 필요한가: 측정은 2026-10-13 에 끝나 회귀가 없었지만([검증과 측정](../08_Verification.md) 2절), 테스트가 남았습니다.
남은 것: S21 · S22 는 Debug 빌드 · 링크까지만 확인했습니다. `-L nogpu`(`NavMesh*` · `UserSettingsFileTest` · `AppHeadlessTasksTest` · `EditorFolderListing` 테스트), Shipping(단일 백엔드 정적 링크, `SW_PROFILE_COMPILED` 꺼짐), Linux(GLX 컨텍스트 · 다중 상속 경고), `OpenGLRHICommandList.cpp`(compile_commands 에 없어 문법 검사도 못 했음), 에디터 실기동(Present 훅), `assertdialog.scenario.xml`, `-gv_standardInputCommands`.
함정: S18 의 새 클래스는 ImGui 에 의존해 단위 테스트가 없습니다 — 에디터 자체 테스트 · `AppScenarioTest` 에디터 케이스가 봅니다.

### MultiGamePackVerification
제목: 게임 여럿 빌드 · 선택 실행의 네 백엔드 확인
옛번호: C-25
왜 필요한가: 게임 여럿을 빌드 한 번에 짓고 실행할 때 고르며 키트는 필요한 것만 싣는 구조입니다(2026-10-13 사용자 확정). 결정은 [결정 기록](../09_Decisions.md) 5-2, 쓰는 법은 `Source/Games/README.md` 입니다.
남은 것: 새 호스트 테스트 `AppScenarioTest.EveryGamePackScenarioPassesOnEveryBackend` 를 네 백엔드로 아직 안 돌렸습니다(DX12 직접 실행으로 10 시나리오 통과만 확인).

### NavMeshExtensions
제목: 3D 내비메시 확장(막는 식생 · AbilityArena 적 · 장애물 캐시)의 실행 확인
옛번호: D1-3
왜 필요한가: 셋 다 코드는 들어갔고(2026-10-22) 빌드 · 실행 확인이 남았습니다. 막는 식생은 `FoliageLayer::_navBlockRadius`(`FoliageNavGeometry.cpp`), AbilityArena 적은 프리팹의 `NavMeshAgentComponent`, 장애물 캐시는 에이전트 종류의 `_obstacleMode = TileCache`(`Recast/RecastTileCache.*` — 타일 바이트가 높이 층 묶음이 되고 장애물은 `INavMesh::addCacheObstacle`)입니다.
남은 것: 테스트 `FoliageTest.BlockingLayerAddsNavObstaclePerInstance` · `NavMeshTileCacheTest.*` 를 돌린다. AbilityArena `ai.scenario.xml` · `control.scenario.xml` 이 내비메시 이동으로도 통과하는지 본다. 장애물 캐시의 상자 요 부호(`NavMeshObstacleComponent::makeCacheObstacle` 의 atan2 와 Detour `addBoxObstacle` 의 `yRadians`)가 맞는지 돌린 직사각 장애물로 본다. 장애물이 많은 장면에서 다시 굽기와 캐시의 `GT.Navigation.*` 시간을 견준다 — 층은 압축하지 않는다(저장 압축기, 메모리는 타일마다 층 크기 × 3 바이트).

### StorageOwnershipLeftovers
제목: 저장소 소유 정리의 잰 뒤에 정할 것
옛번호: 없음
왜 필요한가: 종료 전용 해제 함수는 모두 없앴습니다(규칙은 `Core/Memory/LifetimeScope.h` 머리 주석). 아래는 재 봐야 고칠지 정해지는 것입니다.
남은 것: (1) `BinarySerializer` 의 `thread_local` 버퍼 넷 · `ShaderParameterBinder` 의 `s_cbBytes` — 기준선 뒤에 자라는지 먼저 잰다(자라면 스레드 슬롯 · 바인더가 소유). (2) `LiveReloadManager::clearContexts` · `ModuleDependencyGraph::clear` 의 표 용량 — 소유자 소멸이 종료 보고 뒤인지 본다. (3) `GimmickNodeRegistry::getDefault` 의 기동 단계 등록(`ded0a6fd6`)을 `ScopeOwned` 로 바꿀 수 있는지 본다.
함정: `SceneTransformStorage::get` · `ResourcePaths` 경로 캐시는 엔진 수명 범위가 닫힌 뒤 쓰면 멈추거나(트랜스폼) 캐시하지 않는다(Debug 단언). 한 프로세스에서 엔진을 두 번 세우는 테스트 호스트가 생기면 이 계약이 먼저 걸린다.

## 정리

### StableAddressMap
제목: 값 주소가 옮겨지는 맵이 `&value` 를 내주는 길을 형식으로 막기
옛번호: 없음
왜 필요한가: `sw::unordered_map`(밀집 배열)은 삽입 때 값 주소가 옮겨지는데, 형식이 그것을 드러내지 않고 `&it->second` 를 내줍니다. TypedComponents T3 에서 테스트용 `makeMockComponentTypeInfo` 가 그 주소를 쥐어 타입이 늘자 매달렸습니다(`unique_ptr<TypeInfo>` 로 고침). 같은 모양이 다른 곳에 남을 수 있습니다.
남은 것: 확인된 위험 위치는 `LiveReloadManager.cpp:873`(연쇄 리로드 동안 `ModuleContext*` 를 쥠, 그 사이 `_mapModule` 에 삽입이 들어오면 매달림). `&it->second` 를 돌려주는 `find` 계열(`AbilityCatalog` · `CultureInfo` · `ReflectionTypes` 등)은 호출부가 결과를 오래 쥐면 같은 결함입니다. 근본 해법은 주소가 고정된 맵 형식(값을 `unique_ptr` 로 두는 형식)을 따로 두고, 주소를 오래 쥐는 자리는 그 형식만 쓰게 하는 것입니다. 호출부 전수 조사가 먼저입니다.

### PerfConditionalCandidates
제목: 측정해서 이기면 하는 성능 후보 묶음
옛번호: C-7
왜 필요한가: 조건부 후보의 묶음입니다 — 병렬 틱 임계값의 교차점, GameObject 레이아웃, 적응형 틱 임계값, 스폰 비용(~1.1 us, 잠금 여섯), 시퀀서 성능 수치.
착수 조건: 측정해서 이기면 합니다. TickItem 인라인 재시도는 오브젝트의 틱 부기 49 B 를 먼저 줄여야 합니다.
남은 것: 무버 일괄 틱의 다음 단계는 무버 데이터를 타입 배열(SoA)로 옮겨 일괄 함수가 컴포넌트를 읽지 않게 하는 것(대상 씬 컴포넌트의 칸 번호를 원소에) — 일괄 틱은 `GT.Frame` p99 0.75 배로 들어갔고 더 줄일 필요가 보이면 합니다. 파괴 잎 셰이프는 쿠킹 때 Jolt 셰이프를 `.fracture` 에 싣는 안(Chaos 가 지오메트리 컬렉션에 충돌을 같이 굽는 자리)을 Physics 에 셰이프 직렬화 함수가 생기면 다시 봅니다.

### PreludeAndFwdCleanup
제목: 작은 정리 후보
옛번호: C-22
왜 필요한가: 2026-10-13 병합 검토에서 모은 것입니다.
남은 것: `IPhysicsScene3D/2D` 전방 선언용 `*Fwd.h`, Core → Log 예외 다섯 파일, 프렐류드 예외 제거(184 파일).
함정: Core → Log 예외는 티어마다 필요한 것이 달라 함수 포인터 하나로 풀리지 않습니다.

### NameReferenceAudit
제목: 이름 참조 감사의 남은 것
옛번호: C-34
왜 필요한가: 데이터 쪽은 에셋 검증 `component_reference` 규칙, 코드 · 시나리오 쪽은 린트 `CheckNameReferences` 가 봅니다(2026-10-15 감사). 아직 정적으로 못 보는 것이 남았습니다.
남은 것: ① 입력 액션 이름의 생성자 기본값(`PawnComponent` 의 `Move` · `Look`, 카메라 액션)은 게임마다 맵이 달라 검증 함수로 못 보므로 빙의 · 프리셋 때 런타임 경고가 봅니다. ② 카탈로그 안 id 참조는 팩 둘(`starskirmish-unit-tree` · `nilecity-goods`)만 규칙이 있습니다 — 나머지 키트 카탈로그는 로더 경고(스키마 테스트)에 기대며, 에셋 검증 규칙 쪽 일입니다. ③ 시나리오 `<DevCommand line="editor <명령>">` 의 에디터 명령 id(`EditorCommandRegistry` 는 실행 중 등록), UI 바인딩 경로(`UIBindingSet::bind` 오류 로그), 동적으로 지은 액션 이름은 런타임 경고 게이트(logwarn)에 맡깁니다.
함정: 린트로는 더 풀 것이 없습니다(2026-10-18 판단). ③ 의 에디터 명령 id 를 정적으로 보려면 `SW_DEV_COMMAND` 처럼 선언으로 등록하는 형식이 먼저입니다(C++ 구조 변경, 결정 필요).

### AnimationUpdateAnomalies
제목: 합성 리그 벤치가 드러낸 애니메이션 갱신의 비정상 셋
옛번호: C-47
왜 필요한가: 기준표는 [검증과 측정](../08_Verification.md) "영역별 측정값"의 `SyntheticRigBenchTest` 표(2026-10-17, Release)입니다.
남은 것: (1) 캐릭터 1000 에서 본 150 → 300 이 2.5 배로 선형(2 배)보다 큽니다 — 캐시 · 워커 분배를 봅니다. (2) 본 300 + 리그 노드의 p99 40.6 ms 가 p50 의 3 배입니다 — 히치 원인을 찾습니다. (3) 네 발 60 본이 실행마다 흔들립니다(캐릭터 1000 p50 1223 ~ 3162 us).
함정: GPU 스키닝 시간은 런타임 .mesh 가 인덱스 없는 삼각형 목록이라 벤치 씬에 싣지 못해 재지 않았습니다.

### VectorAliasLoops
제목: `sw::vector` 의 `_list[i]` 루프 별칭
옛번호: C-49
왜 필요한가: 쓰기가 벡터 내부를 가려(별칭) 벡터화되지 않습니다(2026-10-17 `DynamicBitset` 측정에서 확인 — 원시 포인터 루프로 고쳤음).
남은 것: 다른 컨테이너의 블록 루프와 `sw::vector::operator==` 에 같은 모양이 있을 수 있습니다. 호출부와 이득을 Release 로 잰 뒤에 합니다.

### StlContainerConfig
제목: `SW_ENABLE_STL_CONTAINER` 를 할당자만 바꾸게 줄인다
옛번호: C-63
왜 필요한가: 그 옵션이 이름이 같은 `sw::` 형식을 구성마다 다른 형식으로 바꿉니다(2026-10-18 `A-20`). 해시 컨테이너는 두 구성이 같은 구현을 쓰게 했고 `CompressedPair` 로 `pair` 차이를 막았으며, natvis 는 자체 구현의 배치를 읽게 고쳤고, `TestDestructionDamage` 의 해시 기준값 테스트는 STL 구성에서도 돕니다.
남은 것: 옵션이 **할당자만** 바꾸게 줄입니다(ADL 로 `std::make_unique` 와 겹치던 것이 같은 근본 원인의 둘째 경우였음). 설계 판단이 커서 따로 합니다. ⓐ STL 구성의 목적(표준 컨테이너로 비교 · 디버그 STL 검사인가, `std::allocator` 로 메모리 추적을 빼는 것인가)을 먼저 정한다. ⓑ 목적이 할당자뿐이면 자체 `vector` · `array` · `map` · `set` · `pair` 를 두 구성에 늘 쓰고 `string` · `list` · `deque` 는 `std::*<T, 구성별 할당자>` 하나로 둔다. ⓒ 이때 STL 구성에서만 건너뛰는 테스트(`TestVector` · `TestDataStructure` · `TestMemory` · `TestMemoryTag`)를 다시 본다.
함정: `vector` · `array` · `string` · `list` · `deque` · `map` · `set` · `pair` · `span` 이 모두 구성에 따라 std 별칭과 자체 형식 사이를 오가고, `string` · `list` · `deque` 는 자체 형식도 std 를 상속합니다. `map` · `set` 은 두 구성 모두 키 순서지만 주소 · 반복자 보장은 기본 구성 쪽이 약합니다.

### SilentDefaultsRemaining
제목: 조용한 기본값이 남은 곳
옛번호: C-64
왜 필요한가: `A-26` · `C-62` 후속입니다. 필수 값이 없으면 오류로 멈추는 원칙([데이터 주도](DataDriven.md))의 남은 곳입니다.
남은 것: ④ `ConfigManager::isWithinRange` 가 `collectRangeErrors` 로 범위를 따로 검사합니다(`C-56` 과 같은 모양, Config 영역). ⑤ `readName` 호출부 357 곳을 전수로 보지 못했습니다.
함정: 이름을 경로 글로 바꿔 쓰는 `.c_str()`(`hashed_string` 의 FName 규칙: none = 빈 이름, 빈 이름의 `c_str()` = "None")은 형식으로 막지 못하고 이름 읽기 창구에서 글 none 만 막았습니다.

### FunctorLambdaCommonization
제목: 함수 객체 · 람다 공통화 2단계
옛번호: C-68
왜 필요한가: `containers` 가 남긴 후보 58 파일 80 건(`find_if` · `remove_if` · 해시 구조체)이 있습니다.
남은 것: 디렉터리별로 합니다. `GameFramework/Kits` 는 끝났습니다(2026-10-21, `wt/kitclean` — `erase( remove_if )` 9 곳은 `VectorUtil::removeIf`, 손 멤버 찾기 18 곳은 `findBy` · `containsBy`. `Feature/Online` 도 끝났습니다: 2026-10-22 `wt/onlineclean` — 값 찾기 8 곳은 `VectorUtil::contains`, 멤버 찾기 9 곳은 `findBy` · `containsBy`, 값 지우기 3 곳은 새 `VectorUtil::removeValue`). `GameFramework/Base` 도 끝났습니다(2026-10-21, `wt/baseclean` — 멤버 찾기 손 루프 34 곳, erase-remove, 단일 · 여러 키 정렬 비교자, 해시 구조체 1 개). `Editor/Common` 도 끝났습니다(2026-10-21, `wt/editorcommon` — `VectorUtil::removeIf` · `findBy`). 손 멤버 루프를 못 쓰게 하는 형식 또는 린트를 둡니다(형식으로 못 막는 이유를 적습니다). 2026-10-22 `wt/onlineclean` 에서 Engine · Games · Editor · Core 의 `std::find` 손 찾기 약 110 곳과 손 루프 약 130 곳, erase-remove 15 곳도 바꿨습니다(새 도우미 `findIndex` · `removeFirst` · `removeValue` · `findIf` · `containsIf`). 남은 손 루프는 목록이 C 배열 · 맵이거나 원소가 포인터인 것입니다. `std::remove_if` 는 끝났습니다(2026-10-21, `wt/gatesleft` — 남은 Engine · Core · Editor/Panels · Test 14 곳을 `VectorUtil::removeIf` 로 옮기고, 표준 함수라 형식으로 막을 수 없으므로 `CheckStdRemoveIf` 게이트가 `VectorUtil.h` 밖의 `std::remove_if` 를 막습니다). 손 멤버 루프(`find_if`)를 막는 형식 · 린트와 해시 구조체는 남았습니다.
Base 에 남긴 것: 계산한 키로 정렬하는 람다(`TimingJudge` 폭 합, `Inventory` 카탈로그 조회, `ScheduleCatalog` 인덱스 간접 정렬), `hashed_string` 키 정렬(`GameFlags`, `Crafting` 은 대소문자를 구분하는 저장 순서라 그대로), 반복자를 쓰는 `std::find`(`OnlineServiceClient` 지우기), 조건이 둘인 찾기 루프(`TimerQueue::findTimer`). `CompareUtil` 에 비교 함수를 받는 키 비교자(`hashed_string` 키용)가 생기면 `GameFlags` 를 옮깁니다.
`Editor/Common` 에 남은 손 루프는 조건이 둘 이상이거나(`_id == id && _pInstance != nullptr`) C 배열(`kArrKindInfo`)이라 `findBy` 로 옮기지 않았습니다.
끝난 곳: `Source/Editor/Panels/Inspector` 의 손으로 쓴 erase-remove 2건(`EditorPropertyGrid`)을 `VectorUtil::removeIf` 로 바꿨다(2026-10-21).
이름 붙은 지역 람다 Editor 끝(2026-10-23, wt/lambdaeditor): 20줄 넘는 것 5개를 정적 함수로 뺐다. 나머지(캡처가 본질이고 13줄 안쪽)는 그대로 둔다.

## 기능


## 조건부

### MetalMobileConsole
제목: macOS(Metal RHI) · 모바일 · 콘솔 백엔드
옛번호: G-17
왜 필요한가: RHI 는 DX12 · Vulkan · DX11 입니다. 콘솔은 SDK 계약이 필요합니다.
착수 조건: 해당 플랫폼으로 내는 게임이 생기면 합니다.

## 보류

### FNameNumberSuffix
제목: `hashed_string` 에 FName 숫자 꼬리를 둘지
옛번호: H-2
왜 필요한가: 지금은 비교 · 표시 인덱스 두 칸(8 바이트)이라 `"Enemy_12"` · `"Enemy_13"` 이 이름 표에 각각 영구 적재됩니다. 넣으면 `_숫자`(앞자리 0 제외)를 떼어 정수 칸에 두고 비교는 (인덱스, 숫자) 쌍입니다.
착수 조건: 런타임에 번호 붙은 이름을 대량으로 만드는 경로(복제 · 스폰 이름 자동 부여)가 생기면 다시 봅니다(사용자 결정 2026-10-03으로 보류).
