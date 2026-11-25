# AssetLoading

에셋 경로 · 리소스 신원, 리소스를 읽는 입구, 데이터 파일 칸 읽기의 규칙. 상속 · 조건 · 입력 맵 묶음은 [DataInheritance](DataInheritance.md).

## 경로 · 리소스 신원 (R1 ~ R9)

코드 주석이 `(R2)` 처럼 이 번호를 가리킨다.

| 규칙 | 뜻 |
|---|---|
| R1 OS 경계 하나 | 파일 · 폴더 · 라이브러리 · 환경 변수 · 실행 파일 OS 호출은 `PlatformFileUtil`(과 짝)만. `std::filesystem` 은 쓰지 않는다 |
| R2 디스크 I/O 는 절대 경로만 | `FileUtil` · `PlatformFileUtil` I/O 는 절대 경로만 받는다(상대 · `C:foo` · `oo` 는 오류). 경로 글 연산은 대상 밖 |
| R3 리소스는 리소스 경로만 | `AssetPaths` · `AssetIO` 는 `ResourcePath{도메인(선택), 상대 경로}` 만 받는다. 문자열에서 도메인을 추측하지 않는다 |
| R4 쓰기는 FileUtil 이 절대 경로로 | `AssetPaths` · `AssetIO` 는 읽기와 경로 풀기만 |
| R5 상대 → 절대는 경계에서 한 번 | 명령줄 · 설정 · 콘솔 · 시나리오 값은 들어오는 자리에서 기동 때 고정한 루트(`ProcessRoots`) 기준으로 한 번. 실행 중 작업 폴더는 읽지 않는다 |
| R6 폴백 없음 | 필요한 값이 없으면 오류로 멈춘다 |
| R7 확인과 실행은 같은 해석 | 있는지 묻는 쪽과 읽고 쓰는 쪽이 같은 함수로 푼다 |
| R8 같은 일은 하나 | 같은 판정 · 정규화 · 표 · 파서는 하나. 답을 아는 쪽이 값을 넘긴다 |
| R9 구성 차이는 크기대로 | [CppStyle](CppStyle.md) "Build configuration switches" |

- 디스크 입구는 `AbsolutePath`(`tryMake` · `tryResolve`, `FileUtil::requireAbsolutePath`)로 받는다 — `FileUtil::isAbsolutePath` 는 `C:foo` 도 절대로 본다.
- 루트는 `ProcessRoots` — `"Saved"` 리터럴 · 작업 폴더 의존 금지. 사용자 폴더는 `UserDataPath`.
- `ResourcePath` 는 문자열에서 암시적으로 못 만든다. 도메인 판정은 `DomainTable::split` 하나. 명령줄 디스크 경로는 `CommandLineDiskPath`, 개발 명령은 `readDiskPathArgument`.
- 런타임 게임 코드는 디스크 경로 · `Content/` 의 `FileUtil` 읽기를 쓰지 않는다 — `AssetIO` · 에셋 캐시. 데이터로 정한 경로는 `DataPath`.

## 리소스 읽기 입구

- **종류 등록**은 `SW_ASSET_TYPE( id, 정의.withPathRule( … ).withLoader( AssetTypeLoader::of<T>( 메모리 태그 ) ) )` 한 줄, 공유 캐시는 `SW_SHARED_ASSET_CACHE`(Engine) · `SW_MODULE_SHARED_ASSET_CACHE`(모듈) 한 줄. 검증 전용 로더를 두지 않는다 — 스키마 테스트도 진짜 로더를 부른다.
- `request( 경로 )` 는 핸들, `acquire( 경로 )` 는 `SharedAssetRef<T>`. `XMLCatalog` 에 감싸개를 두지 않는다. 다시 읽은 횟수는 `AssetReloadWatch<T>` 로만 본다(`_seen…ReloadCount` 칸 금지).
- 새 에셋 캐시의 `reload` 는 `ReloadResult` 를 돌려준다(`void` 는 등록이 컴파일되지 않는다 — 강제). 실패면 옛 판을 둔다.
- 다시 읽으면 새 판이 생긴다. 진행 중 세션 · 워커 일감 · 번호를 지어 둔 쪽은 `shared_ptr<const T>`(`.snapshot()`)로 쥐고, 지어 둔 것은 `AssetReloadWatch` 로 다시 짓는다.
- **사본을 만들지 않는다**: `loadFromAsset` 은 비공개. 게임 범위 서비스는 `GameInstance::loadRequiredCatalog`, 테스트 · 미리 보기는 `loadFromXMLText`, 쿠커가 읽는 형식만 `loadCopyFromAsset`. 사본이 맞는 것은 기동 때 서비스 상태로 옮기는 엔진 설정, 쓰는 쪽이 고치는 입력(`InputMap` · `NavMeshAsset` 등), 도구 입력뿐.
- 공유 객체는 `const`. 실행 상태가 섞인 카탈로그는 정의와 실행으로 나눈다(`BehaviorTree` · `BehaviorTreeRunner`).
- **리소스는 id 로 읽는다**: `ConfigManager::readComposedXMLDocument` · `XMLReadUtil::parseResourceRoot` · `XMLDocument::loadResource` · `loadResourceAs` · `parseResourceText`. 읽은 글을 id 없이 글 입구로 넘기는 것은 린트 `common-shapes` `assetEntrance` 강제.
- **다른 에셋을 가리키는 칸은 `AssetRef<T>`** — 스스로 읽지 않는다(`acquire()` · `request()`), 접미사가 다르면 로드 오류. 칸이 만들어지는 곳에서 `T` 는 완전해야 한다. 종류가 없는 파일 · 폴더는 `DataPath`, 한 칸이 두 종류를 가리키면 칸을 나눈다(글 칸에 `AssetPath` · `AssetType` 을 다는 것은 파서 오류). 경로 칸은 리플렉션 선언에서만 온다(생성 목록 `Config/Dev/AssetReferenceFields.generated.json`, 낡음은 `--check-asset-refs`).
- **쿠킹은 닿는 것만 담는다**(`PackReachability`). 코드가 경로를 조립해 읽는 파일은 뿌리(`_cookRules` · `<RequiredData>` · `SW_ASSET_COOK_ROOT`)나 곁 파일(`SW_ASSET_COMPANION`)로 선언한다 — 아니면 배포에서 빠진다.
- 에셋 정체성은 GUID, 내용 변경은 내용 해시 — 한 값에 두지 않는다. 형식 판 올림으로 GUID 를 넣지 않는다(`asset-ids --fix` 가 쓴다). 사람은 `.meta` · 머리를 만지지 않는다. 저장 입구를 지나지 않는 `FileUtil::writeTextFile` 쓰기는 머리를 잃으니, 생성물 바이트 비교 검사가 있는 도구는 `AssetGUIDHeader::carry` 를 쓴다.
- 에셋 옮기기 · 지우기는 `move-asset`([FolderLayout](FolderLayout.md) "콘텐츠").

## 데이터 파일 읽기

- 데이터 파일의 칸은 `PROPERTY` 구조체 + `XMLFields` · `JSONFields::readProperties` 로 읽는다. 손 XML 읽기는 `XMLReadUtil`(`loadRootOrWarn` · `findRequiredID` · `readRequiredName` · `parseNameList` · `parseFloat3` · `readOptionalNumber` · `readOptionalFlag`), 곡선 `GameDataXML::readFloatCurve`, 카탈로그 루트 원소 `XMLCatalogTable`(`readInDocumentOrder` · `readInTableOrder`) — 키트마다 읽기 사본을 두지 않는다. 직렬화는 `XMLSerializer` · `JSONSerializer` · `BinarySerializer` 를 리플렉션으로(Serialization 밖 pugixml · JSON 라이브러리 호출 금지).
- 손으로 읽는 것은 다음뿐: 바깥 표준 · 원문을 지키는 것(VRM · glTF · TMX · JWT · 영수증 · 트레이스 · CSS 철자 스타일 시트 등), 양이 많거나 적었는지가 뜻인 칸(타일맵 타일, 문화권 표, `<Requires>` 같은 조건 원소), 고유 문법(`ScheduleCondition`, 이름=수 목록, 비트 합치는 낱말 칸, 격자 글), `TagContainer`(`addTag` 불변식), 키가 동적인 JSON, Core 층. 새로 손으로 읽으려면 이 목록 가운데 어디에 드는지 주석에 적는다.
- 실행 상태 저장 · 복원은 `SW_DECLARE_STATE_IO()` + `serializeState( StateIO& io )` 하나(린트 `common-shapes` `statePair` 강제).
- **카탈로그 참조 엄격도**: 같은 파일 필수 id 연결은 로드 · 쿠킹 실패, 다른 팩 · 코드가 채울 연결은 `optional="true"` 경고, 코드 등록 이름 오류는 로드 실패, 다른 파일 카탈로그 연결은 둘을 쥐는 쪽이 시작 때 검사, 원소 자기 칸 오류는 그 원소 제외. 참조 검사는 `Content/engine/settings/datacatalogreferences.xml` 의 `<Reference>` 줄 한 벌(로더 · 쿠커가 같은 줄을 돈다 — 카탈로그마다 `find( id ) == nullptr` 루프 금지). 카탈로그 항목을 가리키는 컴포넌트 · 씬 칸은 `CatalogKey<EntryDef>` + `<CatalogKey>` · `<Field>` 줄(테스트 `FieldTableMatchesReflection`).
