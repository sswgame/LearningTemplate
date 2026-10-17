# 백로그: 엔진 구조, 물리, 내비게이션, 모듈

엔진과 GameFramework 의 구조 정리, 오브젝트와 씬, 물리와 내비게이션, 핫 리로드와 모듈의 남은 일입니다. 상태 태그의 뜻, 일을 고르는 순서, 새 항목을 쓰는 법은 [백로그 안내](README.md)에 있습니다.

- `A-19` [결함] **일반 `BinarySerializer` · JSON · XML 읽기가 `PROPERTY( Min · Max )` 범위 검사를 하지 않는다(`A-15` 잔여).** `SerializerUtil::collectRangeErrors` 는
  원하는 호출부(`ConfigManager` · `ReflectedXMLFile` · `GameDataXML` · `FightingMatch`)만 부르므로, 다른 경로로 읽은 값은 선언된 범위를 벗어나도 성공으로 돌아온다.
  뿌리: 범위 계약을 읽기 경로마다 따로 지킨다. 읽기 공통 경로(포맷과 무관한 역직렬화 끝)에서 한 번 검사하고 호출부의 개별 호출을 지운다.

- `A-20` [결함] **`SW_ENABLE_STL_CONTAINER` 로 뜻이 바뀌는 다른 컨테이너 별칭을 전수 점검한다.** `vector<bool>`(`A-18`)처럼 `map` · `set` · `unordered_*` 도
  구성에 따라 반복자 무효화 규칙과 원소 주소 안정성이 다를 수 있다. 기본 구성 구현이 주는 보장(노드 주소 안정, 삽입 뒤 반복자 유효)에 기대는 호출부를 찾아,
  두 구성이 같은 계약을 갖게 하거나 계약이 다른 별칭을 쓰지 못하게 막는다(`DynamicBitset` 처럼 형식으로).

- `B-11` [검증대기] **어노테이션 키 완성을 Visual Studio IntelliSense(EDG)에서 손으로 확인하지 못했다.** `ReflectionMacros.h` 는 `__INTELLISENSE__` 에서도 풀이(`ReflectionIntellisense.h`)를 읽는다.
  풀이는 MS 호환 모드의 dependent base 찾기 · `_Pragma( "clang diagnostic" )` · static_assert 안의 제네릭 람다 지역 클래스에 기댄다 — EDG 가 오류 없이 읽는지 본다.
  DTE `ErrorList.ErrorItems` 로는 IntelliSense 오류가 나오지 않아 자동 확인 길이 없다. 손 확인: VS 로 저장소 폴더를 연다 → `CameraComponent.h` 의 `PROPERTY( ` 안에서
  Ctrl+Space 로 키 목록을 본다 → 오류 목록(IntelliSense 만)에서 어노테이션 헤더에 새 오류가 없는지 본다. 오류가 나면 조건에서 `__INTELLISENSE__` 를 뺀다.

- `B-13` [검증대기] **구조 리팩토링 이동(S12 ~ S22)의 테스트가 남았다**(측정은 2026-10-13 에 끝나 회귀가 없었다 — [검증과 측정](../08_Verification.md) 2절).
  - **S21 · S22 는 Debug 빌드 · 링크까지만 확인했다**: `-L nogpu`(`NavMesh*` · `UserSettingsFileTest` · `AppHeadlessTasksTest` · `EditorFolderListing` 테스트),
    Shipping(단일 백엔드 정적 링크, `SW_PROFILE_COMPILED` 꺼짐), Linux(GLX 컨텍스트 · 다중 상속 경고), `OpenGLRHICommandList.cpp`(compile_commands 에 없어 문법 검사도 못 했다), 에디터 실기동(Present 훅),
    `assertdialog.scenario.xml`, `-gv_reloadGameAtFrame`, `-gv_standardInputCommands`. S18 의 새 클래스는 ImGui 에 의존해 단위 테스트가 없다 — 에디터 자체 테스트 · `AppScenarioTest` 에디터 케이스가 본다.

- `C-1` [정리] **엔진 분할 — 계획은 [docs/plans/EnginePartition.md](../plans/EnginePartition.md).** `Engine.dll` 을 티어 경계로 나누기 전에 의존 방향의 약한 고리를 푼다. 단위를 끝내면 계획 문서에서 지운다.

- `C-4` [정리] **GameFramework 구조 정리(2026-10-04 리뷰, 사용자 승인).** 남은 것 —
  - 중간: Overworld `TileMap` 의 칸 손셈(`indexOf` · `isInBounds` — 크기는 파일 스키마 `TileMapXMLData` 가 든다) · `Habitat`(CreatureLife) · NetConnection 메시지 버퍼 재사용 ·
    게임 손셈(HarvestValley `FarmCropComponent` · `FarmSoilComponent`, NileCity `NileDirectorComponent` 의 `index % getWidth()`)은 키트가 `getTopology()` 를 열면 같이.

- `C-5` [정리] **GameFramework 구조 리뷰에서 남은 것(2026-10-05 — 공통 모듈 · 디렉터 베이스 · 층 게이트는 끝남).**
  ⑤ `Engine.cpp` 의 절반이 기동 단계 구조체(낮음).

- `C-7` [정리] **조건부 후보 묶음.** 병렬 틱 임계값의 교차점 · GameObject 레이아웃 · 적응형 틱 임계값 · 스폰 비용(~1.1 us, 잠금 여섯) · 시퀀서 성능 수치. TickItem 인라인
  재시도는 오브젝트의 틱 부기 49 B 를 먼저 줄여야 한다. 측정해서 이기면 한다. 무버 일괄 틱의 다음 단계는 무버 데이터를 타입 배열(SoA)로 옮겨 일괄 함수가 컴포넌트를 읽지 않게 하는 것(대상 씬 컴포넌트의 칸 번호를 원소에) — 일괄 틱은 `GT.Frame` p99 0.75 배로 들어갔고 더 줄일 필요가 보이면 한다. 파괴 잎 셰이프는 쿠킹 때 Jolt 셰이프를 `.fracture` 에 싣는 안(Chaos 가 지오메트리 컬렉션에 충돌을 같이 굽는 자리)을 Physics 에 셰이프 직렬화 함수가 생기면 다시 본다.

- `C-13` [정리] **include · 전방 선언 남은 후보.** ① (끝) OS 헤더는 `EngineMinimal.h` 에서 뺐다(2026-10-11 — Win32 · POSIX API 를 쓰는 파일이 `Core/Common/PlatformOsHeaders.h` 를 직접 include,
  효과는 [검증과 측정](../08_Verification.md)). Windows 는 전 구성이 빌드로 확인됐고 리눅스 쪽 직접 include 는 CI 로 확인한다 — 빠진 파일이 나오면 그 파일에 include 를 더한다.
  ② `GameObjectManager.h` 의 값 멤버 서브시스템 4 개(`PrimitiveRegistry` · `ComponentRegistry` · `CameraRegistry` · `AnimationSystem`)를 `unique_ptr` 로 바꾸는 안은
  보류다(2026-10-05 측정 기준 Physics/* · Navigation/* 이 헤더 무게의 대부분이었다. `_store` · `_transformHierarchy` · `_structuralChangeBuffer` · `_tickScheduler` 는
  헤더의 인라인 · 템플릿이 써서 포인터로 못 뺀다). 이득은 `ninja -t deps` 전후 TU 수로 판정한다. `SceneNavigation` · `ScenePhysics`(겹침 월드 포함) · `SceneAudio` 는
  씬 서비스(`ISceneService`)로 옮겨 코어 헤더가 Physics · Audio · Navigation 을 퍼뜨리지 않는다(엔진 분할 2 단계).

- `C-19` [정리] **구조 리팩토링의 남은 후보.**
  - **씬 · UI 쪽의 엔진 창구 직접 읽기**: `SceneEngineServices` 주입(S10)과 같은 패턴이 범위 밖에 남았다 — `WidgetComponent`(`engine::findSubsystem<UISystem>()`) · `UISystem` · `FontService`,
    `SceneCooker` · `SceneNavigationCooker` · `PortraitRenderer` 의 인자 없는 `Scene` 생성. 서비스 클래스가 초기화 때 의존 서브시스템 포인터를 멤버로 받는 것(언리얼 `InitializeDependency`)은 고칠 때 함께 한다.

- `C-22` [정리] **작은 정리 후보(2026-10-13 병합 검토에서 모음).** `IPhysicsScene3D/2D` 전방 선언용 `*Fwd.h`, Core → Log 예외 다섯 파일(티어마다 필요한 것이 달라 함수 포인터 하나로 풀리지 않는다), 프렐류드 예외 제거(184 파일).

- `C-30` [정리] **유지보수성을 위한 데이터화(2026-10-15 `wt/datadriven`)의 남은 것.** 기준과 모양은 [결정 기록](../09_Decisions.md) 5-4 "유지보수성을 위한 데이터화", 새 줄은 `CheckDataDrivenLiterals` 가 막는다(전체는 `[baseline]`).
  ① **데이터 속성의 리터럴 기본값 · 조용한 자르기가 904 곳 남았다**(기준선 `AttributeFallback`)(`py -3 Scripts/lint/gate/CheckDataDrivenLiterals.py --list`). 많은 순: WesternCatalog 64 · AsymmetricHorrorRules 51 · CameraPreset 43 · WitcherCatalog 41 ·
  ConquestCatalog 39 · ScavengerCatalog 39 · MechCatalog 38 · BrCatalog 33 · HorrorCatalog 32 · ActionPlatformerCatalog 29. 원소마다 리플렉션 Def(초기값 = 기본값, `Required`, `Min`/`Max`)를 두고 `GameDataXML::readFields` 로 바꾸며,
  데이터 속성 이름이 칸 이름과 다르면(`xp` ↔ `_experience`) 데이터를 다시 쓴다. 고칠 때마다 기준선을 낮춘다. `MonsterCatalog` 의 Stats · AI 원소는 `MonsterDef` 한 구조체라 하위 Def 로 나눠야 한다(필드 사용 130 곳).
  ② 게임 코드의 실수 리터럴 줄 약 700(constexpr 아닌 것), NileCity 자동 계획표의 좌표(27 · 37 · 집 줄 — 도로 x · 농장 x 만 PROPERTY), HarvestValley 작물 → 모델 표, StarSkirmish AI 성향 표(`makeAISettings`), FarmSun 빛 곡선. `TuningConstexpr` 기준선은 게임 · 키트의 나머지 실수 상수다.
  ③ `GameSettings::getCustomPropertyFloat( key, fallback )` 처럼 호출부가 기본값을 넘기는 API 는 스키마 선언으로 바꿀 후보다.
  ④ 에디터 색 · 아이콘 표(`EditorComponentIcon.cpp`)와 에디터 UI 크기 리터럴은 디자인 토큰 작업(editorux 묶음)이 맡는다.
  ⑤ 배포본(Shipping)은 벤치 · 자동 플레이 스위치가 없어 `AppSmokeTest.BenchFrameMatchesGoldenImage` 와 `QaGoldenImages`(캡처 인자가 버려지면 건너뜀)를 건너뛴다(Debug · Release 는 그대로). 배포 구성의 그림 비교가 필요하면 구운 게임의 팩 시나리오 스크린샷으로 기준을 둔다.

- `C-25` [정리] **게임 여럿을 빌드 한 번에 짓고, 실행할 때 고르고, 키트는 필요한 것만**(2026-10-13, 사용자 확정). 결정은 [결정 기록](../09_Decisions.md) 5-2, 쓰는 법은 `Source/Games/README.md`.
  남은 것: 새 호스트 테스트 `AppScenarioTest.EveryGamePackScenarioPassesOnEveryBackend` 를 네 백엔드로 아직 안 돌렸다(DX12 직접 실행으로 10 시나리오 통과만 확인).

- `C-34` [정리] **이름 참조 감사(2026-10-15)의 남은 것.** 데이터 쪽은 에셋 검증 `component_reference` 규칙, 코드 · 시나리오 쪽은 린트 `CheckNameReferences` 가 본다. 아직 정적으로 못 보는 것:
  ① 입력 액션 이름의 생성자 기본값(`PawnComponent` 의 `Move` · `Look`, 카메라 액션)은 게임마다 맵이 달라 검증 함수로 못 본다 — 빙의 · 프리셋 때 런타임 경고가 본다.
  ② 카탈로그 안 id 참조는 팩 둘(`starskirmish-unit-tree` · `nilecity-goods`)만 규칙이 있다 — 나머지 키트 카탈로그는 로더 경고(스키마 테스트)에 기댄다.
  ③ 시나리오 `<DevCommand line="editor <명령>">` 의 에디터 명령 id(`EditorCommandRegistry` 는 실행 중 등록), UI 바인딩 경로(`UIBindingSet::bind` 오류 로그), 동적으로 지은 액션 이름은 런타임 경고 게이트(logwarn)에 맡긴다.

- `C-36` [정리] **데이터 주도 구조 정리(2026-10-16, 사용자 지시 "원천적인 문제 해결").** 6 영역 조사가 경로 리터럴 약 200곳 · 조용한 폴백 약 60곳 · 이중 정본 약 30곳을 드러냈다. 뿌리 원인 · 형식으로 막는 해법 · 작업 묶음 W0~W8 은 [데이터 주도 구조 정리 계획](../plans/DataDriven.md).
  기준은 "게이트는 최후의 수단" — 위반을 쓸 수 없는 형식(`DataPath` · `Optional` · `[[nodiscard]]` 로더 결과)이 먼저고 린트 `CheckDataDrivenStructure` 는 틈을 지키는 안전망이다. 진행 표는 그 게이트의 `--summary`.

  읽기가 옛 키를 별칭으로 찾는 경로(`TypeInfo::_mapNameToPropertyWithBase` 의 별칭 항목)와 테스트 `ReflectionSerializationTest.StructuralMoveAndPropertyAlias` 의 별칭 절이 씁니다.
  쓰는 곳을 세어 데이터를 새 이름으로 다시 쓰고 별칭을 없앱니다([데이터 주도 구조 정리 계획](../plans/DataDriven.md) 의 Alias 항목과 같은 일). 값 계산이 필요한 경우만 `SW_SCHEMA_MIGRATION` 입니다.

- `C-40` [정리] **Shipping 에서 빠지는 API 를 부르는 쪽마다 `#if !defined( SW_SHIPPING )` 을 기억해야 한다.** `TypeRegistry::unregisterTypesByModule` 등을 테스트가 부르면
  Shipping 컴파일이 깨지고(리눅스 CI 에서 실제로 깨짐, `b206e2a26`), 가드를 잊은 곳은 Shipping 빌드에서야 드러난다.
  테스트 전용 RAII 도우미(등록 범위 객체)로 감싸거나 늘 컴파일되는 API 로 바꿔 호출부가 구성을 몰라도 되게 한다.

- `C-47` [정리] **합성 리그 벤치가 드러낸 애니메이션 갱신의 비정상 셋.** 기준표는 [검증과 측정](../08_Verification.md) "영역별 측정값"의 `SyntheticRigBenchTest` 표(2026-10-17, Release).
  (1) 캐릭터 1000 에서 본 150 → 300 이 2.5 배로 선형(2 배)보다 크다 — 캐시 · 워커 분배를 본다. (2) 본 300 + 리그 노드의 p99 40.6 ms 가 p50 의 3 배다 — 히치 원인을 찾는다.
  (3) 네 발 60 본이 실행마다 흔들린다(캐릭터 1000 p50 1223 ~ 3162 us). GPU 스키닝 시간은 런타임 .mesh 가 인덱스 없는 삼각형 목록이라 벤치 씬에 싣지 못해 재지 않았다.

- `C-49` [정리] **`sw::vector` 의 `_list[i]` 루프는 쓰기가 벡터 내부를 가려(별칭) 벡터화되지 않는다**(2026-10-17 `DynamicBitset` 측정에서 확인 — 원시 포인터 루프로 고쳤다).
  다른 컨테이너의 블록 루프와 `sw::vector::operator==` 에 같은 모양이 있을 수 있다. 호출부와 이득을 Release 로 잰 뒤에 한다.
- `C-51` [정리] **열거형 이름 캐시의 낮은 위험 둘**(2026-10-17 `reflect7ab` 검토). ① `EnumNameCache::find`(`EnumName.cpp`)가 `_pInfo` 와 `_generation` 을 따로 저장해, 두 스레드가 세대 경계에서 겹쳐 쓰면 새 세대에 옛 포인터가 붙을 수 있다.
  저장 순서만 바꿔서는 막히지 않으므로 쌍을 한 원자 값(세대를 담은 불변 항목 포인터 하나)으로 바꾼다. ② `EnumInfo::_pArrDenseName` 이 원자가 아닌 포인터라 핫 리로드 해제 중 렌더 · 오디오 · 로거 스레드가 `getEnumName` 을 부르면 위험하다.
  같은 뿌리로 `TypeLookupCache` 도 세대를 보지 않아 엔진을 다시 만들면 해제된 `TypeInfo` 를 가리킨다 — 타입 캐시 전부가 세대 하나를 보게 한다.

- `C-52` [정리] **타입별 버킷(7-c)과 타입 핸들(7-d) 뒤의 남은 정리**(`docs/plans/TypedComponents.md` 7절). ① 핸들 옮기기(`ObjectStateBatch::resolveObjectReferences`)가
  컴포넌트 타입의 최상위 `PROPERTY` 만 봐서 리플렉션 구조체 필드 안의 `GameObjectHandle` 은 다시 매겨지지 않는다 — 구조체와 컨테이너를 재귀로 내려가는 값 방문으로 바꾼다.
  ② `ComponentStateStore` 가 다른 실행의 세이브를 같은 타입 안의 열거 순서로 맞춘다 — 오브젝트 저장 id 와 `ComponentStableKey` 로 맞춘다.
  ③ `ComponentRegistry` · `TickBatchRegistry` 가운데 버킷으로 충분한 목록을 옮긴다. 기준은 등록 순서가 결과에 나오는가, 채널이 있는가, 잠금 없이 읽어야 하는가이다.
  ④ 측정(Release, VSync 끔, 3~5판, p50 · p99): 오브젝트 1만 · 10만 × 타입 수 50 · 200 · 500 의 `forEachComponentOfType`, 추가 · 제거 잠금, `sizeof( Component )` 전후와 큐브 20,000 개 `GT.Frame`,
  개발 빌드의 `resolveComponent<T>` 확인 비용.

- `D1-2` [기능] **강체 물리의 다음 조각.** 겹침 월드(`PhysicsOverlapWorld` · `BoxCollider2DComponent`)는 강체 씬과 따로 돈다 — 키트의 투사체 · 근접 판정이 Box2D 센서로 옮겨 가면 겹침 월드를 제거한다.

- `D1-3` [기능] **3D 내비메시(Recast & Detour, `Source/Engine/Navigation/`)의 남은 것.** (2) 식생을 베이크 기하로 — 레이어마다 "길을 막는가" 값이 `FoliageLayer` 에 있어야 한다
  (지형 높이장은 종류별 기하 레지스트리로 들어갔다) (3) 장애물이 많고 자주 움직이면 TileCache(압축 층) — 지금은 타일을 통째로 다시 베이크
  (6) 다른 테스트 게임(AbilityArena 등)의 적도 내비메시로 — 행동 트리 `MoveTo` 노드가 `INavMover` 를 쓴다.

- `D1-4` [기능] **천(`ClothComponent`, Jolt 소프트 바디 `IPhysicsSoftBodyScene`)을 그리는 동적 메시가 없다** — 정점은 물리 프레임마다 `getVertices` 로 나오고 지금은 물리 디버그 선으로만 보인다.
  그래픽스 쪽 동적 정점 버퍼 메시(정점 · 법선을 프레임마다 다시 올림)가 있어야 한다. 헤어 카드 · 옷처럼 스킨드 메시 뼈에 고정하는 길(Jolt `Skinned` 구속)도 아직 없다.

- `D1-5` [기능] **중첩 프리팹 뒤에 남은 것**(형식 · 스폰 · 순환 오류 · 쿠킹 펼치기는 Object README "중첩 프리팹"). 씬에서 부품을 직접 고친 값은 저장되지 않는다 —
  씬 인스턴스에 부품 오버라이드를 싣는 길(유니티 nested override). 에디터 쪽 연결(부품 고르기 · 연결 끊기 · 바깥 프리팹에 적용). `PrefabInstanceRegistry` 의 기록은 오브젝트가 없어져도
  씬을 비울 때까지 남는다(되돌리기 · 플레이 종료 복원 때문) — 오래 도는 씬의 정리 시점을 정한다.
  세 조각 모두 에디터의 프리팹 편집 경로(Apply Overrides · `adoptChildPrefabInstances`, 되돌리기 기록의 수명)와 같은 기록을 읽고 쓰므로 그 작업과 한 묶음으로 한다(2026-10-17, 작업자 enginefeature 가 겹침 때문에 남김).

- `D1-6` [기능] **Linux 창 계층이 X11 뿐이다 — Wayland.** `Engine/Window/Linux` 가 X11 이고 Wayland 를 찾지 못했다. 리눅스 확인은 CI 잡에서만 한다(결정 기록 5-12). 창 생성 · 입력 · 디스플레이 모드 · 클립보드(`D6-2.3`)가 대상이다.

- `D1-7` [기능] **월드 스트리밍 · 월드 파티션이 없다.** 씬 단위 로딩만 있고 큰 월드(지형 + 식생 + 도시)를 구역별로 올리고 내리는 구조가 없다. 텍스처 밉 스트리밍(`D4-7.5`)은 별도. 언리얼 World Partition · HLOD, 유니티 Scene Streaming 에 해당한다. 먼저 설계 문서를 쓴다(`design-reviewer` 검토). 구역 격자와 에셋 의존 · 네트워크 관심 영역(`D7-8`)과의 접점, 쿠킹 단위, 에디터 UI.

- `D1-8` [기능] **물리 확장 후보.** 차량 물리, 부력 · 유체, 천과 캐릭터의 상호작용, 파워드 래그돌(`D4-6.6` 보류와 같은 일). 쓰는 게임이 생기면 설계한다. Jolt 의 플랫폼 간 결정성 옵션이 꺼져 있어(`D7-8` 검토) 롤백을 물리 객체로 넓히려면 이 옵션 결정이 먼저다.

- `G-1` [조건부] **모듈이 렌더 패스를 등록하는 창구가 없다.** `FramePassContext` · 커맨드 리스트 · 트랜지언트 풀을 모듈 경계 밖으로 내야 하고, 그것은 RT 안전 계약까지
  내보내는 일이다. 쓰는 모듈이 생기면 그때.

- `G-5` [조건부] **씬 · 프리팹 파일을 넘는 오브젝트 참조**(조건: 레벨 스트리밍 · 하위 레벨 · 다른 씬의 오브젝트를 가리키는 데이터가 생기면). 그때 오브젝트마다
  영속 GUID 를 싣는다(언리얼 `FSoftObjectPath` 의 하위 오브젝트 경로 자리). 지금은 파일 안에서 엔티티 `id` 로 가리키고, 파일을 넘는 데이터 · 코드가 0 이다.

- `G-17` [조건부] **macOS(Metal RHI) · 모바일 · 콘솔 백엔드가 없다.** RHI 는 DX12 · Vulkan · DX11 이다. 콘솔은 SDK 계약이 필요하다. 해당 플랫폼으로 내는 게임이 생기면 한다.

- `H-2` [보류] **(보류 — 사용자 결정 2026-10-03) `hashed_string` 에 FName 숫자 꼬리를 둘지.** 지금은 비교 · 표시 인덱스 두 칸(8 바이트)이라 `"Enemy_12"` · `"Enemy_13"` 이
  이름 표에 각각 영구 적재된다. 런타임에 번호 붙은 이름을 대량으로 만드는 경로(복제 · 스폰 이름 자동 부여)가 생기면 다시 본다 — 넣으면 `_숫자`(앞자리 0 제외)를
  떼어 정수 칸에 두고 비교는 (인덱스, 숫자) 쌍.

---

[◀ 백로그 안내](README.md)
