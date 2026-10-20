# 엔진 후보

> 상태: 후보 · 선행: 없음 · 영역: engine · 옛번호: B-11, B-13, C-4, C-7, C-13, C-19, C-22, C-25, C-34, C-47, C-49, C-63, C-64, C-65, C-66, C-67, C-68, C-69, C-70, C-71, C-72, D1-3, D1-4, D1-6, D1-8, G-1, G-5, G-17, H-2 · 진행: 해당없음

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
남은 것: S21 · S22 는 Debug 빌드 · 링크까지만 확인했습니다. `-L nogpu`(`NavMesh*` · `UserSettingsFileTest` · `AppHeadlessTasksTest` · `EditorFolderListing` 테스트), Shipping(단일 백엔드 정적 링크, `SW_PROFILE_COMPILED` 꺼짐), Linux(GLX 컨텍스트 · 다중 상속 경고), `OpenGLRHICommandList.cpp`(compile_commands 에 없어 문법 검사도 못 했음), 에디터 실기동(Present 훅), `assertdialog.scenario.xml`, `-gv_reloadGameAtFrame`, `-gv_standardInputCommands`.
함정: S18 의 새 클래스는 ImGui 에 의존해 단위 테스트가 없습니다 — 에디터 자체 테스트 · `AppScenarioTest` 에디터 케이스가 봅니다.

### MultiGamePackVerification
제목: 게임 여럿 빌드 · 선택 실행의 네 백엔드 확인
옛번호: C-25
왜 필요한가: 게임 여럿을 빌드 한 번에 짓고 실행할 때 고르며 키트는 필요한 것만 싣는 구조입니다(2026-10-13 사용자 확정). 결정은 [결정 기록](../09_Decisions.md) 5-2, 쓰는 법은 `Source/Games/README.md` 입니다.
남은 것: 새 호스트 테스트 `AppScenarioTest.EveryGamePackScenarioPassesOnEveryBackend` 를 네 백엔드로 아직 안 돌렸습니다(DX12 직접 실행으로 10 시나리오 통과만 확인).

## 정리

### StableAddressMap
제목: 값 주소가 옮겨지는 맵이 `&value` 를 내주는 길을 형식으로 막기
옛번호: 없음
왜 필요한가: `sw::unordered_map`(밀집 배열)은 삽입 때 값 주소가 옮겨지는데, 형식이 그것을 드러내지 않고 `&it->second` 를 내줍니다. TypedComponents T3 에서 테스트용 `makeMockComponentTypeInfo` 가 그 주소를 쥐어 타입이 늘자 매달렸습니다(`unique_ptr<TypeInfo>` 로 고침). 같은 모양이 다른 곳에 남을 수 있습니다.
남은 것: 확인된 위험 위치는 `LiveReloadManager.cpp:873`(연쇄 리로드 동안 `ModuleContext*` 를 쥠, 그 사이 `_mapModule` 에 삽입이 들어오면 매달림). `&it->second` 를 돌려주는 `find` 계열(`AbilityCatalog` · `CultureInfo` · `ReflectionTypes` 등)은 호출부가 결과를 오래 쥐면 같은 결함입니다. 근본 해법은 주소가 고정된 맵 형식(값을 `unique_ptr` 로 두는 형식)을 따로 두고, 주소를 오래 쥐는 자리는 그 형식만 쓰게 하는 것입니다. 호출부 전수 조사가 먼저입니다.

### GameFrameworkStructureCleanup
제목: GameFramework 구조 정리의 남은 것
옛번호: C-4
왜 필요한가: 2026-10-04 리뷰(사용자 승인)에서 나온 칸 손셈 · 버퍼 정리입니다.
남은 것: Overworld `TileMap` 의 칸 손셈(`indexOf` · `isInBounds` — 크기는 파일 스키마 `TileMapXMLData` 가 듭니다), `Habitat`(CreatureLife), NetConnection 메시지 버퍼 재사용. 게임 손셈(HarvestValley `FarmCropComponent` · `FarmSoilComponent`, NileCity `NileDirectorComponent` 의 `index % getWidth()`)은 키트가 `getTopology()` 를 열면 같이 합니다. 로드맵상 파일이 겹쳐 미룬 정리입니다.

### PerfConditionalCandidates
제목: 측정해서 이기면 하는 성능 후보 묶음
옛번호: C-7
왜 필요한가: 조건부 후보의 묶음입니다 — 병렬 틱 임계값의 교차점, GameObject 레이아웃, 적응형 틱 임계값, 스폰 비용(~1.1 us, 잠금 여섯), 시퀀서 성능 수치.
착수 조건: 측정해서 이기면 합니다. TickItem 인라인 재시도는 오브젝트의 틱 부기 49 B 를 먼저 줄여야 합니다.
남은 것: 무버 일괄 틱의 다음 단계는 무버 데이터를 타입 배열(SoA)로 옮겨 일괄 함수가 컴포넌트를 읽지 않게 하는 것(대상 씬 컴포넌트의 칸 번호를 원소에) — 일괄 틱은 `GT.Frame` p99 0.75 배로 들어갔고 더 줄일 필요가 보이면 합니다. 파괴 잎 셰이프는 쿠킹 때 Jolt 셰이프를 `.fracture` 에 싣는 안(Chaos 가 지오메트리 컬렉션에 충돌을 같이 굽는 자리)을 Physics 에 셰이프 직렬화 함수가 생기면 다시 봅니다.

### HeaderIncludeSlimming
제목: include · 전방 선언 남은 후보
옛번호: C-13
왜 필요한가: `GameObjectManager.h` 의 값 멤버 서브시스템 4 개(`PrimitiveRegistry` · `ComponentRegistry` · `CameraRegistry` · `AnimationSystem`)를 `unique_ptr` 로 바꾸는 안은 보류입니다(2026-10-05 측정 기준 Physics/* · Navigation/* 이 헤더 무게의 대부분이었음). 이득은 `ninja -t deps` 전후 TU 수로 판정합니다.
남은 것: `SceneNavigation` · `ScenePhysics` · `SceneAudio` 를 씬 서비스(`ISceneService`)로 옮겨 코어 헤더가 Physics · Audio · Navigation 을 퍼뜨리지 않게 합니다([EnginePartition](EnginePartition.md) 2 단계). OS 헤더를 `EngineMinimal.h` 에서 뺀 일은 끝났고, 리눅스 쪽 직접 include 는 CI 로 확인하며 빠진 파일이 나오면 그 파일에 include 를 더합니다.
함정: `_store` · `_transformHierarchy` · `_structuralChangeBuffer` · `_tickScheduler` 는 헤더의 인라인 · 템플릿이 써서 포인터로 못 뺍니다.

### EngineServiceInjection
제목: 씬 · UI 쪽의 엔진 창구 직접 읽기
옛번호: C-19
왜 필요한가: `SceneEngineServices` 주입(S10)과 같은 패턴이 범위 밖에 남았습니다.
남은 것: `FontService`, `SceneNavigationCooker` · `PortraitRenderer` 의 인자 없는 `Scene` 생성(`WidgetComponent` · `UISystem` · `SceneCooker` 는 끝났음). 서비스 클래스가 초기화 때 의존 서브시스템 포인터를 멤버로 받는 것(언리얼 `InitializeDependency`)은 고칠 때 함께 합니다.

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

### SerializerSilentSkips
제목: 직렬화가 조용히 넘기는 곳
옛번호: C-66
왜 필요한가: 읽기 · 쓰기가 조용히 넘기면 데이터 결함이 숨습니다.
남은 것(2026-10-20: 페이로드를 끝까지 읽었는지는 태그 스트림 두 경로 · diff · 값 복사가 `SerializerUtil::applyPropertyBinary` 하나로 보고, B 코덱 열거형 폭은 이미 열거자 범위입니다): 읽기가 실패한 필드에 앞부분이 써진 채 남습니다(컨테이너 · 구조체가 원소를 채운 뒤 끝 검사에서 실패). 엄격 읽기는 객체째 실패라 드러나지 않고, 읽지 못한 필드를 받는 컨텍스트(오브젝트 상태의 `skipsUnreadableProperties`, 소프트 읽기의 orphan 을 버리는 버전 읽기)에서만 그 필드가 반쯤 읽힌 값이 됩니다. 값을 임시에 읽고 성공할 때만 옮기는 형식(타입을 지운 임시 생성)이 필요합니다.

### WorkspaceImGuiSplit
제목: `Common/Workspace` 의 ImGui 17개를 분리한다
옛번호: C-67
왜 필요한가: `EditorModel` 경계를 이 폴더로 넓히는 선행입니다.
남은 것: `EditorAssetType.h` · `EditorContext.h` · `EditorLayoutStore.*` · `EditorRenderViews.*` 등 ImGui 를 쓰는 17개.

### FunctorLambdaCommonization
제목: 함수 객체 · 람다 공통화 2단계
옛번호: C-68
왜 필요한가: `containers` 가 남긴 후보 58 파일 80 건(`find_if` · `remove_if` · 해시 구조체)이 있습니다.
남은 것: 디렉터리별로 합니다. `GameFramework/Base` 는 끝났습니다(멤버 찾기 손 루프 34 곳, erase-remove, 단일 · 여러 키 정렬 비교자, 해시 구조체 1 개). 손 멤버 루프를 못 쓰게 하는 형식 또는 린트를 둡니다(형식으로 못 막는 이유를 적습니다).
Base 에 남긴 것: 계산한 키로 정렬하는 람다(`TimingJudge` 폭 합, `Inventory` 카탈로그 조회, `ScheduleCatalog` 인덱스 간접 정렬), `hashed_string` 키 정렬(`GameFlags`, `Crafting` 은 대소문자를 구분하는 저장 순서라 그대로), 반복자를 쓰는 `std::find`(`OnlineServiceClient` 지우기), 조건이 둘인 찾기 루프(`TimerQueue::findTimer`). `CompareUtil` 에 비교 함수를 받는 키 비교자(`hashed_string` 키용)가 생기면 `GameFlags` 를 옮깁니다.

### NameLookupAndWatchMask
제목: 이름 조회가 흩어진 곳과 감시 마스크
옛번호: C-69
왜 필요한가: `SceneDevCommands` · Sequencer 의 이름 조회가 곳곳에 따로 있습니다. `IFileWatcher` 를 쓰는 곳은 조용히 실패합니다(리눅스 감시 마스크에 `IN_ATTRIB` 없음).
남은 것: 이름 조회 한 곳으로 모으기, 감시 마스크 보강. `HUDViewModel` 의 슈터 전용 칸은 `D4-15` 가 맡습니다.

### DiskPathVsResourceId
제목: 디스크 경로와 리소스 id 가 같은 `string_view` 입구를 쓴다
옛번호: C-70
왜 필요한가: 2026-10-19 CI 실패의 근본 원인입니다. 쿠커 · 테스트가 디스크로 훑은 절대 경로를 리소스 id 입구에 넣어 Shipping(낱개 파일 불허)이 거절했습니다. `loadFromDiskFile` 계열로 고쳤으나 입구가 같아 같은 모양이 또 날 수 있습니다.
남은 것: 경계를 형식으로 표현할 방법을 찾습니다. 경계는 "런타임 읽기만 배포에서 막는다"입니다.
함정: `DiskFilePath` 형식으로 막는 안은 하지 않았습니다 — Shipping App 이 쿠킹 호스트라 디스크 입구를 컴파일에서 뺄 수는 없습니다.

### NetSettingsRemaining
제목: 넷 설정의 남은 것
옛번호: C-71
왜 필요한가: `gv_netLoopbackOnly` 도 테스트 전역 변수에서만 읽는 같은 모양입니다. Shipping 클라이언트에는 `gv_netConnectAddress` 가 없습니다.
남은 것: 위 둘을 정리합니다. `_gamePortShardCount` 는 [ServerDebug](ServerDebug.md)(옛 `D7-3` ⑤)가 맡습니다.

### InternalErasureRemaining
제목: `Internal` 소거의 남은 것
옛번호: C-72
왜 필요한가: 병합 때 작업자끼리 겹친 익명 함수 이름이 한 건 났습니다(`makeRandomSeed`).
남은 것: `-DSW_ENABLE_UNITY_BUILD` 로 익명 함수 이름 충돌을 한 번 봅니다.
함정: `DuplicateAnonymousFunction` 은 `Source/` 만 보므로 `Test/` 는 직접 확인합니다.

## 기능

### NavMeshExtensions
제목: 3D 내비메시(Recast & Detour)의 남은 것
옛번호: D1-3
왜 필요한가: `Source/Engine/Navigation/` 의 3D 내비메시에 아래가 비었습니다.
남은 것: (2) 식생을 베이크 기하로 — 레이어마다 "길을 막는가" 값이 `FoliageLayer` 에 있어야 합니다(지형 높이장은 종류별 기하 레지스트리로 들어갔음). (3) 장애물이 많고 자주 움직이면 TileCache(압축 층) — 지금은 타일을 통째로 다시 베이크합니다. (6) 다른 테스트 게임(AbilityArena 등)의 적도 내비메시로 — 행동 트리 `MoveTo` 노드가 `INavMover` 를 씁니다.

### Wayland
제목: Linux 창 계층이 X11 뿐이다
옛번호: D1-6
왜 필요한가: `Engine/Window/Linux` 가 X11 이고 Wayland 를 찾지 못했습니다.
남은 것: 창 생성 · 입력 · 디스플레이 모드 · 클립보드(옛 `D6-2` (4), [UiInputRemaining](Candidates-ui.md))가 대상입니다.
함정: 리눅스 확인은 CI 잡에서만 합니다(결정 기록 5-12).

### PhysicsExtensions
제목: 물리 확장 후보
옛번호: D1-8
왜 필요한가: 차량 물리, 부력 · 유체, 천과 캐릭터의 상호작용, 파워드 래그돌(옛 `D4-6.6` 보류와 같은 일). 쓰는 게임이 생기면 설계합니다.
남은 것: Jolt 의 플랫폼 간 결정성 옵션이 꺼져 있어(옛 `D7-8` 검토) 롤백을 물리 객체로 넓히려면 이 옵션 결정이 먼저입니다.
설계 메모: 센서 규칙(옛 `D1-2.1`, 2D 는 들어감) — 센서끼리 서로를 봅니다(box2d `sensor.c` 3.1.1). 막는 콜라이더도 센서입니다. 겹침은 고정 스텝에서만 납니다. 무버 캐스트가 센서를 거르도록 비센서 셰이프 마스크에 전용 비트를 둡니다. 결정은 [결정 기록](../09_Decisions.md) 의 `D1-2.1` 줄입니다.

## 조건부

### ModuleRenderPass
제목: 모듈이 렌더 패스를 등록하는 창구
옛번호: G-1
왜 필요한가: `FramePassContext` · 커맨드 리스트 · 트랜지언트 풀을 모듈 경계 밖으로 내야 하고, 그것은 RT 안전 계약까지 내보내는 일입니다.
착수 조건: 렌더 패스를 등록하려는 모듈이 생기면 합니다.

### CrossFileObjectReference
제목: 씬 · 프리팹 파일을 넘는 오브젝트 참조
옛번호: G-5
왜 필요한가: 지금은 파일 안에서 엔티티 `id` 로 가리키고, 파일을 넘는 데이터 · 코드가 0 입니다. 그때 오브젝트마다 영속 GUID 를 싣습니다(언리얼 `FSoftObjectPath` 의 하위 오브젝트 경로 자리).
착수 조건: 레벨 스트리밍 · 하위 레벨 · 다른 씬의 오브젝트를 가리키는 데이터가 생기면 합니다.

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

### ReflectionMaxCountConstant
제목: MaxCount 상한을 C++ 상수 하나에서 읽기
옛번호: 없음
왜 필요한가: 기믹 상태 구조체는 개수 상한을 `PROPERTY( MaxCount = 32 )` 와 C++ 상수(`kMaxSlotCount` · `kMaxNodeCount`) 두 곳에 적는다. 생성기가 상수를 키로 받지 않고(`ReflectionFormats.md` 3.8) `PropertyInfo` 에 `MaxCount` 를 읽는 메타데이터가 없기 때문이다. 두 값이 같은지는 테스트(`StateLimitsMatchDeclaredMaxCount`)가 대조하는데, 형식으로 막은 것이 아니라 게이트로 막은 것이다.
남은 것: 생성기가 `MaxCount = 상수 이름` 을 받아 해석하고 `PropertyInfo` 가 상한을 읽게 한다. 그러면 상수 중복과 대조 테스트가 사라진다. 같은 모양으로 상한을 두 번 적는 다른 구조체가 있는지 먼저 센다.
착수 조건: 상한이 있는 상태 구조체가 늘어 중복이 부담이 될 때, 또는 `ReflectionFormats` 생성기를 다시 열 때.

### TextAddressRemainingVoid
제목: 값 · 객체 주소를 아직 `void*` 로 받는 직렬화 함수를 주소 형식으로
옛번호: 없음
왜 필요한가: `SerializerUtil` 의 텍스트 · 이진 함수 열 개는 `ObjectAddress` · `ValueAddress`(`Engine/Serialization/Base/TextAddress.h`)로 옮겼는데, 아직 `void*` 를 받는 것이 남았다: `arePropertyValuesEqual` · `applyPropertyDefault` · `tryCoerceBinaryPayload`(`SchemaMigrate.h`)와 컨테이너 방문자 인터페이스. 객체 주소와 값 주소를 한 함수가 받으면 오프셋이 한 번 더 더해지는 결함이 다시 나올 수 있다(핸들 선택기에서 한 번 나왔다).
남은 것: 위 함수와 방문자의 시그니처를 주소 형식으로 바꾸고, 반대쪽 주소를 넘기면 컴파일이 안 되게 `static_assert` 테스트를 더한다.
착수 조건: 직렬화 경로를 다시 열 때, 또는 같은 모양의 주소 혼동 결함이 한 번 더 나올 때.
