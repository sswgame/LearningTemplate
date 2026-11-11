# DataInheritance

> 질문: 데이터 파일이 부모를 가리키고 차이만 적게 하려면 무엇을 하는가?
> 독자: 파이프라인 · 머티리얼 · 프리팹 데이터를 쓰거나, 새 데이터 형식(아이템 · 어빌리티)을 이 구조에 올리는 사람과 에이전트.
> 결론: 루트에 `_base="<리소스 id>"` 를 적고 다른 칸만 쓴다. 카탈로그 항목은 `_base="<id>"` 다. 형식마다 합치기 코드를 쓰지 않는다 — 리플렉션 형식은 `PROPERTY( InheritKey )` 한 줄이면 되고, 손으로 읽는 XML 형식은 규칙을 값으로 적으며, 카탈로그는 설정 표에 한 줄을 더한다. 런타임은 쿠킹이 펼친 결과만 읽는다.

구현: `Source/Engine/Serialization/Inherit/DataInheritance.h`(합치기), `ResourceComposer.h`(읽는 쪽과 쿠커가 지나는 한 길).

## 파일에 적는 법

```xml
<RenderPipelineDesc _base="engine/pipeline/forwardpipeline.xml" _name="ForwardToonPipeline">
	<_listPass>
		<RenderGraphPassDesc _name="DepthOfField" _op="Remove" />
		<RenderGraphPassDesc _name="MotionBlur">
			<_listInput><item>SceneColor</item></_listInput>
		</RenderGraphPassDesc>
		<RenderGraphPassDesc _name="Fog" _after="Transparent" />
	</_listPass>
</RenderPipelineDesc>
```

| 자리 | 규칙 |
|---|---|
| 속성 | 자식이 적은 것만 덮는다. 빈 값(`attr=""`)도 적은 것이다 |
| 구조체 칸(자식 원소) | 안으로 들어가 같은 규칙으로 합친다 |
| 열쇠 있는 목록 | 원소의 열쇠 칸(`InheritKey`)으로 합친다. 아래 표 |
| 그 밖의 자식 원소(열쇠 없는 목록 · 글 값) | 자식이 적으면 같은 이름의 형제까지 통째로 바뀐다. 빈 원소(`<_listInput />`)는 비운다 |

| 목록 원소에 적는 것 | 뜻 | 오류 |
|---|---|---|
| (작업 없음) | 있으면 적은 칸만 합치고, 없으면 끝에 더한다 | 같은 열쇠를 두 번 적음 |
| `_op="Replace"` | 부모 칸을 물려받지 않고 바꾼다. 자리는 그대로다 | 없는 열쇠 |
| `_op="Remove"` | 지운다 | 없는 열쇠 |
| `_before="X"` · `_after="X"` | X 앞 · 뒤로 옮긴다. 칸 없이 자리만 옮겨도 된다 | 없는 X, 둘 다 적음 |

위에서 아래로 차례로 얹는다. 앞 줄이 더하거나 지운 결과를 뒤 줄이 본다.

오류: 순환, 없는 부모, 형식이 다른 부모, 구조체 칸을 두 번 적음(`DataInheritance.FieldRepeated`), 깊이 초과(`Config/Engine/EngineConfig.json` 의 `_dataInheritanceMaxDepth`). 오류면 로드 실패다. 부모 없이 자식 칸만으로 읽지 않는다.

열쇠를 잘못 적으면 "없는 원소를 더한 것" 이 된다. 삭제 · 교체만 없는 열쇠가 오류인 까닭은, 차이 파일에 새 원소를 적을 때마다 표시를 강제하지 않기 위해서다. 더해진 원소의 빈 칸은 그 형식의 검증(파이프라인은 `validate`)이 잡는다.

## 새 형식을 올리는 법

**리플렉션 형식(`ReflectedXMLFile::loadDesc` · `XMLSerializer::loadFromFile` 로 읽는 것).** 읽기는 이미 합친다. 할 일은 목록 원소의 이름 칸에 표시를 하는 것뿐이다. 본보기는 `RenderGraphPassDesc` 다.

```cpp
REFLECT()
struct RenderGraphPassDesc
{
    REFLECT_BODY();
    PROPERTY( InheritKey )
    string _name;
    // ...
};
```

열쇠가 없는 목록은 통째로 바뀐다.

**그리고 문서 표에 한 줄을 적는다(Engine 형식도).** 팩 쿠커는 리플렉션을 읽지 않는다 — 게임 모듈을 올리기 전에 돌고(`HeadlessSubsystem::runCookTask`), 대상마다 지어진 형식이 다르다(서버는 `Renderer/` 가 없어 `RenderPipelineDesc` 가 없다). 안 적으면 쿠킹이 `DataInheritance.NoSchema` 로 멈춘다. 표 줄이 코드와 다르면 `ResourceComposerTest` 가 진다.

```xml
<Document root="MyKitDesc">
	<Field name="_listStage" key="_name"/>
</Document>
```

칸 덮어쓰기로 풀리지 않는 Engine 형식은 `SW_DATA_INHERITANCE_MERGE` 로 합치기 함수를 올린다. 결과는 다른 형식과 같은 부모 없는 글이고 읽기와 쿠커가 같은 함수를 지난다. 층의 차이가 쌓임이고 부모(`<Skeleton>`)와 루트가 다른 `SkeletonVariant` 가 그렇다(사슬 규칙은 공통 `DataInheritance::collectChain`).

**Engine 의 손 XML 형식(`XMLFields::readProperties` 로 칸을 읽는 것).** 규칙을 값으로 적고 `ResourceComposer` 로 읽는다. 본보기는 `MaterialXML::makeInheritanceSchema` 와 `Material::loadFromXML` 이다. `SW_DATA_INHERITANCE_SCHEMA` 는 Engine 에서만 컴파일된다.

```cpp
DataInheritanceSchema MaterialXML::makeInheritanceSchema()
{
    DataInheritanceSchema schema{ "MaterialDef" };
    schema._listKeptAttribute.emplace_back( "formatVersion" );
    DataInheritanceSchema properties{ "_properties" };
    properties._keyAttribute = "name";
    schema._listField.push_back( std::move( properties ) );
    return schema;
}
SW_DATA_INHERITANCE_SCHEMA( MaterialDef, &MaterialXML::makeInheritanceSchema ); // 팩 쿠커가 루트 이름으로 찾는다

// 읽기
string flatText;
bool   bComposed = false;
if ( ResourceComposer::compose( text, sourceName, ResourceContext::forSchema( &MaterialXML::makeInheritanceSchema ), flatText, bComposed ) == false )
    return false;
```

맨 위 노드의 `_keyAttribute` 를 채우면 루트의 자식들이 곧 목록이다(래퍼 원소가 없는 형식).

**게임 · 키트의 손 XML 형식**은 코드 규칙 없이 문서 표의 줄만으로 읽고 쿠킹한다.

그 밖에 형식마다 해야 할 일은 둘이다.

- 참조 칸 표(`Config/Dev/AssetReferenceFields.generated.json`)에는 `_base` 가 이미 있어 참조 색인 · 린트가 따라간다. 핫 리로드가 있는 종류면 표의 칸을 `AssetReferenceUse::InlinedAtLoad` 로 둔다 — 부모가 바뀌면 `AssetReloadCascade` 가 자식을 의존 순서로 다시 읽는다. 따라가는 것은 `_base` 칸뿐이고(`EditorAssetReference::_bInlined`) 같은 파일의 다른 경로 칸은 연쇄하지 않는다.
- 저장이 있으면 `ReflectedXMLFile::saveDescAsChild`(부모와 다른 칸만) · `saveDesc`(형식 기본값과 다른 칸만)를 쓴다. 손 형식은 `ResourceComposer::saveDelta`(차이 · 다시 읽어 대조 · 쓰기 한 벌), 카탈로그 형식은 `DataCatalogInheritance::makeFileDelta` + `ResourceComposer::checkDeltaReadsBack` 이다. 두 글은 같은 직렬화기가 쓴 것이어야 한다.
  읽은 형식이 부모 id 를 들고(읽을 때 `DataInheritance::findBasePath`) 저장이 그 부모와의 차이로 쓴다 — 에디터가 펼친 한 벌을 쓰면 `_base` 가 끊긴다. 손 형식은 차이를 다시 읽어 같은 값이 나올 때만 쓴다(부모가 적은 칸을 자식이 지운 편집은 차이로 적을 수 없어 오류다). 본보기: `SocketSet::saveToFile` · `RetargetProfile::saveToFile` · `PhysicsAsset::saveToFile` · `AnimationGraphAsset::saveToFile` · `BehaviorTreeEditModel::makeSaveText` · `AudioGraphDef::saveToAsset`, 이벤트 프리셋은 `AudioEventLibrary::saveToXMLText`. 차이로 적을 수 없을 때 부모 없는 한 벌로 쓰는 대체는 두지 않는다. 글을 그대로 고쳐 쓰는 저장(리그 `RigSaveParams` · 카탈로그 창)은 `_base` 가 남고, 물려받은 줄을 고치면 열쇠 이름의 덮어쓰기 줄을 더한다. 되돌리기는 펼친 글로 복원하므로 복원한 값에 문서의 부모를 다시 단다.

## 쿠킹과 런타임

- 팩 쿠커(`PackCooker`)는 소스를 담을 때 `_base` 가 있는 파일을 펼친 한 벌로 담는다. 런타임은 글 검색 한 번으로 "부모 없음" 을 알고 지나간다. 쿠킹본만 읽는 구성에서 `_base` 를 만나면 오류다(`DataInheritance.NotFlattened`).
- 부모를 `AssetIO` 로 읽으므로 그 읽기가 쿠킹 작업의 입력이 된다. 의존 목록을 따로 적지 않아도 부모가 바뀌면 자식의 쿠킹 키가 바뀐다(`CookJob`).
- 에디터 · 개발 실행은 원본을 읽어 합친다.

## 조건(`_when`) · 덮어쓰기 층 · 경로 교체

부모 다음 단계다(`ResourceComposer`, 설계는 `docs/ResourceStructure.md` 2 절). 어느 형식에나 규칙 없이 적는다.

```xml
<Interaction id="Use" maxDistance="3">
	<_when _if="quality.low" maxDistance="5" />          <!-- 이 원소의 속성을 바꾼다 -->
	<_when _if="!platform.windows" _op="Remove" />       <!-- 이 원소를 뺀다 -->
</Interaction>
```

| 규칙 | 까닭 |
|---|---|
| 조건은 자기를 품은 원소 하나만 고친다(속성 · 빼기). 자식 원소를 품으면 오류다 | 합치기 규칙(열쇠 · 스키마)이 필요 없어 쿠커 · 개발 실행 · 배포 런타임이 같은 답을 낸다. 조건부로 더하려면 원소를 적고 반대 조건으로 뺀다 |
| 한 원소의 조건은 적힌 차례로 얹고 나중이 이긴다. 안쪽 원소의 조건과 바깥 원소의 조건은 서로 다른 원소를 고친다 | — |
| `_if` 는 리소스 태그(`축.값`)만 묻는다. 게임 상태 조건(`flag` · `chance`) · 모르는 축 · 닫힌 값 밖은 읽기 오류다 | 리소스 문맥에 게임 세계 칸이 없다. 게임 플래그는 `flag` 축 태그로 넣는다 |
| 부모와 자식이 같은 원소에 적은 조건은 쌓인다(부모 다음 자식). 열쇠 목록 아래의 조건은 목록 원소가 아니다. 차이 저장은 자식이 더한 조건만 적고, 부모의 조건을 지운 결과는 저장 오류다(`DataInheritance.WhenDropped`) | 자식이 값을 적어도 부모의 품질 조건이 사라지면 안 된다 |
| 팩 쿠커는 쿠킹 축(`platform` · `target`) 조건을 풀고, 로드 축 조건과 그 뒤 조건은 남긴다 | 한 원소에서 차례가 뒤집히지 않게 한다. 남은 쿠킹 축 조건은 런타임이 같은 값으로 푼다 |
| 저장은 있던 파일의 조건을 지킨다(`ResourceComposer::saveText`). 새 형식의 저장은 `FileUtil::writeTextFile` 이 아니라 이 입구를 쓴다 | 편집기 사본은 지금 태그로 푼 값을 들어, 그대로 쓰면 푼 값이 파일에 박히고 조건이 사라진다 |

덮어쓰기 층은 게임 팩 설정의 `<Engine _listResourceLayer>` 뿌리다. 리소스 `X` 를 리소스 id 로 읽을 때 `<뿌리>/X` 가 있으면 원본을 부모로 얹는다(층 파일에 `_base` 를 적으면 오류). 규칙은 읽는 쪽의 형식에서 온다 — 리플렉션 형식은 리플렉션(배포 런타임에도 있다), 손 형식은 그 규칙 함수, 카탈로그는 표. 경로 교체는 `<Engine _resourceRemap>` 표의 `<Remap id when to>` 다.

## 도구가 묻는 것

- 이 칸이 어디서 왔나: `DataInheritance::flatten` 의 `DataInheritanceProvenance`(`findOrigin( "_listPass[Present]._cullMode" )`), 프리팹은 `PrefabCache::findFieldOrigin`.
- 이 부모의 자식 목록: `DataInheritance::collectChildren`(파일을 훑는다). 에디터는 `EditorReferenceIndex::findReferrers` 도 같은 답을 준다.

## 애니메이션 · 스프라이트 형식의 열쇠

| 형식 | 열쇠 | 통째로 바뀌는 것 |
|---|---|---|
| `.animset.xml` | 클립 `name` | — (`path` 줄을 `set` 줄로 바꾸면 `_op="Replace"`) |
| `.animgraph.xml` | 상태 `name` · 전이 `id` · 레이어와 마스크 `name` · 주석 `id` | `blend_space` · `pose_node` · `condition`(빈 원소는 비운다) |
| `.spritelib.xml` | 분류 · 라벨 `_name` | — |
| `.rig.xml` | 대상 · 노드 `name` | 노드 안의 목록 |
| `.retarget.xml` | 사슬 `name` · 뼈 짝 `source` · 자세 `bone` | 사슬의 뼈 목록 |
| `.physics.xml` | 바디 `_bone` | 셰이프 · 충돌 쌍 |
| `.sockets.xml` · `.notifies.xml` | 소켓 · 가상 뼈 · 알림 `name`, 발 닿음 `notify` | — |

## 카탈로그 항목 상속

한 파일에 항목이 여럿인 카탈로그(아이템 · 아이템 외형 · 어빌리티)는 항목마다 부모를 가리킨다. 구현은 `Source/Engine/Serialization/Inherit/DataCatalogInheritance.h` 이고 합치기는 위와 같은 `DataInheritance` 다.

```xml
<ItemVisual id="helmet_knight_gilded" _base="helmet_knight">
  <Part name="Shell" material="common/materials/kaykit_knight_gilded.material"/>
</ItemVisual>
<Item id="my_helmet" _base="common/data/items.xml#helmet_knight" name="My Helmet"/>
<Part name="Beard" mesh="…" _before="Brows"/>
<BlockedTag tag="State.Dashing" _op="Remove"/>
<Requires _op="Clear"/>
```

| 적는 것 | 뜻 |
|---|---|
| 항목의 `_base="<id>"` | 같은 파일 · 같은 종류의 항목. 파일에서 뒤에 있어도 된다. 종류마다 id 가 따로다(어빌리티와 이펙트) |
| 항목의 `_base="<리소스 id>#<id>"` | 다른 파일의 항목. 다른 게임 팩이 `common` 을 부모로 쓴다. 그 항목의 `_base` 는 그 파일 기준이다 |
| 루트의 `_base="<리소스 id>"` | 파일 전체를 물려받는다. 항목을 더하고 고치고 `_op="Remove"` 로 뺀다. 먼저 합친 뒤 항목의 부모를 찾는다 |
| 규칙의 자식 칸(`key` 있음) | 래퍼 없이 되풀이되는 원소를 그 속성으로 합친다. `_op` · `_before` · `_after` 는 위 표와 같다 |
| 규칙의 자식 칸(`key` 없음) | 구조체 칸이다. 안으로 들어가 합친다. 두 번 적으면 오류다 |
| 규칙에 없는 자식 | 자식이 적으면 그 이름의 원소를 통째로 바꾼다. `<X _op="Clear"/>` 는 물려받은 것을 비운다 |

오류는 파일 상속과 같다: 순환, 없는 부모, 깊이 초과. 더해서 규칙에 없는 종류의 `_base`(`DataCatalogInheritance.KindNotListed`), 구조체 칸을 두 번 적음(`FieldRepeated`). 오류면 그 파일을 읽지 못한다.

규칙은 코드가 아니라 `Content/engine/settings/datacataloginheritance.xml` 에 있다(`<Entry name key>` 가 항목 종류, 줄 바로 아래 `<Field>` 는 항목이 아닌 루트 칸 — 입력 맵의 `<layers>` 래퍼). 팩 쿠킹은 게임 모듈을 올리기 전에 돌기(`HeadlessSubsystem::runCookTask`) 때문에 `GameFramework` 나 키트의 정적 등록을 볼 수 없다. 새 카탈로그는 이 표에 한 줄을 더하면 된다. 읽는 코드는 고치지 않는다.

읽기는 `XMLDocument::loadResource` · `parseData` 가 `ResourceComposer` 로 펼친다. 그래서 `XMLCatalog` · `AppearanceDatabase` · 데이터 종류 검증이 모두 펼친 결과만 본다. 항목 id 를 바꾸지 않으므로 세이브는 그대로다. 팩 쿠커도 같은 함수로 펼쳐 담는다. 쿠킹본에 `_base` 가 남으면 읽기가 모르는 칸 오류를 낸다.

에디터 카탈로그 창(`CatalogDocument`)은 원문을 고치므로 적은 칸만 저장한다. 물려받은 칸은 흐린 글로 보이고, 부모와 같은 값을 적으면 그 칸을 지운다. 행의 Variant 단추가 이 행을 부모로 하는 새 행을 만든다. 행의 Children 단추는 자식 원소를 규칙대로 펼친다(`CatalogChildSection`). 줄의 순서와 값은 엔진이 합친 결과이고, 고치면 그 원소만 열쇠와 함께 적으며 비면 지운다. 목록 원소는 더하기 · 빼기(`_op="Remove"`) · 되살리기 · 순서(`_before` · `_after`)가 있고, 규칙에 없는 칸은 물려받기 · 덮어쓰기 · 비우기(`_op="Clear"`)다. 아무 원소도 적지 않은 새 칸 이름을 더하는 일과 글 값(`<X>글</X>`)은 글 상자에서 고친다. 시나리오: `engine/automation/editor/catalogchildren`.

핫 리로드: 부모 파일을 고치면 항목 `_base` · 루트 `_base` 로 물려받은 카탈로그가 다시 읽힌다. 표에 없는 `.xml` 은 그 경로를 가진 캐시가 다시 읽고(`AssetReloadRoute::kAnyCacheHoldingPath`) 부모만 고쳐도 감시가 알린다. 역색인은 `#` 앞 파일을 대상으로 센다. 파일 단위 계획이라 두 파일이 서로 다른 항목을 부모로 삼아도 순환 오류이고(`AssetReloadCascade.ReferenceCycle`) 아무것도 다시 읽지 않는다. 카탈로그는 모두 공유 캐시(`SharedAssetCache<T>`)에 들어 이 경로를 탄다([ResourceStructure](../ResourceStructure.md) 3 절). 시나리오: `game/empty/automation/editor/catalogcascade`. 실제 예: `game/empty/data/interactionhighlight.interactions.xml` 은 `common` 의 항목을 부모로 쓴다.

같은 메시를 머티리얼만 달리한 아이템은 외형 항목을 물려받아 `material` 만 적는다. 소켓 프리팹 부품은 프리팹의 모든 메시에 그 머티리얼을 입히고, 쉬는 부품 풀은 머티리얼별로 나뉜다. 꾸미기로 고르는 변형은 `<MaterialVariant>` 다. 시나리오: `game/shooter3d/automation/showcase/itemvariant`.

머티리얼이 여럿인 스킨드 부품은 메시 구간 이름마다 머티리얼을 적는다(`<Section name material>`). 변형(`<Variant>` · `<MaterialVariant>`)은 안에 `<Section>` 을 적어 그 구간만 바꾸고 `<Material section>` 으로 그 구간에만 매개변수를 건다. 규칙은 `<SwapMaterial section>` · `<MaterialValue section>` 이고, 넓은 교체(부품 전체) 위에 좁은 교체(구간)가 덮는다. 구간은 번호가 아니라 이름이다(언리얼 Material Slot · 유니티 `Renderer.materials` 는 슬롯 · 번호) — 다시 임포트해 구간 순서가 바뀌어도 데이터가 맞다. 병합(`_bMergeParts`)해도 구간마다 머티리얼이 남고, 구간 머티리얼은 메시 해시(병합 캐시 키)에 든다. 물려받은 항목은 변형의 `<Section>` 을 이름으로 덮는다(`datacataloginheritance.xml`). 시나리오: `game/empty/automation/sectionmaterial`.

외형 프리셋(`CharacterAppearance` 의 `parent`)은 여기에 올리지 않았다. 값은 씨앗으로 뽑고 세트는 쌓이는 규칙이라 칸을 덮는 합치기와 다르다(`presets.xml` 머리말).

| 엔진 | 방식 | 여기와 다른 점(기억에 의존) |
|---|---|---|
| 언리얼 Data Asset | 자산 하나가 항목 하나, 변형은 자식 클래스나 사본. Data Table 은 행 상속이 없다 | 값만 다른 변형에 자산이 하나씩 든다. 여기는 표 한 줄이고, 표 안의 행끼리 물려받는다 |
| 유니티 ScriptableObject | Variant 는 프리팹처럼 자산 단위이고 일반 ScriptableObject 는 변형이 없다 | 같은 모델을 행 단위로 쓴다. 리스트를 인덱스가 아니라 이름으로 짚는다 |

## 프리팹 변형

프리팹은 `<GameObject>` 대신 씬 인스턴스와 같은 `<PrefabOverrides>` 를 적는다.

```xml
<Prefab formatVersion="0" _base="game/abilityarena/prefabs/caster.prefab.xml" name="ArenaGrunt">
	<PrefabOverrides>
		<Override key="ArenaUnitComponent#0">
			<ArenaUnitComponent _kind="Grunt" />
		</Override>
	</PrefabOverrides>
</Prefab>
```

읽으면 `getStateData` 와 중첩 인스턴스가 부모를 합친 값이다. 스폰 · 씬 복원 · 쿠킹은 변형인지 모른다. 쿠킹본(`.prefab.bin`)에는 합친 값만 있다. `<Part path="0">` 로 물려받은 중첩 부품을 덮어쓰고 `<PrefabInstance>` 로 부품을 더한다. 물려받은 부품을 지울 수는 없다(유니티와 같다). 에디터의 Apply 는 변형을 변형으로 저장한다.

## 씬 조립

씬은 프리팹과 같은 모델을 쓰고, 런타임은 펼친 엔티티 목록 하나만 본다. 구현: `SceneDocument::expandSceneInstances` · `Scene::instantiateNodes` · `SceneDataLayerSubsystem`.

```xml
<Scene formatVersion="2" name="Town">
	<SceneSettings …>
		<_listDataLayer><SceneDataLayerDef _name="Winter" _bInitiallyActive="false" /></_listDataLayer>
	</SceneSettings>
	<entities>
		<entity id="5" name="House" scene="game/x/maps/house.scene.xml">
			<PrefabOverrides><Part path="1"><PrefabOverrides>…</PrefabOverrides></Part></PrefabOverrides>
			<GameObject …>…앵커의 자리…</GameObject>
		</entity>
		<entity id="6" name="Snow" layers="Winter">…</entity>
	</entities>
</Scene>
```

| 적는 것 | 뜻 | 오류 |
|---|---|---|
| 엔티티 `scene="<씬>"` | 앵커다. 그 씬의 엔티티가 앵커 아래에 펼쳐진다 | 없는 씬, 자기 포함, 상태 없는 앵커, 프리팹과 함께 적음 |
| 앵커의 `<Part path="내부 id">` | 멤버를 덮어쓴다. 중첩은 `7/3`. 프리팹의 `<Part>` 와 같은 형식이다 | 없는 대상은 경고하고 버린다(내부 씬을 고쳤다고 바깥이 안 읽히면 안 된다) |
| 루트 `_base="<씬>"` | 씬 상속. 엔티티 id 가 열쇠이고 `_op` · `_before` · `_after` 는 위 표와 같다. 상태가 실린 엔티티에 `<PrefabOverrides>` 를 적으면 부모 상태 위에 얹는다 | 파일 상속과 같다 |
| 엔티티 `layers="A,B"` | 모두 켜졌을 때 오른다. 인스턴스 멤버는 앵커의 레이어를 더해 가진다 | 선언하지 않은 이름 |

- **id:** 멤버의 파일 id 는 내부 id ^ `computeInstanceIDScope( 앵커 id )` 다. 글 상태의 참조도 같은 XOR 로 옮긴다(`ObjectLoadContext::_savedIDScope`). 쿠킹된 바이너리 상태는 이미 평평한 id 다. 앵커 id 와 내부 id 에서만 정해지므로 내부 씬을 고쳐도 `SceneObjectReference` 가 끊기지 않는다.
- **이름:** 멤버 이름은 `앵커/내부`다. 네트워크 씬 번호가 이름의 해시라(`NetObjectTable`) 인스턴스마다 달라야 한다. 그래서 내부 씬의 코드가 이름으로 찾는 일은 멤버에서 맞지 않는다.
- **쿠킹:** 펼친 목록을 그대로 굽고 저작 칸을 비운다(`stripAuthoringInstanceData`). 내부 씬을 `AssetIO` 로 읽으므로 내부 씬이 바뀌면 바깥 씬의 쿠킹 키가 바뀐다. 끈 레이어까지 모두 지어 굽는다. 대상의 레이어가 가리키는 쪽의 레이어에 들어 있지 않으면 쿠킹 실패다(`SceneCooker.DataLayerCrossReference`). 셀은 좌표 + 레이어 집합이 하나다.
- **저장:** 멤버는 엔티티로 적지 않고, 내부 씬을 임시로 지어 쓴 상태와의 차이를 앵커의 `<Part>` 로 적는다. 상속 씬은 부모를 지어 쓴 글과의 차이로 적는다(`saveXMLDelta`). 끈 레이어의 엔티티는 읽은 그대로 다시 적는다.
- **지우기:** 멤버는 지울 수 없다. 프리팹 부품과 같다 — 지워도 다음 로드에 돌아온다.
- **켜고 끄기:** `SceneDataLayerSubsystem::setLayerActive`, 개발 명령 `scene.layer <이름> on|off`, 에디터 Data Layers 창. 편집 중에 끄면 내리지 않고 숨기기만 한다. 시나리오: `game/empty/automation/datalayer`.

## 기본값을 쓰지 않는 저장

저장은 부모(없으면 형식 기본값)와 같은 칸을 쓰지 않는다. 프리팹은 뺀 글을 다시 읽어 값이 같을 때만 쓰고, 다르면 모든 칸을 쓰며 경고한다(`PrefabAsset.DefaultOmitMismatch`).

**코드에서 기본값을 바꾸면 그 칸을 적지 않은 모든 데이터의 값이 바뀐다.** 언리얼의 CDO 와 같다. 바꾸면 안 되는 값은 기본값을 고치지 말고 데이터에 적는다. 값이 그림 · 규칙을 바꾸면 쓰는 쪽 자료를 대조한다. 필수 칸(`PROPERTY( Required )`)은 저장이 지우지 않는다.

## 합치지 않은 것과 까닭

| 코드 | 까닭 |
|---|---|
| `PrefabOverrides` 의 합치기 | 컴포넌트 목록의 열쇠가 칸 값이 아니라 `이름#n` 이고 원소 이름이 타입이다. 씬 인스턴스는 프리팹에서 사라진 대상을 오류가 아니라 경고로 넘겨야 한다(프리팹을 고쳤다고 씬이 읽히지 않으면 안 된다). 변형은 같은 형식을 쓰되 `checkOverrideTargets` 로 엄격하게 먼저 본다 |
| `_listRenderPassRef` | 부모 하나가 아니라 여러 파일을 섞는 믹스인이다. 단일 부모로 옮기면 첨부 묶음을 파이프라인마다 베껴야 한다. 합친 결과의 이름이 겹치면 오류인 것도 다르다(덮어쓰지 않는다) |
| JSON 형식 | 설정은 층(`ConfigManager`)으로 겹친다. 프리팹 JSON 은 변형을 읽고 쓰지만 기본값은 빼지 않는다. 에디터 테마 토큰(`*.tokens.json`)은 파일이 자기 부모를 고르므로 `_base` 를 적고 키 표를 덮는다(`EditorThemeTokens`) |
| 오디오 이벤트의 `_parentEvent` | 이벤트가 `_listEvent` 래퍼 안에 있어 카탈로그 규칙(루트 바로 아래 되풀이)에 맞지 않는다. 합치기는 `DataInheritance` 그대로이고 이벤트 하나를 가상의 문서로 넘긴다. 파일 합치기가 목록 원소의 `_base` 를 지우므로 이름이 다르다 |
| `AudioEffectDef` 목록 | 이름이 비면 종류 이름을 쓰는 규칙이라 열쇠가 비는 원소가 있다. 적으면 통째로 바뀐다 |
| `SkeletonVariant` 의 칸 | 뼈 비율은 곱해지고 클립 규칙은 쌓이며 본은 뒤에 붙는다. 덮어쓰면 변형의 변형이 다른 값이 된다. 사슬(순환 · 깊이 · 없는 부모)만 공통이고 칸은 `Skeleton::mergeVariantText` 가 얹어 평평한 `<Skeleton>` 글을 낸다(쿠킹본에 `_base` 가 남지 않는다). 부모 본 번호를 바꾸지 않는지는 합친 뒤에 본다 |

## 상용 엔진과 비교

기억에 의존한 설명이다. 판마다 다를 수 있다.

| 엔진 | 방식 | 여기와 같은 점 · 다른 점 |
|---|---|---|
| 언리얼 | 아키타입(CDO) 위에 다른 칸만 쓰는 델타 직렬화, 블루프린트 자식 클래스 | 부모 값을 고치면 적지 않은 칸이 따라간다. 기본값을 코드에서 바꾸면 데이터가 따라가는 위험도 같다. 배열은 통째로 델타이지만 여기는 열쇠가 있으면 원소 단위다 |
| 유니티 | Prefab Variant(`m_Modifications` — 경로와 값의 목록) | 프리팹 변형이 같은 모델이다. 유니티 배열은 인덱스로 짚어 부모에 끼워 넣으면 자식이 어긋나지만, 여기는 이름으로 짚어 어긋나지 않는다 |
| Godot | 씬 상속(부모 씬 + 바뀐 속성 · 더한 노드) | 노드 경로로 짚는 것이 열쇠와 같다. 부모가 지운 노드를 자식이 가리키면 여기는 오류다(변형) |

여기서만 하는 일은 쿠킹에서 펼치는 것이다. 세 엔진도 쿠킹본에는 결과만 남기지만, 여기는 XML 파일 자체를 펼쳐 런타임 코드 경로가 하나다.
