# 엔진 정리 묶음 (2026-10-24)

> 상태: 측정대기 · 선행: 없음 · 영역: engine · 옛번호: C-49, C-63, C-64, C-47, C-34 · 진행: 7/7

**남은 확인(통합에서 확인):** 매니페스트 id `stable-unordered-map-test`, `game-framework-util-test`, `vector-bulk-fill-bench`, `synthetic-rig-bench-phases`, `stl-container-ci-build`(위 1절 각 항목). 전체 빌드 · `CoreTest` · `EngineTest` 는 묶음의 `debug-full-build` · `ctest-nogpu` 가 돈다. 확인이 끝나면 통합 담당이 이 문서를 닫는다.
Release 측정 둘(`ContainerBenchTest.VectorBulkFillAndCompare` 전후, `SyntheticRigBenchTest` 단계별 구간). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

`Candidates-engine.md` 의 정리 후보 중 설계 판단이나 측정이 먼저인 것을 단위로 나눠 옮긴 묶음입니다. 이름은 옛 후보 절의 이름입니다.

## 1. 끝난 것

- `ConfigManager::isWithinRange` 를 지웠습니다(2026-10-24, 호출부 없는 죽은 코드였고 범위 검사는 `readConfigXMLElement` 와 `ConfigRangeUtil` 이 한 곳 `collectRangeErrors` 로 봅니다).
- **StableAddressMap-1 · 2**(2026-10-24): `sw::unordered_map` 은 원소를 밀집 배열에 두어 삽입이 모든 원소를, `erase` 가 마지막 원소를 옮긴다. 이 위의 `&it->second` 를 오래 쥐는 자리를 전수로 봤다.
  - 매달릴 수 있던 곳은 `LiveReloadManager::_mapModule` 의 `ModuleContext*` 하나였다(연쇄 리로드가 포인터를 쥔 채 `onAfterReload` 안의 모듈 등록이 새 키를 넣을 수 있다). 형식을 `Core/Container/stable_unordered_map.h`(값을 힙에 하나씩 두고 주소를 고정, 키를 지우기 전까지 유효)로 바꿨고, 이동하던 `ModuleContext` 의 이동 연산은 없앴다 — 한 번 만든 자리에서 산다.
  - 나머지는 반환 즉시 쓰는 조회였다(`GlyphCache::findOrAddGlyph` 는 "다음 호출까지만 유효" 가 계약, `CultureTable::findCulture` · `SourceStringTable::findEntry` · `AbilityCatalog::findAbility` · `MonsterCatalog::findMonster` · `RemoteConfig::findEntry` · `ChatService::ensureMember` ·
    `MemoryEphemeralDatabase::findValue` · `PostgresDriver::findOrPrepare` · `FrameResourceRegistry` · `SerializeContext` 처리기 조회 · `HarfBuzzTextShaper` 글꼴 조회). 카탈로그류는 로드가 끝난 뒤 읽기 전용이고, 나머지는 한 호출 안에서 쓴다.
  - 정본 규칙은 `unordered_map.h` 머리 주석 5-1 이다: 다른 삽입 · 삭제를 거쳐 주소를 쥐어야 하면 이 맵이 아니라 `stable_unordered_map`. 주소를 내주는 API 를 없앨 수는 없어(반복자의 `operator->`) 이름이 다른 형식으로 갈랐다.
    `unordered_map<…, unique_ptr<…>>` 로 같은 일을 손으로 하던 자리(`ContentBrowserLogic::_mapChildFolder` 등)는 동작이 같아 옮기지 않았다.
- **VectorAliasLoops**(코드 끝, 측정 대기): 별칭 문제는 1 바이트 타입(`char` 는 무엇과도 별칭)의 저장이 벡터 멤버 `_pData` 와 겹쳐 보이는 곳이다. 다른 컨테이너의 블록 루프는 `DynamicBitset` 뿐이었고 이미 고쳤다. `sw::vector` 의 채우기(`assign( count, value )` ·
  `resize( count, value )`)는 `fillConstructRangeInternal` 하나로 모아 버퍼 포인터를 지역으로 뜨게 했고, `operator==` 도 두 포인터를 지역으로 뜬다. 읽기만 하는 비교 루프는 원래 별칭 문제가 없어 이득이 없을 수 있다 — 측정 케이스
  `ContainerBenchTest.VectorBulkFillAndCompare`(`vector<uint8>`/`vector<uint32>` 채우기 · 비교)를 이 변경 전후의 바이너리로 번갈아 3 판 돌려 이득이 없는 쪽은 되돌린다.
- **StlContainerConfig**(2026-10-24): 결정을 [결정 기록](../09_Decisions.md) 5-4 의 "`SW_ENABLE_STL_CONTAINER` 가 구성마다 바꾸는 것은 컨테이너 기본 할당자 하나" 줄에 적었다(옵션 설명이 원래 "엔진 할당자 대신 `std::allocator`" 라서 목적을 할당자로 읽었다 — 표준 컨테이너 비교가 목적이면 사용자가 되돌린다).
  `Memory.h` 에 `ContainerAllocator<T>` 별칭을 두고, `vector` · `array` · `pair` · `map` · `set` · `span` 은 두 구성 모두 자체 구현, `string` · `list` · `deque` 는 두 구성 모두 `std::*` 를 상속한 경합 탐지 클래스(기본 할당자만 구성별)로 했다.
  `InlineAllocator` 도 두 구성에서 같다. STL 구성에서만 건너뛰던 `TestVector` · `TestDataStructure` · `TestMemory` 의 케이스 열 곳은 두 구성에서 같이 돈다. 남은 STL 건너뜀은 할당자에 기대는 `MemoryTagTest` 셋뿐이다.
  `string` · `list` · `deque` 를 `std::*<T, 할당자>` 별칭으로 줄이지 않은 것은 경합 탐지(디버그)를 잃기 때문이다.
- **ReadNameAudit**(2026-10-24): 호출부 317 곳을 가족별로 봤다. `StateArchiveUtil::readName`(약 110 곳)은 `[[nodiscard]] bool` 이라 모든 호출부가 실패를 받는다. Engine 의 `RigXMLReader::readName` · `CharacterDataReader::readName`(약 130 곳)은 `bRequired` 를 호출부가 적는다.
  근본 원인은 `XMLReadUtil::readName` 이 빈 이름으로 "적지 않음" 과 "잘못 적음" 을 가르지 못해 필수 이름의 빈 값 확인을 잊으면 조용히 지나가는 것이었다. `readName` 을 `readOptionalName` 으로 바꿔 필수가 이 이름으로 읽히지 않게 하고,
  `GameDataXML::readRequiredName`(`[[nodiscard]]`, 원소 · 속성 이름과 함께 오류) · `AppearanceXMLUtil::readRequiredName`(보고서에 오류)을 더해 `GameDataXML::readName` 35 곳 · `AppearanceXMLUtil::readName` 약 100 곳이 모두 필수 · 선택을 고르게 했다.
  실제로 조용히 지나던 곳: 타이밍 창의 `grade`, 어빌리티 이펙트의 `<Execution class>` · 어빌리티 세트의 이름 없는 `<Param>` · `<Attribute>` · `<Ability>` · `<Effect>` 항목, 퀘스트 단계 id · 목표 kind/target · 분기, 연결(`AreaGraph::<Link>`)의 끝점,
  외형의 `<Morph>` · `<Material>` · `<HideRegion>` 이름, 캐릭터 값의 `name`/`option`. 이름 목록(`GameDataXML::appendNameList` · `CharacterDataReader::readNameList` · `RigXMLReader::readNameList`)과 `RigXMLReader::readName` 은
  글 `none` 이 빈 이름이 되어 조용히 들어가던 것을 오류로 알린다.
  경로 글로 바꿔 쓰는 `.c_str()` (빈 이름의 `c_str()` 는 "None")는 형식으로 막지 못한다 — 이름 읽기 창구에서 막은 상태 그대로다.
- **EditorCommandIdForm**(2026-10-24): 에디터 명령 id 의 선언 형식은 이미 있었다(`SW_EDITOR_COMMAND( 이름, "id", … )` 25 곳과 원본 표 `_s_arrCommandRow` 줄 `{ "id", "라벨", … }`). 실행 중 등록(`EditorCommandRegistry::registerCommand`)은 이 선언을 옮기는 쪽일 뿐이라
  형식을 더 바꾸지 않고 린트가 그 선언을 읽게 했다: `CheckNameReferences` 의 `NameReference/EditorCommand` 가 시나리오 `<DevCommand line="editor <id>">` 의 id 를 두 선언에서 줍은 id 와 견주고, 증거 조각 한 개를 더했다(`CheckLintsAreAlive` 통과).
  첫 실행에서 `emptyscene.scenario.xml` 이 `scene.new` 를 개발 명령으로 불러(`editor scene.new` 여야 한다) 실행 때야 드러날 결함이 잡혀 고쳤다. 남은 이름 참조 — 입력 액션 기본값은 `NameReference/InputAction`(`k…Action` 상수)이,
  나머지 카탈로그 안 id 는 에셋 검증 규칙(`Config/Editor/AssetValidationRules.json` 의 `interaction-ids` · `appearance-preset-ids` · `camera-preset-ids` · `ability-set-ids` 등)이 이미 본다.
- **AnimationUpdateAnomalies**(계측 끝, 측정 대기): 세 이상(본 150 → 300 의 2.5 배, 후처리 리그 300 본의 p99 3 배, 네 발 60 본의 실행 사이 흔들림)은 Release 측정이 있어야 원인을 가른다. 가를 수 있게 `AnimationSystem::evaluate` 의
  `BasePose` · `Attachment` · `PostProcess` · `SkinPalette` 단계마다 프로파일 구간(`GT.Animation.basePose` · `attachment` · `postProcess` · `skinPalette`)을 두었고, `SyntheticRigBenchTest` 가 가장 느린 프레임의 번호를 로그에 남긴다
  (처음 몇 프레임에 몰리면 첫 접근 비용, 흩어지면 스케줄러 · 판 사이 흔들림). 판 사이 흔들림은 이 기계의 일반 잡음(프레임 8 ~ 12 ms)일 수 있어 [결정 기록](../09_Decisions.md)의 `crowdBuckets` 항목과 같은 방법(전후를 번갈아 5 판 이상)으로 본다.
  원인이 나오면 그 단계의 코드를 고치는 것이 이 단위의 나머지다.
