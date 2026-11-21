# DataInheritance

> 결론: 데이터 · 리소스 파일과 경로의 규칙. 상속은 루트에 `_base="<리소스 id>"`(카탈로그 항목은 `_base="<id>"`)를 적고 다른 칸만 쓴다 — 형식마다 합치기 코드를 쓰지 않는다(리플렉션 형식 `PROPERTY( InheritKey )`, 손 XML 형식은 스키마, 카탈로그는 설정 표 한 줄). 모든 리소스 글은 `ResourceComposer::compose` 한 길(부모 → 조건 → 덮어쓰기 층)을 지나고 런타임은 쿠킹이 펼친 결과만 읽는다. 경로는 R1 ~ R9, 다른 에셋을 가리키는 칸은 `AssetRef<T>`.

## 파일에 적는 법

| 자리 | 규칙 |
|---|---|
| 속성 | 자식이 적은 것만 덮는다(빈 값 `attr=""` 도 적은 것) |
| 구조체 칸 | 안으로 들어가 같은 규칙. 두 번 적으면 오류(`FieldRepeated`) |
| 열쇠 있는 목록 | 원소의 `InheritKey` 칸으로 합친다: 작업 없음 = 있으면 합치고 없으면 끝에 더함, `_op="Replace"` · `_op="Remove"`(없는 열쇠는 오류), `_before="X"` · `_after="X"`(둘 다 적으면 오류). 위에서 아래로 차례로 얹는다 |
| 열쇠 없는 목록 · 글 값 | 자식이 적으면 통째로 바뀐다. 빈 원소(`<_listInput />`)는 비운다 |

- 순환 · 없는 부모 · 형식이 다른 부모 · 깊이 초과(`_dataInheritanceMaxDepth`)는 로드 실패 — 로드 강제. 부모 없이 자식 칸만으로 읽지 않는다.
- 열쇠를 잘못 적으면 새 원소가 더해진다 — 빈 칸은 그 형식의 검증이 잡으니 새 형식은 검증을 꼭 둔다.

## 새 형식을 올리는 법

1. `SW_ASSET_TYPE` 한 줄(아래 "리소스 읽기 입구"). 다른 리소스를 가리키는 칸은 `AssetRef<T>`.
2. 리플렉션 형식: 목록 원소의 이름 칸에 `PROPERTY( InheritKey )`(본보기 `RenderGraphPassDesc`). **Engine 형식도 문서 표에 한 줄**(`<Document root="…"><Field name="_listStage" key="_name"/></Document>`) — 팩 쿠커는 게임 모듈 전에 돌아 리플렉션을 읽지 않는다. 빠지면 `DataInheritance.NoSchema`, 코드와 다르면 `ResourceComposerTest` 가 진다.
3. Engine 손 XML 형식: `SW_DATA_INHERITANCE_SCHEMA` + `ResourceComposer::compose`(본보기 `MaterialXML::makeInheritanceSchema`). 게임 · 키트 손 형식은 문서 표 줄만으로 된다. DevTools 가 처음 `SW_DATA_INHERITANCE_SCHEMA` 를 쓰는 커밋에서 `DataInheritance.h` 의 `SW_REGISTRAR_MODULE == "Engine"` 단언을 `DataInheritance::isCookVisibleModule( string_view )`(Engine · DevTools 만 참)로 바꾼다.
4. 칸 덮어쓰기로 안 풀리는(차이가 쌓이는) Engine 형식만 `SW_DATA_INHERITANCE_MERGE`.
5. 카탈로그(한 파일에 항목 여럿)는 `Content/engine/settings/datacataloginheritance.xml` 에 `<Entry name key>` 한 줄 — 읽는 코드는 고치지 않는다.
6. 조건 · 동작은 등록된 어휘를 쓴다(아래 "행동 어휘"). 칸을 더하면 실행이 읽고 시나리오가 효과를 확인한다 — 읽히지 않는 칸은 넣지 않는다.
7. 핫 리로드가 있는 종류는 `_base` 칸을 `AssetReferenceUse::InlinedAtLoad` 로 둔다(`AssetReloadCascade` 가 자식을 다시 읽는다).

## 저장

- **부모와의 차이로 쓴다** — 펼친 한 벌을 쓰면 `_base` 가 끊긴다. 리플렉션 형식 `ReflectedXMLFile::saveDescAsChild` · `saveDesc`, 손 형식 `ResourceComposer::saveDelta`, 카탈로그 `DataCatalogInheritance::makeFileDelta` + `validateDeltaReadsBack`. 차이로 적을 수 없는 편집은 오류이고 부모 없는 한 벌로 쓰는 대체는 두지 않는다.
- 새 형식의 저장은 `FileUtil::writeTextFile` 이 아니라 `ResourceComposer::saveText` 를 쓴다 — 조건(`_when`)을 지킨다.
- 부모(없으면 형식 기본값)와 같은 칸은 쓰지 않는다. **코드에서 기본값을 바꾸면 그 칸을 적지 않은 모든 데이터가 바뀐다** — 바뀌면 안 되는 값은 데이터에 적는다. 이름 · 경로 칸의 C++ 문자열 기본값은 `PROPERTY( Required )` 이거나 헤더 초기값 + `PROPERTY( DefaultName )`(리플렉션 파서 강제).
- 층을 얹어 읽은 리소스는 원본 자리에 쓰지 않는다(`ResourceLayer.SaveWouldBake`).

## 쿠킹

- 팩 쿠커는 `_base` 파일을 펼친 한 벌로 담는다. 쿠킹본에 `_base` 가 남으면 오류(`DataInheritance.NotFlattened`). 부모는 `AssetIO` 로 읽어 쿠킹 입력이 된다 — 의존 목록을 따로 적지 않는다.

## 조건(`_when`) · 덮어쓰기 층 · 경로 교체

```xml
<Interaction id="Use" maxDistance="3">
	<_when _if="quality.low" maxDistance="5" />
	<_when _if="!platform.windows" _op="Remove" />
</Interaction>
```

- 조건은 자기를 품은 원소 하나만 고친다(속성 · 빼기). 자식 원소를 품으면 오류 — 조건부로 더하려면 원소를 적고 반대 조건으로 뺀다.
- `_if` 는 리소스 태그(`축.값`)만 묻는다. 게임 상태 · 모르는 축은 읽기 오류. 게임 플래그는 `flag` 축 태그로 넣는다.
- 축: `platform` · `target` 은 쿠킹(쿠킹본에서 풀린다), `quality` · `locale` · `region` · `feature` · `flag` · `layer` 는 로드(태그가 바뀌면 그 리소스를 다시 읽는다).
- 부모의 조건을 지운 차이 저장은 오류(`DataInheritance.WhenDropped`).
- 덮어쓰기 층은 게임 팩 설정 `<Engine _listResourceLayer>`. 층 파일은 원본을 암묵적 부모로 가진 차이 — 층 파일에 `_base` 를 적으면 오류. 쿠킹 때 펼치지 않고 읽을 때 얹는다.
- 바이너리(텍스처 · 오디오 · 메시)는 `<Engine _resourceRemap>` 경로 교체. 사슬은 오류, 쿠킹 안에서는 바꾸지 않는다.
- **네트워크 게임플레이 값을 로드 축에 기대지 않는다** — 서버가 리소스 합성 해시를 대조해 다르면 끊는다(`Net.ResourceDigestMismatch`). `PerClient` 축(품질 · 언어)만 달라도 된다. 시뮬레이션 데이터에 쿠킹 축과 로드 축 조건을 섞지 않는다.
- 한 프로세스의 태그 문맥은 하나다(캐시 열쇠가 경로뿐). 플레이어마다 다른 값은 데이터 키로 나눈다.

## 카탈로그 항목 상속

| 적는 것 | 뜻 |
|---|---|
| 항목 `_base="<id>"` | 같은 파일 · 같은 종류 항목(뒤에 있어도 된다) |
| 항목 `_base="<리소스 id>#<id>"` | 다른 파일 항목(게임 팩이 `common` 을 부모로) |
| 루트 `_base="<리소스 id>"` | 파일 전체를 물려받고 항목을 더하거나 `_op="Remove"` |
| 규칙에 없는 자식 | 통째로 바뀐다. `<X _op="Clear"/>` 는 비운다 |

- 규칙에 없는 종류의 `_base` 는 오류(`KindNotListed`). 두 파일이 서로의 항목을 부모로 삼으면 핫 리로드 순환 오류.
- 외형 항목의 머티리얼은 메시 구간 **이름**으로 적는다(번호는 다시 임포트하면 어긋난다).
- 외형 프리셋(`CharacterAppearance` 의 `parent`)은 이 구조에 올리지 않는다(씨앗 · 쌓이는 규칙).

## 애니메이션 · 스프라이트 형식의 열쇠

| 형식 | 열쇠 | 통째로 바뀌는 것 |
|---|---|---|
| `.animset.xml` | 클립 `name` | — |
| `.animgraph.xml` | 상태 `name` · 전이 `id` · 레이어와 마스크 `name` · 주석 `id` | `blend_space` · `pose_node` · `condition` |
| `.spritelib.xml` | 분류 · 라벨 `_name` | — |
| `.rig.xml` | 대상 · 노드 `name` | 노드 안의 목록 |
| `.retarget.xml` | 사슬 `name` · 뼈 짝 `source` · 자세 `bone` | 사슬의 뼈 목록 |
| `.physics.xml` | 바디 `_bone` | 셰이프 · 충돌 쌍 |
| `.sockets.xml` · `.notifies.xml` | 소켓 · 가상 뼈 · 알림 `name`, 발 닿음 `notify` | — |

## 프리팹 변형 · 씬 조립

- 프리팹 변형은 `_base` + `<PrefabOverrides>`(씬 인스턴스와 같은 형식). `<Part path="0">` 로 물려받은 부품을 덮고 `<PrefabInstance>` 로 더한다. 물려받은 부품은 지울 수 없다.
- 씬 앵커는 엔티티 `scene="<씬>"`, 멤버는 앵커의 `<Part path="내부 id">` 로 덮는다(없는 대상은 경고하고 버린다). 씬 상속은 루트 `_base`(엔티티 id 가 열쇠). 데이터 레이어는 엔티티 `layers="A,B"`(선언하지 않은 이름은 오류).
- 내부 씬의 코드는 멤버를 이름으로 찾지 않는다(멤버 이름은 `앵커/내부`). 멤버는 지울 수 없다.
- 참조 대상의 레이어가 가리키는 쪽의 레이어에 없으면 쿠킹 실패(`SceneCooker.DataLayerCrossReference`).

## 행동 어휘와 행동 리소스 변형

행동 트리 · 스케줄 · 대화 · 어빌리티 · 기믹 회로는 실행기를 합치지 않고 조건 · 동작 이름만 공유한다.

- **조건** `ConditionExpr` — 등록 `SW_CONDITION( 이름, 칸 스키마, 함수 )`, 모르는 이름은 로드 오류. 세계는 형식 있는 문맥 칸(`GameplayContext`)으로 넘긴다. 매 프레임 값 비교(`AnimationConditionOp`) · 게임 문맥 없는 곳(`UserSettingConditionOperator`)에는 쓰지 않는다.
- **동작** `ActionRegistry` — `SW_GAME_ACTION( OpenShop, "shop.open", OpenShopParams, &openShop )`, 칸은 리플렉션 구조체, 칸 이름은 `id` · `name` · `task` 와 겹치지 않게. 어빌리티 이펙트(복제 · 예측) · 기믹 회로 노드 · 에디터 명령에는 쓰지 않는다.
- 행동 트리 변형은 노드 `id` 가 열쇠. `<SubTree tree>` 는 읽을 때 펼치고 `<SubTree slot>` 은 러너가 채운다.

## 입력 맵 묶음(레이어 쌓기 — 상속의 예외)

입력은 기능 여럿을 게임마다 골라 얹어야 해 부모 사슬이 아니라 레이어 쌓기다. 게임은 `gamesettings.xml` 의 `<inputMap path="…"/>` 줄로 기본 맵 → 공통 파일 → 게임 차이를 쌓는다(읽는 차례: 기본 맵 → 키트 맵(id 순) → 나머지 줄). 차이 파일은 `<action>`(새 액션) · `<override>`(바인딩을 통째로) · `<remove>` 를 적는다. 키트는 그 기능 `.cpp` 에 `SW_INPUT_MAP_EXTENSION( 이름, "id", "경로" )` 한 줄.

- 맵 전체 값(시간 · 거리 · 기본 레이어)은 기본 맵에만 적는다. 예외 `virtualCursor`(`common/input/virtualcursor.input.xml`, 묶음에 한 번).
- 레이어는 한 파일이 정의하고, 공통 파일은 자기 레이어를 하나 갖는다(공통 파일이 게임 키를 막지 않게).
- `<override>` 는 `layer` 를 적지 않으면 덮는 액션의 레이어에 남는다. 한 액션은 묶음에서 한 번만 덮거나 지운다.
- 같은 레이어 · 발화 규칙 · 입력의 두 액션, 없는 액션을 가리킨 차이, 깨진 묶음은 오류로 게임을 띄우지 않는다(`InputMap::loadBundle` — 게임 시작 · 쿠킹 · `InputMapBundleTest` · 패널 다시 읽기 강제).
- **액션 이름**: 두 게임 이상이 같은 뜻이면 접두어 없는 공용 이름 하나(`Move`, `Interact`; 정의 `Content/common/input/`, 코드는 `Engine/Input/Map/CommonInputActions.h`). 한 게임만 쓰면 게임 접두어(`Farm.Use`) — 다른 게임에 같은 뜻이 생기면 공용으로 옮긴다. 묶인 키가 아니라 뜻이 기준. 장치 이름공간(`Camera.Pan`)과 별도 맵 이름(`Photo.Move`, `UI.Pause`)은 접두어를 유지.
- 게임 코드는 키를 묻지 않는다. 한 공용 액션을 읽는 쪽은 하나로 정한다(디렉터 둘이 읽으면 둘 다 발화).
- 우선순위: 공통 < 키트 < 게임 차이 < 플레이어 재배치(`UserSettingsManager::reapplyAll`). 재배치 겹침은 함께 켜진 레이어까지 본다(`canLayersFireTogether`).
- 입력 맵 패널에서 게임만의 변경은 차이 파일에 `<override>` 로 적는다(공통 파일을 고치면 다른 게임도 바뀐다).

## 합치지 않은 것

- `PrefabOverrides` 합치기(열쇠가 지역 id 키, 씬 인스턴스는 사라진 대상을 경고로 넘긴다), `_listRenderPassRef`(여러 파일 믹스인 — 이름이 겹치면 오류), JSON 설정(`ConfigManager` 층), 오디오 `_parentEvent`(래퍼 안 이벤트), `AudioEffectDef` 목록(열쇠가 비는 원소), 스케줄 `<Archetype>`, `SkeletonVariant` 칸(곱해지고 쌓인다 — `Skeleton::mergeVariantText`)은 위 구조에 올리지 않는다. 같은 일을 다시 하려면 이 줄의 까닭이 사라졌는지 먼저 본다.

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
- 쿠킹이 알아야 하는 합성 규칙은 데이터(`datacataloginheritance.xml`)에 둔다 — 팩 쿠킹은 게임 모듈 전에 돈다.
- `request( 경로 )` 는 핸들, `acquire( 경로 )` 는 `SharedAssetRef<T>`. `XMLCatalog` 에 감싸개를 두지 않는다. 다시 읽은 횟수는 `AssetReloadWatch<T>` 로만 본다(`_seen…ReloadCount` 칸 금지).
- 새 에셋 캐시의 `reload` 는 `ReloadResult` 를 돌려준다(`void` 는 등록이 컴파일되지 않는다 — 강제). 실패면 옛 판을 둔다.
- 다시 읽으면 새 판이 생긴다. 진행 중 세션 · 워커 일감 · 번호를 지어 둔 쪽은 `shared_ptr<const T>`(`.snapshot()`)로 쥐고, 지어 둔 것은 `AssetReloadWatch` 로 다시 짓는다.
- **사본을 만들지 않는다**: `loadFromAsset` 은 비공개. 게임 범위 서비스는 `GameInstance::loadRequiredCatalog`, 테스트 · 미리 보기는 `loadFromXMLText`, 쿠커가 읽는 형식만 `loadCopyFromAsset`. 사본이 맞는 것은 기동 때 서비스 상태로 옮기는 엔진 설정, 쓰는 쪽이 고치는 입력(`InputMap` · `NavMeshAsset` 등), 도구 입력뿐.
- 공유 객체는 `const`. 실행 상태가 섞인 카탈로그는 정의와 실행으로 나눈다(`BehaviorTree` · `BehaviorTreeRunner`).
- **리소스는 id 로 읽는다**: `ConfigManager::readComposedXMLDocument` · `XMLReadUtil::parseResourceRoot` · `XMLDocument::loadResource` · `loadResourceAs` · `parseResourceText`. 읽은 글을 id 없이 글 입구로 넘기는 것은 린트 `common-shapes` `assetEntrance` 강제.
- **다른 에셋을 가리키는 칸은 `AssetRef<T>`** — 스스로 읽지 않는다(`acquire()` · `request()`), 접미사가 다르면 로드 오류. 칸이 만들어지는 곳에서 `T` 는 완전해야 한다. 종류가 하나로 정해지지 않는 칸만 `PROPERTY( AssetPath )`. 경로 칸은 리플렉션 선언에서만 온다(생성 목록 `Config/Dev/AssetReferenceFields.generated.json`, 낡음은 `--check-asset-refs`).
- **쿠킹은 닿는 것만 담는다**(`PackReachability`). 코드가 경로를 조립해 읽는 파일은 뿌리(`_cookRules` · `<RequiredData>` · `SW_ASSET_COOK_ROOT`)나 곁 파일(`SW_ASSET_COMPANION`)로 선언한다 — 아니면 배포에서 빠진다.
- 에셋 정체성은 GUID, 내용 변경은 내용 해시 — 한 값에 두지 않는다. 형식 판 올림으로 GUID 를 넣지 않는다(`asset-ids --fix` 가 쓴다). 사람은 `.meta` · 머리를 만지지 않는다. 저장 입구를 지나지 않는 `FileUtil::writeTextFile` 쓰기는 머리를 잃으니, 생성물 바이트 비교 검사가 있는 도구는 `AssetGUIDHeader::carry` 를 쓴다.
- 에셋 옮기기 · 지우기는 `move-asset`([FolderLayout](FolderLayout.md) "콘텐츠").

## 데이터 파일 읽기

- 데이터 파일의 칸은 `PROPERTY` 구조체 + `XMLFields` · `JSONFields::readProperties` 로 읽는다. 손 XML 읽기는 `XMLReadUtil`(`loadRootOrWarn` · `findRequiredID` · `readRequiredName` · `parseNameList` · `parseFloat3` · `readOptionalNumber` · `readOptionalFlag`), 곡선 `GameDataXML::readFloatCurve`, 카탈로그 루트 원소 `XMLCatalogTable`(`readInDocumentOrder` · `readInTableOrder`) — 키트마다 읽기 사본을 두지 않는다. 직렬화는 `XMLSerializer` · `JSONSerializer` · `BinarySerializer` 를 리플렉션으로(Serialization 밖 pugixml · JSON 라이브러리 호출 금지).
- 손으로 읽는 것은 다음뿐: 바깥 표준 · 원문을 지키는 것(VRM · glTF · TMX · JWT · 영수증 · 트레이스 · CSS 철자 스타일 시트 등), 양이 많거나 적었는지가 뜻인 칸(타일맵 타일, 문화권 표, `<Requires>` 같은 조건 원소), 고유 문법(`ScheduleCondition`, 이름=수 목록, 비트 합치는 낱말 칸, 격자 글), `TagContainer`(`addTag` 불변식), 키가 동적인 JSON, Core 층. 새로 손으로 읽으려면 이 목록 가운데 어디에 드는지 주석에 적는다.
- 실행 상태 저장 · 복원은 `SW_DECLARE_STATE_IO()` + `serializeState( StateIO& io )` 하나(린트 `common-shapes` `statePair` 강제).
- **카탈로그 참조 엄격도**: 같은 파일 필수 id 연결은 로드 · 쿠킹 실패, 다른 팩 · 코드가 채울 연결은 `optional="true"` 경고, 코드 등록 이름 오류는 로드 실패, 다른 파일 카탈로그 연결은 둘을 쥐는 쪽이 시작 때 검사, 원소 자기 칸 오류는 그 원소 제외. 참조 검사는 `Content/engine/settings/datacatalogreferences.xml` 의 `<Reference>` 줄 한 벌(로더 · 쿠커가 같은 줄을 돈다 — 카탈로그마다 `find( id ) == nullptr` 루프 금지). 카탈로그 항목을 가리키는 컴포넌트 · 씬 칸은 `CatalogKey<EntryDef>` + `<CatalogKey>` · `<Field>` 줄(테스트 `FieldTableMatchesReflection`).
