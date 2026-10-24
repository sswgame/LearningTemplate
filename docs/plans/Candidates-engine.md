# 엔진 후보

> 상태: 후보 · 선행: 없음 · 영역: engine · 옛번호: B-11, B-13, C-4, C-7, C-22, C-25, C-34, C-47, C-49, C-63, C-64, C-65, C-68, C-69, C-71, C-72, D1-3, D1-4, D1-6, D1-8, G-17, H-2 · 진행: 해당없음

엔진 구조 · 물리 · 내비게이션 · 모듈 영역에서 아직 착수하지 않은 일의 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다. 순서는 [로드맵](Roadmap.md)이 가집니다.

## 확인 대기

### IntellisenseAnnotationCheck
제목: 어노테이션 키 완성을 IntelliSense 에서 손으로 확인
옛번호: B-11
왜 필요한가: `ReflectionMacros.h` 는 `__INTELLISENSE__` 에서도 풀이(`ReflectionIntellisense.h`)를 읽습니다. 풀이는 MS 호환 모드의 dependent base 찾기 · `_Pragma( "clang diagnostic" )` · static_assert 안의 제네릭 람다 지역 클래스에 기대므로, Visual Studio IntelliSense(EDG)가 오류 없이 읽는지 봐야 합니다.
못 하는 이유: 사람이 Visual Studio 를 열어 눈으로 봐야 합니다(풀리는 조건: VS 를 쓰는 사람의 손 확인).
남은 것: 자동 확인 길이 없습니다(DTE `ErrorList.ErrorItems` 로는 IntelliSense 오류가 나오지 않음). 손 확인 — VS 로 저장소 폴더를 연다 → `CameraComponent.h` 의 `PROPERTY( ` 안에서 Ctrl+Space 로 키 목록을 본다 → 오류 목록(IntelliSense 만)에서 어노테이션 헤더에 새 오류가 없는지 본다. 오류가 나면 조건에서 `__INTELLISENSE__` 를 뺍니다.

### StructureMoveTests
제목: 구조 리팩토링 이동(S12 ~ S22)의 남은 테스트
옛번호: B-13
왜 필요한가: 측정은 2026-10-13 에 끝나 회귀가 없었지만([검증과 측정](../08_Verification.md) 2절), 테스트가 남았습니다.
남은 것: S21 · S22 는 Debug 빌드 · 링크까지만 확인했습니다. `-L nogpu`(`NavMesh*` · `UserSettingsFileTest` · `AppHeadlessTasksTest` · `EditorFolderListing` 테스트), Shipping(단일 백엔드 정적 링크, `SW_PROFILE_COMPILED` 꺼짐), Linux(GLX 컨텍스트 · 다중 상속 경고), `OpenGLRHICommandList.cpp`(compile_commands 에 없어 문법 검사도 못 했음), 에디터 실기동(Present 훅), `assertdialog.scenario.xml`, `-gv_standardInputCommands`.
실행 대기(통합): 매니페스트 id `ctest-nogpu`, `assertdialog-scenario`, `standard-input-commands`. Linux · `OpenGLRHICommandList.cpp` 는 CI, Shipping 은 Shipping 빌드 한 번, 에디터 실기동(Present 훅)은 창 실행이라 GPU 확인 때 같이 봅니다.
함정: S18 의 새 클래스는 ImGui 에 의존해 단위 테스트가 없습니다 — 에디터 자체 테스트 · `AppScenarioTest` 에디터 케이스가 봅니다.

### MultiGamePackVerification
제목: 게임 여럿 빌드 · 선택 실행의 네 백엔드 확인
옛번호: C-25
왜 필요한가: 게임 여럿을 빌드 한 번에 짓고 실행할 때 고르며 키트는 필요한 것만 싣는 구조입니다(2026-10-13 사용자 확정). 결정은 [결정 기록](../09_Decisions.md) 5-2, 쓰는 법은 `Source/Games/README.md` 입니다.
남은 것: 새 호스트 테스트 `AppScenarioTest.EveryGamePackScenarioPassesOnEveryBackend` 를 네 백엔드로 아직 안 돌렸습니다(DX12 직접 실행으로 10 시나리오 통과만 확인).
실행 대기(통합): 매니페스트 id `game-pack-scenarios`(네 백엔드 DX12 · DX11 · Vulkan · GL). 통과하면 이 절을 지웁니다.

### NavMeshExtensions
제목: 3D 내비메시 확장(막는 식생 · AbilityArena 적 · 장애물 캐시)의 실행 확인
옛번호: D1-3
왜 필요한가: 셋 다 코드는 들어갔고(2026-10-22) 빌드 · 실행 확인이 남았습니다. 막는 식생은 `FoliageLayer::_navBlockRadius`(`FoliageNavGeometry.cpp`), AbilityArena 적은 프리팹의 `NavMeshAgentComponent`, 장애물 캐시는 에이전트 종류의 `_obstacleMode = TileCache`(`Recast/RecastTileCache.*` — 타일 바이트가 높이 층 묶음이 되고 장애물은 `INavMesh::addCacheObstacle`)입니다.
남은 것: 테스트 `FoliageTest.BlockingLayerAddsNavObstaclePerInstance` · `NavMeshTileCacheTest.*` 를 돌린다. AbilityArena `ai.scenario.xml` · `control.scenario.xml` 이 내비메시 이동으로도 통과하는지 본다. 장애물 캐시의 상자 요 부호(`NavMeshObstacleComponent::makeCacheObstacle` 의 atan2 와 Detour `addBoxObstacle` 의 `yRadians`)가 맞는지 돌린 직사각 장애물로 본다. 장애물이 많은 장면에서 다시 굽기와 캐시의 `GT.Navigation.*` 시간을 견준다 — 층은 압축하지 않는다(저장 압축기, 메모리는 타일마다 층 크기 × 3 바이트).
실행 대기(통합): 매니페스트 id `foliage-navmesh-tests`, `ability-arena-ai-control-scenarios`, `navmesh-obstacle-yaw-and-time`. 상자 요 부호 확인과 `GT.Navigation.*` 시간 견주기는 직사각 장애물 장면 · 측정이 필요해 시나리오가 없으면 테스트부터 만듭니다.

### StorageOwnershipLeftovers
제목: 저장소 소유 정리의 잰 뒤에 정할 것
옛번호: 없음
왜 필요한가: 종료 전용 해제 함수는 모두 없앴습니다(규칙은 `Core/Memory/LifetimeScope.h` 머리 주석). 아래는 재 봐야 고칠지 정해지는 것입니다.
못 하는 이유: (1) · (2) 는 기준선 측정(종료 보고 · 버퍼 성장)이 먼저라 지금 정할 수 없습니다 — 자라지 않으면 아무것도 바꾸지 않는 것이 답입니다.
끝낸 것: (3) `GimmickNodeRegistry::getDefault` 는 `ScopeOwned<DefaultRegistryHolder>` 로 바꿨습니다. 처음 쓰는 쪽이 기준선 뒤에 세워도 엔진 종료가 범위를 닫을 때 종료 보고 전에 사라지므로, 기동 단계(`ModuleTypes`)에서 미리 세우던 `TypeRegistrar`(`ded0a6fd6`)는 지웠습니다. 확인은 종료 누수 테스트(`AppSmokeTest.ShutdownReturnsEveryTagToTheBaseline`, 보통 · 에디터 실행) 통과입니다.
남은 것: (1) `BinarySerializer` 의 `thread_local` 버퍼 넷 · `ShaderParameterBinder` 의 `s_cbBytes` — 기준선 뒤에 자라는지 먼저 잰다(자라면 스레드 슬롯 · 바인더가 소유). (2) `LiveReloadManager::clearContexts` · `ModuleDependencyGraph::clear` 의 표 용량 — 소유자 소멸이 종료 보고 뒤인지 본다.
함정: `SceneTransformStorage::get` · `ResourcePaths` 경로 캐시는 엔진 수명 범위가 닫힌 뒤 쓰면 멈추거나(트랜스폼) 캐시하지 않는다(Debug 단언). 한 프로세스에서 엔진을 두 번 세우는 테스트 호스트가 생기면 이 계약이 먼저 걸린다.

## 정리

설계 판단이나 측정이 먼저인 정리 후보(StableAddressMap · VectorAliasLoops · StlContainerConfig · 이름 읽기 전수 · 에디터 명령 id 형식 · AnimationUpdateAnomalies)는 [EngineCleanups](EngineCleanups.md) 의 단위가 되었습니다.

### FunctorLambdaCommonization
제목: 함수 객체 · 람다 공통화 2단계
옛번호: C-68
왜 필요한가: `containers` 가 남긴 후보 58 파일 80 건(`find_if` · `remove_if` · 해시 구조체)이 있습니다.
남은 것: 디렉터리별로 합니다. `GameFramework/Kits` 는 끝났습니다(2026-10-21, `wt/kitclean` — `erase( remove_if )` 9 곳은 `VectorUtil::removeIf`, 손 멤버 찾기 18 곳은 `findBy` · `containsBy`. `Feature/Online` 도 끝났습니다: 2026-10-22 `wt/onlineclean` — 값 찾기 8 곳은 `VectorUtil::contains`, 멤버 찾기 9 곳은 `findBy` · `containsBy`, 값 지우기 3 곳은 새 `VectorUtil::removeValue`). `GameFramework/Base` 도 끝났습니다(2026-10-21, `wt/baseclean` — 멤버 찾기 손 루프 34 곳, erase-remove, 단일 · 여러 키 정렬 비교자, 해시 구조체 1 개). `Editor/Common` 도 끝났습니다(2026-10-21, `wt/editorcommon` — `VectorUtil::removeIf` · `findBy`). 손 멤버 루프를 못 쓰게 하는 형식 또는 린트를 둡니다(형식으로 못 막는 이유를 적습니다). 2026-10-22 `wt/onlineclean` 에서 Engine · Games · Editor · Core 의 `std::find` 손 찾기 약 110 곳과 손 루프 약 130 곳, erase-remove 15 곳도 바꿨습니다(새 도우미 `findIndex` · `removeFirst` · `removeValue` · `findIf` · `containsIf`). 남은 손 루프는 목록이 C 배열 · 맵이거나 원소가 포인터인 것입니다. `std::remove_if` 는 끝났습니다(2026-10-21, `wt/gatesleft` — 남은 Engine · Core · Editor/Panels · Test 14 곳을 `VectorUtil::removeIf` 로 옮기고, 표준 함수라 형식으로 막을 수 없으므로 `CheckStdRemoveIf` 게이트가 `VectorUtil.h` 밖의 `std::remove_if` 를 막습니다). 손 멤버 루프(`find_if`)를 막는 형식 · 린트와 해시 구조체는 남았습니다.
Base 에 남긴 것: 계산한 키로 정렬하는 람다(`TimingJudge` 폭 합, `Inventory` 카탈로그 조회, `ScheduleCatalog` 인덱스 간접 정렬), `hashed_string` 키 정렬(`GameFlags`, `Crafting` 은 대소문자를 구분하는 저장 순서라 그대로), 반복자를 쓰는 `std::find`(`OnlineServiceClient` 지우기), 조건이 둘인 찾기 루프(`TimerQueue::findTimer`). `CompareUtil` 에 비교 함수를 받는 키 비교자(`hashed_string` 키용)가 생기면 `GameFlags` 를 옮깁니다.
`Editor/Common` 에 남은 손 루프는 조건이 둘 이상이거나(`_id == id && _pInstance != nullptr`) C 배열(`kArrKindInfo`)이라 `findBy` 로 옮기지 않았습니다.
이름 붙은 지역 람다 정리: Core · Base · PlugIn · UI · Input · Games 는 끝났습니다(`wt/lambdacore` — 함수로 뺀 것 `readFloat4`→`UIStyleFieldTable::storeFloat4`, `compareOpen` 셋→`GridOpenEntry::isWorse`, `loadAction`→`InputMap::loadActionNode` 외 8 곳. 캡처가 본질인 짧은 것은 남김).
끝난 곳: `Source/Editor/Panels/Inspector` 의 손으로 쓴 erase-remove 2건(`EditorPropertyGrid`)을 `VectorUtil::removeIf` 로 바꿨다(2026-10-21).
이름 붙은 지역 람다 Editor 끝(2026-10-23, wt/lambdaeditor): 20줄 넘는 것 5개를 정적 함수로 뺐다. 나머지(캡처가 본질이고 13줄 안쪽)는 그대로 둔다.
이름 붙은 지역 람다 Kits 끝(2026-10-23, wt/lambdakits): 유닛 · 손님 id 목록 읽기 · 쓰기, 조우 후보 순회, 0 채우기, 단계 비교자 5 군을 파일 익명 namespace 정적 함수로 옮겼다. 카탈로그 `is`(이름 비교) 11 곳은 공용 위치가 `GameDataXML`(Base)이라 남겼다.
이름 붙은 지역 람다 Engine/Graphics 끝(2026-10-24, `wt/lambdagfx`): 해제 큐 장부 해제 `RHIReleaseQueue::enqueueLedgerRelease` 계열, 메시 정점 생성기, 바인딩 계약 표, Vulkan 로드 · 저장 동작 변환을 함수로 뺐다. 캡처가 본질인 5줄 안쪽 람다(`recycleCb`, `waitForQuery`, `setRootDescriptor`, `nextUnit`, `uploadOne`)는 남겼다.
이름 붙은 지역 람다(Engine Renderer · Destruction · Animation · Automation 외, 2026-10-24 `wt/lambdaeng`): 검증 실패 보고 `fail` 2곳은 `FractureGraphUtil::reportInvalid`, 패킷 쓰기 `pushUint32` · `pushString`, 장면 팔레트 `appendPalette`, 시나리오 `appendSlot`, `RenderGraph` 의 `request` · `touch` 는 함수로 뺐습니다. 캡처가 본질인 짧은 것(Water `vertexAt`, Profiler `findThread` · `findSlot`, FacialRig, RenderPipelineAsset 등)과 형식이 다른 `addIfInside` 2곳은 남겼습니다.
이름 붙은 지역 람다 Kits 끝(2026-10-23, wt/lambdakits): 유닛 · 손님 id 목록 읽기 · 쓰기, 조우 후보 순회, 0 채우기, 단계 비교자 5 군을 파일 익명 namespace 정적 함수로 옮겼다. 카탈로그 `is`(이름 비교) 11 곳은 공용 위치가 `GameDataXML`(Base)이라 남겼다. → 2026-10-24 `wt/kitshelpers`: 11 곳을 `GameDataXML::isElement` 로, ID 목록 읽기 · 쓰기를 `StateArchiveUtil::writeIDList` · `readIDList` 로 합쳤다(RTS 는 `uint64` 묶은 id 목록).
이름 붙은 지역 람다 Serialization·Reflection·Object 끝(2026-10-24, wt/lambdaser): 쌍 처리기와 도우미는 함수로 뺐고, 캡처가 본질인 짧은 것만 남겼다.

### ValidationIssueLogWorldOwned
제목: `ValidationIssueLog` 를 엔진 수명 범위의 창구에서 월드(게임 오브젝트 매니저) 소유로 옮기기
옛번호: 없음
왜 필요한가: `ValidationIssueLog::get()` 은 `ScopeOwned` 로 엔진 수명 범위가 소유하는 표의 편의 창구라서 단일체 예외 표에 `ScopeOwnedHotPath` 로 남았습니다(2026-10-24 판정 — 유지). 그러나 이 표의 내용은 "출처(오브젝트)마다의 검증 결과" 라 월드가 둘이면(에디터 미리보기 창 · 씬 둘) 결과가 한 표에 섞입니다. 언리얼 Map Check 는 월드별 메시지 로그입니다.
남은 것: 표를 `GameObjectManager` 의 서브시스템(또는 `Scene` 이 든 값)으로 옮기고, 파괴 경로(`GameObject` 소멸자)는 자기 매니저의 표를 직접 부릅니다. 맵 검사 패널은 활성 씬의 표를 읽습니다. `find()` 의 "엔진 범위가 닫혔으면 nullptr" 는 사라지고 `get()` 호출 9 곳(`MapCheckPanel` · `ObjectValidation` · `EditorSelfTestPanelCases`)이 소유자를 받는 모양이 됩니다. 옮긴 뒤 `CheckServiceAccess.toml` 의 `ValidationIssueLog` 줄을 지웁니다.

### ReflectedInstanceOwner
제목: 리플렉션이 만든 형식 소거 인스턴스의 소유 형식
옛번호: 없음
왜 필요한가: `wt/ownership`(2026-10-24)이 맨 `sw_new` 를 `make_unique` 로, 거친 `Memory::allocate` 블록을 `MemoryBlock` 으로 바꾸고 `Style/RawNew` · `CheckRawAllocation` 이 새 맨 할당을 막게 했습니다. 남은 틈은 외부 라이브러리 할당 콜백과, 리플렉션이 생성자를 불러 만든 인스턴스(블록 + `TypeInfo::_destroyInstance` 짝)입니다. 후자는 `void*` 를 맨 손으로 들고 `destroyInstance` 를 손으로 짝지어 부르는 모양이라 누수 · 이중 해제가 형식에 안 보입니다.
남은 것: 블록과 `const TypeInfo*` 를 함께 쥐고 소멸자가 `_destroyInstance` 후 블록을 푸는 이동 전용 소유 형식을 `Engine/Reflection` 에 두고 `CheckRawAllocation.toml` 의 `ProjectSettingsDocument.cpp`(`_pCurrent` · `_pDefault` · `createInstance` 호출 8곳) · `UIDocumentLoader.cpp`(`createWidget`)를 그 형식으로 옮긴 뒤 두 줄을 지웁니다. `GameObject.cpp` 의 컴포넌트 저장소는 풀 항목이 짝이라 따로 봅니다. `hashed_string.h` 의 청크 표(`atomic<Entry*>`)는 락 없이 읽는 표라 소유 형식을 담지 못하니, 표가 소유하는 `atomic` 소유 칸 형식이 필요한지 함께 따집니다.

## 기능

### LowLevelReportingGaps
제목: 로그 경로의 조용한 유실 두 곳과 낮은 티어의 로그 사용을 빌드 구조로 막기
옛번호: 없음
왜 필요한가: `SW_LOW_LEVEL_*` 정리(`wt/lowlevellog`, 2026-10-25)에서 같은 계열의 틈 셋이 남았습니다. (1) `SW_LOG_ASSERT` 는 멈추기 전에 stderr 에 동기로 쓰지 않아 비동기 큐가 크래시 직전 메시지를 잃습니다(`SW_ASSERT` 와 `SW_LOW_LEVEL_ASSERT` 는 이미 먼저 씁니다). (2) `SW_LOG_*` 는 싱크가 없으면 줄을 조용히 버립니다(`Logger::writeLogGlobal`) — 폴백을 두지 않는다는 원칙에 어긋납니다. (3) 낮은 티어(Memory · Container · Math)가 `SW_LOG_*` 를 쓰지 못하는 것은 린트 규칙(`CheckCoreLayers`)만이 막습니다.
남은 것: (1) `SW_LOG_ASSERT` 의 Debug 분기에서 `printAssertFailure` 처럼 stderr 에 쓰고 flush 한 뒤 로그로 넘깁니다. (2) 싱크가 없을 때 `writeLogGlobal` 이 stderr 로 남길지, 오류로 멈출지 정합니다. 로그 줄은 기동 순서가 이르면 싱크가 없는 것이 정상이라, 기동 때 한 번 알리는 방식도 비교합니다. (3)은 코드로 들어갔습니다(`Core_low_objects` + `pchLow.h`, `wt/lowtier`) — 통합이 Ninja-Debug · Release · Debug-Cold · 정적 Shipping · CI-Debug-STL 의 Core · Engine · ReflectionParser 링크를 확인하면 이 절에서 (3)을 지웁니다. 린트 규칙은 낮은 티어 헤더가 위 티어 pch 로 컴파일되는 TU 에 포함돼 컴파일이 못 막으므로 남깁니다(결정 기록).


## 조건부

### PerfConditionalCandidates
제목: 측정해서 이기면 하는 성능 후보 묶음
옛번호: C-7
왜 필요한가: 조건부 후보의 묶음입니다 — 병렬 틱 임계값의 교차점, GameObject 레이아웃, 적응형 틱 임계값, 스폰 비용(~1.1 us, 잠금 여섯), 시퀀서 성능 수치.
착수 조건: 측정해서 이기면 합니다. TickItem 인라인 재시도는 오브젝트의 틱 부기 49 B 를 먼저 줄여야 합니다.
남은 것: 무버 일괄 틱의 다음 단계는 무버 데이터를 타입 배열(SoA)로 옮겨 일괄 함수가 컴포넌트를 읽지 않게 하는 것(대상 씬 컴포넌트의 칸 번호를 원소에) — 일괄 틱은 `GT.Frame` p99 0.75 배로 들어갔고 더 줄일 필요가 보이면 합니다. 파괴 잎 셰이프는 쿠킹 때 Jolt 셰이프를 `.fracture` 에 싣는 안(Chaos 가 지오메트리 컬렉션에 충돌을 같이 굽는 자리)을 Physics 에 셰이프 직렬화 함수가 생기면 다시 봅니다.

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
