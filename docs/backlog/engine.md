# 백로그: 엔진 구조, 물리, 내비게이션, 모듈

엔진과 GameFramework 의 구조 정리, 오브젝트와 씬, 물리와 내비게이션, 핫 리로드와 모듈의 남은 일입니다. 상태 태그의 뜻, 일을 고르는 순서, 새 항목을 쓰는 법은 [백로그 안내](README.md)에 있습니다.

- `A-13` [결함] **자기 기술형 바이너리가 모르는 값 타입을 1 바이트 건너뛰고 성공으로 읽는다.** `SerializerUtil::deserializeValueBinary` 는 핸들러 · 열거형 · 타입 레지스트리
  어디에도 없는 타입이면 1 바이트를 넘기고 성공을 돌려준다(`SerializerUtil.cpp` 의 마지막 분기). 이유를 담은 읽기 오류로 바꾼다. 고정 배치형은 표 밖의 타입에 코덱을
  만들지 않는 형식으로 막았다([리플렉션 직렬화 형식 계획](../plans/ReflectionFormats.md) 1.4 · 8.2). 쓰는 쪽도 같은 타입을 무엇으로 쓰는지 함께 본다.
  실제로 걸린 경우(2026-10-17, 단계 5-b): 생성기는 프로퍼티 타입의 별칭은 벗기지만 컨테이너 원소의 별칭은 벗기지 않아, `vector<AccountID>` 의 원소가 모르는 이름 `AccountID` 로
  등록되고 자기 기술형 왕복에서 원소가 조용히 빠졌다. 저장소 레코드 두 칸(`MaintenanceWindow::_listAllowedAccount`, `PartySnapshot::_listMemberID`)은 원소를 같은 타입 `uint64` 로 적어 피했다.
  뿌리는 `AstVisitor.cpp` 의 컨테이너 원소 이름(`fillContainerArgs`)이 `resolvePropertyAlias` 를 거치지 않는 것이므로 생성기와 이 읽기 오류를 함께 고친다.

- `A-15` [결함] **`PROPERTY( Min · Max )` 를 고정 배치형 코덱만 지킨다.** 자기 기술형 읽기(`BinarySerializer` · JSON · XML)는 범위 밖 값을 그대로 넣는다.
  범위를 보는 `SerializerUtil::collectRangeErrors` 는 설정 JSON · 데이터 XML 로드만 부르고, 기본값(`RangeCheckScope::FieldsOnly`)은 컨테이너 원소를 보지 않는다.
  지금은 `FightingMatch::readState` 만 `IncludeSequenceElements` 로 직접 부른다(2026-10-17). 뿌리는 범위 계약을 읽기 경로마다 따로 지킨다는 것이다.
  자기 기술형 읽기 공통 경로에서 원소까지 보게 하려면, 먼저 지금 에셋 · 세이브 가운데 범위 밖 값이 있는지 전수로 세고(쿠커나 테스트로), 고친 뒤 기본값을 바꾼다.

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

- `G-1` [조건부] **모듈이 렌더 패스를 등록하는 창구가 없다.** `FramePassContext` · 커맨드 리스트 · 트랜지언트 풀을 모듈 경계 밖으로 내야 하고, 그것은 RT 안전 계약까지
  내보내는 일이다. 쓰는 모듈이 생기면 그때.

- `G-5` [조건부] **씬 · 프리팹 파일을 넘는 오브젝트 참조**(조건: 레벨 스트리밍 · 하위 레벨 · 다른 씬의 오브젝트를 가리키는 데이터가 생기면). 그때 오브젝트마다
  영속 GUID 를 싣는다(언리얼 `FSoftObjectPath` 의 하위 오브젝트 경로 자리). 지금은 파일 안에서 엔티티 `id` 로 가리키고, 파일을 넘는 데이터 · 코드가 0 이다.

- `H-2` [보류] **(보류 — 사용자 결정 2026-10-03) `hashed_string` 에 FName 숫자 꼬리를 둘지.** 지금은 비교 · 표시 인덱스 두 칸(8 바이트)이라 `"Enemy_12"` · `"Enemy_13"` 이
  이름 표에 각각 영구 적재된다. 런타임에 번호 붙은 이름을 대량으로 만드는 경로(복제 · 스폰 이름 자동 부여)가 생기면 다시 본다 — 넣으면 `_숫자`(앞자리 0 제외)를
  떼어 정수 칸에 두고 비교는 (인덱스, 숫자) 쌍.

---

[◀ 백로그 안내](README.md)
