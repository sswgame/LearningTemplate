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

열쇠를 잘못 적으면 새 원소를 더한 것이 된다(새 원소에 표시를 강제하지 않으려고 삭제 · 교체만 없는 열쇠가 오류다). 더해진 원소의 빈 칸은 그 형식의 검증(파이프라인은 `validate`)이 잡는다.

## 새 형식을 올리는 법

순서: (1) `SW_ASSET_TYPE` 한 줄(로더 포함, [ExistingFacilities](ExistingFacilities.md) "리소스 읽기 입구와 에셋 참조"), 쿠킹이 펼칠 형식이면 아래 문서 표에도 한 줄. (2) 목록 원소의 이름 칸에 `PROPERTY( InheritKey )` 또는 카탈로그 규칙 한 줄. (3) 다른 리소스를 가리키는 칸은 `AssetRef<T>`. (4) 리소스 id 로 읽는 입구를 쓰고, 쓰는 쪽은 `acquire` · `request` 로 받아 실행 상태는 자기 쪽에 둔다. (5) 조건과 동작은 등록된 어휘를 쓴다("행동 어휘와 행동 리소스 변형"). (6) 칸을 더하면 실행이 그 칸을 읽고 시나리오가 그 효과를 확인한다 — 읽히지 않는 칸은 넣지 않는다.

**리플렉션 형식(`ReflectedXMLFile::loadDesc` · `XMLSerializer::loadFromFile` 로 읽는 것).** 읽기는 이미 합친다. 목록 원소의 이름 칸에 표시만 한다(본보기 `RenderGraphPassDesc`).

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

**그리고 문서 표에 한 줄을 적는다(Engine 형식도).** 팩 쿠커는 게임 모듈을 올리기 전에 돌고(`HeadlessSubsystem::runCookTask`) 대상마다 지어진 형식이 달라(서버에는 `RenderPipelineDesc` 가 없다) 리플렉션을 읽지 않는다. 안 적으면 쿠킹이 `DataInheritance.NoSchema` 로 멈추고, 표 줄이 코드와 다르면 `ResourceComposerTest` 가 진다.

```xml
<Document root="MyKitDesc">
	<Field name="_listStage" key="_name"/>
</Document>
```

칸 덮어쓰기로 풀리지 않는 Engine 형식(차이가 쌓이는 `SkeletonVariant`)은 `SW_DATA_INHERITANCE_MERGE` 로 합치기 함수를 올린다. 결과는 부모 없는 글이고 읽기와 쿠커가 같은 함수를 지난다(사슬 규칙은 공통 `DataInheritance::collectChain`).

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

- 참조 칸 표(`Config/Dev/AssetReferenceFields.generated.json`)에는 `_base` 가 이미 있다. 핫 리로드가 있는 종류면 그 칸을 `AssetReferenceUse::InlinedAtLoad` 로 둔다 — 부모가 바뀌면 `AssetReloadCascade` 가 자식을 의존 순서로 다시 읽는다(따라가는 것은 `_base` 칸뿐).
- 저장은 부모와의 차이로 쓴다. 읽을 때 부모 id 를 들고(`DataInheritance::findBasePath`) 저장한다 — 펼친 한 벌을 쓰면 `_base` 가 끊긴다. 리플렉션 형식은 `ReflectedXMLFile::saveDescAsChild`(부모와 다른 칸) · `saveDesc`(형식 기본값과 다른 칸), 손 형식은 `ResourceComposer::saveDelta`(차이 · 다시 읽어 대조 · 쓰기), 카탈로그는 `DataCatalogInheritance::makeFileDelta` + `ResourceComposer::validateDeltaReadsBack`. 두 글은 같은 직렬화기가 쓴 것이어야 한다. 차이로 적을 수 없는 편집(부모가 적은 칸을 지움)은 오류이고, 부모 없는 한 벌로 쓰는 대체는 두지 않는다. 본보기: `SocketSet::saveToFile` · `AnimationGraphAsset::saveToFile` · `BehaviorTreeEditModel::makeSaveText`. 원문을 그대로 고치는 저장(`RigSaveParams` · 카탈로그 창)은 물려받은 줄을 고치면 열쇠 이름의 덮어쓰기 줄을 더한다. 되돌리기로 복원한 값에는 문서의 부모를 다시 단다.

## 쿠킹과 런타임

- 팩 쿠커(`PackCooker`)는 `_base` 가 있는 파일을 펼친 한 벌로 담는다. 쿠킹본만 읽는 구성에서 `_base` 를 만나면 오류다(`DataInheritance.NotFlattened`).
- 부모 · 내부 씬은 `AssetIO` 로 읽어 그 읽기가 쿠킹 입력이 된다 — 의존 목록을 따로 적지 않는다(`CookJob`).
- 에디터 · 개발 실행은 원본을 읽어 합친다.

## 조건(`_when`) · 덮어쓰기 층 · 경로 교체

부모 다음 단계다. 리소스 글은 모두 `ResourceComposer::compose( text, id, context )` 한 길을 지나고(부모 → 조건 → 덮어쓰기 층), 읽는 쪽은 단계를 모르고 펼친 글만 받는다. 조건은 어느 형식에나 규칙 없이 적는다 — Godot 기능 태그처럼 태그 집합 하나에 묻되, 언리얼 Device Profile 처럼 쿠킹 축과 로드 축을 나눠 쿠킹 축은 쿠킹본에서 지운다.

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

| 축 | 정해지는 때 | 값을 넣는 곳 | 쿠킹본 |
|---|---|---|---|
| `platform` · `target` | 쿠킹 | 엔진 기동(쿠커도 같은 자리) | 펼쳐서 없어진다(로드 축 조건 뒤의 것은 차례를 지키려 남고 런타임이 같은 값으로 푼다) |
| `quality` | 로드 | 사용자 설정 핸들러 `resource.tag` | `_when` 이 남고, 값이 바뀌면 그 태그를 읽은 리소스를 다시 읽는다 |
| `locale` | 로드 | 현지화의 지금 언어(`en-US` → `locale.en_US`) | 위와 같다 |
| `region` · `feature` · `flag` | 로드 | 게임 코드(`ResourceTags::setAxisValue` · `setTagActive`) | 위와 같다 |
| `layer` | 로드 | 켜진 씬 데이터 레이어(`SceneDataLayerSubsystem`) | 위와 같다 |

- **규칙 출처의 순서:** 문서 표 · 카탈로그 표(`datacataloginheritance.xml`) → 합치기 함수(`SW_DATA_INHERITANCE_MERGE`) → 손 형식 → 리플렉션 형식. 쿠킹은 Engine 코드만 보므로 게임 · 키트 형식은 표에만 있어도 된다. 프리팹(`PrefabOverrides`)은 열쇠 모양이 달라 따로이고 문서 머리의 조건 · 층만 같은 길을 지난다.
- **태그 주인:** 식은 `ConditionExpr` 이고 그냥 이름은 태그 조건(`resourceTag`)이다. 태그는 코어 범위 리소스 루트(`ContentRootSubsystem::getResourceTags`)가 든 `ResourceTags`, 합성 기록(`ResourceComposition`)도 같은 주인이 든다. 쿠킹본을 런타임이 푼 결과가 소스를 바로 푼 결과와 같은지는 `ResourceConditionTest.CookedTextResolvesLikeTheSource`.
- **바이너리 경로 교체:** 텍스처 · 오디오 · 메시는 경로를 바꾼다(`ResourceRemap`). 같은 id 는 위에서 처음 참인 줄, 사슬은 오류. `AssetIO` 입구가 찾기 전에 한 번 바꾸고, 쿠킹 작업 안에서는 바꾸지 않는다(쿠킹본이 기계마다 달라지지 않게). 두 파일 모두 팩에 든다.
- **다시 읽기:** 리소스 id 로 읽은 합성은 기댄 로드 축 태그를 `ResourceComposition` 에 남기고, 태그가 바뀌면 `AssetManager::reloadComposedDependents` → `AssetCacheBase::reloadComposed` 가 그 id 를 쥔 캐시를 다시 읽는다. 배포 구성에도 있다. 시나리오 `game.empty.resourcecondition`.
- **한계:** 캐시 열쇠는 경로뿐이라 한 프로세스의 문맥은 하나다. 서버가 플레이어마다 다른 지역 값을 고르려면 데이터의 키로 나눈다. 에디터에서 씬이 여럿이면 `layer` 축은 마지막에 넣은 씬의 집합이다. 예외 하나: 몇 축만 바꿔 끼우는 미리보기 합성(`ResourceContext::withTagOverride` — UI 미리보기의 `screen` · `orientation`)은 그 캐시가 경로 + 태그 열쇠로 따로 들고(`UITextAssetCache`) 다시 읽기 · 접속 대조 기록에 적지 않는다.
- **네트워크:** 게임플레이 값이 클라이언트마다 다르면 판정이 어긋난다. 클라이언트는 붙자마자, 그리고 새로 읽거나 바뀔 때마다 로드 축 · 층에 기댄 합성 한 줄(리소스 id + 결과 해시, `GameNetSessionMessage::kResourceDigest`)을 보내고, 서버(`ResourceDigestComparer`)는 같은 리소스를 다르게 합성했고 유예(`gv_netResourceDigestGraceSeconds`)가 지나도 다르면 끊는다(`Net.ResourceDigestMismatch`). 서버가 조건 · 층 없이 읽은 표도 해시를 들어, 클라이언트만 모드 층을 얹으면 거절된다. 서버가 읽지 않는 리소스(시각 효과)는 대조하지 않는다. `ResourceTagScope::PerClient` 축(품질 · 언어)은 달라도 되므로 판정 값은 이 축에 기대지 않는다. 일관성 검사이지 부정행위 방지가 아니다. 시뮬레이션 데이터에 쿠킹 축과 로드 축 조건을 섞지 않는다. 시나리오 `editor.netresourcedigest`.
- **편집기 저장:** 참인 조건이 정한 칸의 편집은 그 조건의 값이 된다(규칙은 `restoreConditions` 머리말). 층을 얹어 읽은 리소스는 원본 자리에 쓰지 않는다(`ResourceLayer.SaveWouldBake`). 시나리오 `editor.resourcesave`.
- **덮어쓰기 층**(언리얼 Composite Data Table · 팩 우선순위 자리): 순서 있는 목록(`ResourceLayers`, 아래 층이 먼저)이고 층 파일은 원본을 암묵적 부모로 가진 차이다. 목록은 게임 팩 설정의 `game/<팩>/layers/<이름>` 이고 DLC · 모드 팩을 올리는 쪽이 늘린다(`ResourceComposition::setLayers` — 바뀐 층에 파일이 있는 리소스를 다시 읽는다). 쿠킹 때 펼치지 않고 읽을 때 얹는다 — 공용 도메인 팩(`common` · `engine`)은 게임마다 같아야 해 게임 층을 펼칠 자리가 없다(`PackCooker::cookDomainPack`). 층 파일의 `_when` 은 층의 차이에 얹는다. 리소스 id 로 읽을 때만 얹고 글 입구(`loadFromXMLText`)는 얹지 않는다. 항목 id 를 바꾸지 않아 세이브는 그대로다. 층 파일은 외부 입력이라 합성 뒤 그 형식의 검증이 그대로 돌고, 온라인 게임은 위 해시 대조로 거른다.

## 도구가 묻는 것

- 이 칸이 어디서 왔나: `DataInheritance::flatten` 의 `DataInheritanceProvenance`(`findOrigin( "_listPass[Present]._cullMode" )`), 프리팹은 `PrefabCache::findFieldOrigin`.
- 이 부모의 자식 목록: `DataInheritance::collectChildren`(파일을 훑는다). 에디터는 `EditorAssetIndex::findReferrers` 도 같은 답을 준다.

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

에디터 카탈로그 창(`CatalogDocument`)은 원문을 고치므로 적은 칸만 저장하고, 부모와 같은 값을 적으면 그 칸을 지운다. 자식 원소 편집(`CatalogChildSection`)도 같은 규칙(열쇠와 함께 적고, 비면 지움, `_op` · `_before` · `_after` · `_op="Clear"`)을 쓴다. 시나리오: `engine/automation/editor/catalogchildren`.

핫 리로드: 부모 파일을 고치면 물려받은 카탈로그가 다시 읽힌다(`AssetReloadCascade`, 역색인은 `#` 앞 파일 기준). 계획이 파일 단위라 두 파일이 서로의 항목을 부모로 삼으면 순환 오류(`AssetReloadCascade.ReferenceCycle`)다. 시나리오: `game/empty/automation/editor/catalogcascade`. 공유 캐시의 다시 읽기(새 판 · 옛 판)는 [ExistingFacilities](ExistingFacilities.md) "리소스 읽기 입구와 에셋 참조".

외형 항목의 머티리얼은 메시 구간 **이름**으로 적는다(`<Section name material>` · `<SwapMaterial section>` · `<MaterialValue section>`; 번호는 다시 임포트하면 어긋난다). 물려받은 항목은 변형의 `<Section>` 을 이름으로 덮는다. 시나리오: `game/empty/automation/sectionmaterial` · `game/shooter3d/automation/showcase/itemvariant`.

외형 프리셋(`CharacterAppearance` 의 `parent`)은 여기에 올리지 않는다 — 값은 씨앗으로 뽑고 세트는 쌓이는 규칙이라 칸 덮어쓰기와 다르다(`presets.xml` 머리말).

## 프리팹 변형

프리팹은 `<GameObject>` 대신 씬 인스턴스와 같은 `<PrefabOverrides>` 를 적는다.

```xml
<Prefab formatVersion="1" _base="game/abilityarena/prefabs/caster.prefab.xml" name="ArenaGrunt">
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
<Scene formatVersion="3" name="Town">
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
- **이름:** 멤버 이름은 `앵커/내부`다(네트워크 씬 번호가 이름의 해시라 — `NetObjectTable`). 내부 씬의 코드는 멤버를 이름으로 찾지 않는다.
- **쿠킹:** 펼친 목록을 굽고 저작 칸을 비운다(`stripAuthoringInstanceData`). 끈 레이어까지 굽는다. 참조 대상의 레이어가 가리키는 쪽의 레이어에 들어 있지 않으면 쿠킹 실패다(`SceneCooker.DataLayerCrossReference`).
- **저장:** 멤버는 엔티티로 적지 않고, 내부 씬을 임시로 지어 쓴 상태와의 차이를 앵커의 `<Part>` 로 적는다. 상속 씬은 부모를 지어 쓴 글과의 차이로 적는다(`saveXMLDelta`). 끈 레이어의 엔티티는 읽은 그대로 다시 적는다.
- **지우기:** 멤버는 지울 수 없다(프리팹 부품과 같다).
- **켜고 끄기:** `SceneDataLayerSubsystem::setLayerActive`(개발 명령 `scene.layer <이름> on|off`). 시나리오: `game/empty/automation/datalayer`.

## 기본값을 쓰지 않는 저장

저장은 부모(없으면 형식 기본값)와 같은 칸을 쓰지 않는다. 프리팹은 뺀 글을 다시 읽어 값이 같을 때만 쓰고, 다르면 모든 칸을 쓰며 경고한다(`PrefabAsset.DefaultOmitMismatch`).

**코드에서 기본값을 바꾸면 그 칸을 적지 않은 모든 데이터의 값이 바뀐다.** 언리얼의 CDO 와 같다. 바꾸면 안 되는 값은 기본값을 고치지 말고 데이터에 적는다. 값이 그림 · 규칙을 바꾸면 쓰는 쪽 자료를 대조한다. 필수 칸(`PROPERTY( Required )`)은 저장이 지우지 않는다.

## 행동 어휘와 행동 리소스 변형

행동 트리 · 스케줄 · 대화 · 어빌리티 · 기믹 회로는 실행기를 합치지 않고(아래 "합치지 않은 것") 조건과 동작의 이름만 공유한다.

- **조건 `ConditionExpr`**(`Engine/Behavior/ConditionExpr.h`): 문법 `flag && !other` · `tag(Quest.Done)` · `weather(rain, storm)` · `affinity >= 4`, 등록은 `SW_CONDITION( 이름, 칸 스키마, 함수 )`. 읽을 때 후위 배열 + 함수 포인터로 컴파일하고 모르는 이름은 로드 오류, 확률 조건은 문맥의 씨앗을 쓴다. 세계는 형식 있는 칸의 문맥(`GameplayContext` — `GameFlagsSlot` · `BlackboardSlot` · `ScheduleConditionSlot`)으로 넘긴다 — 없는 칸은 거짓, 없는 값은 NaN(`hasBlackboard` 로 묻는다). 장착 · 외형 조건의 종류 칸(`_kind` · `_setID` · `_tag`)은 순환 · 뒤집힌 규칙 검사가 쓰므로 남는다. 쓰는 곳: `ScheduleCondition` · `EquipmentCondition` · `AppearanceRuleCondition` · 대화 `_condition` · 행동 트리 블랙보드 조건 · 상점 조건 · 리소스 `_when`. 쓰지 않는 곳: `AnimationConditionOp`(매 프레임 값 비교), `UserSettingConditionOperator`(게임 문맥 없음).
- **동작 `ActionRegistry`**(`Engine/Behavior/ActionRegistry.h`): `SW_GAME_ACTION( OpenShop, "shop.open", OpenShopParams, &openShop )` — 칸은 리플렉션 구조체라 에디터가 목록과 칸을 보이고, 함수는 `Succeeded` · `Failed` · `Running`. 글 형식은 `flag.set(hasKey, 1)` · `flag.set(flag=hasKey, value=1)`, 행동 트리는 노드 속성(`<Action task="flag.set" flag="alarm"/>` — 칸 이름은 `id` · `name` · `task` 와 겹치지 않게). 쓰는 곳: 행동 트리 `<Action task>` · 대화 `_actionCommand` · 스케줄 `onStart` · `onEnd` · 퀘스트 `<Reward><Action call>`(시나리오 `game/empty/automation/vocabulary`). 쓰지 않는 곳: 어빌리티 이펙트(복제 · 예측), 기믹 회로 노드(스텝 상태가 저장 대상), 에디터 · 개발 명령. 코드로 짓는 트리(`BehaviorTree::addAction( …, BehaviorTaskFunction )`)는 이름 없이 함수를 받는다.
- **행동 트리 변형:** 노드 `id` 가 열쇠(`BehaviorTreeSchema`, `_bSelfNested`)라 `_base` 트리에서 가지만 바꾼다. `<SubTree tree>` 는 읽을 때 펼치고(순환 오류, 언리얼 Run Behavior), `<SubTree slot>` 은 러너가 `setSlotTree` 로 채운다(Run Behavior Dynamic). 퀘스트 · 루트 · 스폰 · 상점은 `datacataloginheritance.xml` 의 줄이다.

## 종류별 변형 축

앞 층(부모 · 조건 · 덮어쓰기 층) 위에 얹는다. 합치기 코드 대신 `PROPERTY( InheritKey )` 나 규칙 한 줄이 기본이다.

| 축 | 항목 | 엔진 |
|---|---|---|
| 교체표 | 애니메이션 셋 `_base` · 열쇠 목록, 그래프는 슬롯 이름만 | 유니티 Animator Override Controller |
| 교체표 | 그래프 상태가 열쇠, 연결 레이어 `<layer slot>` → `SkeletalAnimatorComponent::_listLinkedLayer`(한 단계) | 언리얼 Linked Anim Layers |
| 교체표 | 스프라이트 라이브러리: 분류 · 라벨 → 프레임(아틀라스는 파일에 하나 — 배치가 깨지지 않게) | 유니티 Sprite Library |
| 파라미터 | 머티리얼 파라미터 묶음(`*.matparams.xml`), 머티리얼 층([MaterialGraph](MaterialGraph.md)), 외형 구간 머티리얼(`AppearanceSectionDef`) | 언리얼 MPC · Material Layers · Material Slot |
| 파라미터 | VFX 방출기 `InheritKey` · 사용자 파라미터, 오디오 라이브러리 · 믹서 `_base`, 이벤트 `_parentEvent` | Niagara 부모 이미터 · MetaSound Preset |
| 인스턴스 값 | `InstanceCustomData`(float4 둘, 배치 키 아님), `MaterialInstance` 는 드로우 단위 덮기 | 언리얼 Custom Primitive Data |
| 조립 | 씬 인스턴스 · 씬 상속 · 데이터 레이어(아래 "씬 조립", 조건의 `layer` 축) | 언리얼 Level Instance · Data Layers, Godot 씬 상속 |
| UI | 스타일 시트 `_base`, `<TypeVariation>`, 에디터 테마 토큰 `_base` | Godot Theme Type Variation |
| 기타 | 리그 · 리타겟 · 물리 · 소켓 · 알림 표 · 입력 레이어에 `InheritKey` 또는 표 한 줄 | — |

- **뺀 것:** 물리 셰이프 · 충돌 쌍, 리그 · 리타겟 안의 뼈 목록(순서가 뜻)은 통째로 바뀐다. 입력 맵의 `<override>` · `<remove>` 는 실행 중 쌓기다([InputMapBundle](../InputMapBundle.md)).
- **LOD:** 애니메이션은 등급 한 축(`AnimationLODState::_lodGrade`)으로 그래프 `<layer>` · 블렌드 `<node>` · 리그 노드가 `max_lod` 칸에서 꺼지고 `_nodeFadeSeconds` 동안 가중치를 옮긴다(`AnimationLODFade`, 본 LOD `SkeletonBoneLOD` 와 따로). `ClothComponent` 는 조상 스켈레탈 메시의 등급으로 반복 수를 낮추고(`_listLODIterationCount`) `_maxLOD` 를 넘으면 시뮬레이션을 내린다 — 다시 켜면 지금 고정 자세에서 새로 만든다(확인 `ClothLODTest` · `game.empty.clothlod`).
- **메시 런타임 조립(언리얼 Mutable 자리):** 부품 피팅 · 모프 굽기(`AppearanceMeshPart::assemble`) → 구간 있는 메시 하나로 병합(`mergeParts`) → 결과 캐시(`AppearanceBakedMeshCache`) · 진행 중 짓기 나눔(`CharacterAppearanceInFlightTable`), 짓기는 워커. 캐릭터마다 텍스처 층을 굽지 않는다 — 염색 · 마스크 · 표면 상태는 머티리얼 매개변수라 군중이 머티리얼을 나눈다. 고정 차림 아틀라스는 에디터가 쿠킹한다(`OutfitAtlasSpec`).

## 합치지 않은 것과 까닭

| 코드 | 까닭 |
|---|---|
| `PrefabOverrides` 의 합치기 | 컴포넌트 목록의 열쇠가 칸 값이 아니라 `이름#n` 이고 원소 이름이 타입이다. 씬 인스턴스는 프리팹에서 사라진 대상을 오류가 아니라 경고로 넘겨야 한다(프리팹을 고쳤다고 씬이 읽히지 않으면 안 된다). 변형은 같은 형식을 쓰되 `validateOverrideTargets` 로 엄격하게 먼저 본다 |
| `_listRenderPassRef` | 부모 하나가 아니라 여러 파일을 섞는 믹스인이다. 단일 부모로 옮기면 첨부 묶음을 파이프라인마다 베껴야 한다. 합친 결과의 이름이 겹치면 오류인 것도 다르다(덮어쓰지 않는다) |
| JSON 형식 | 설정은 층(`ConfigManager`)으로 겹친다. 프리팹 JSON 은 변형을 읽고 쓰지만 기본값은 빼지 않는다. 에디터 테마 토큰(`*.tokens.json`)은 파일이 자기 부모를 고르므로 `_base` 를 적고 키 표를 덮는다(`EditorThemeTokens`) |
| 오디오 이벤트의 `_parentEvent` | 이벤트가 `_listEvent` 래퍼 안에 있어 카탈로그 규칙(루트 바로 아래 되풀이)에 맞지 않는다. 합치기는 `DataInheritance` 그대로이고 이벤트 하나를 가상의 문서로 넘긴다. 파일 합치기가 목록 원소의 `_base` 를 지우므로 이름이 다르다 |
| `AudioEffectDef` 목록 | 이름이 비면 종류 이름을 쓰는 규칙이라 열쇠가 비는 원소가 있다. 적으면 통째로 바뀐다 |
| 행동 실행기(행동 트리 · 스케줄 · 대화 · 어빌리티 · 기믹 회로) | 모델이 다르다. 언리얼도 BT · StateTree · GAS 를 나누고 Gameplay Tag 로 어휘만 공유한다 — 여기도 조건 · 동작 이름만 공유한다(위 "행동 어휘") |
| 스케줄 `<Archetype>` 을 항목 `_base` 로 | (1) `_base` 는 같은 종류만 가리키는데 아키타입은 NPC 가 아니다. (2) 행사의 `archetypes="villager"` 는 사슬 소속을 묻는데 펼친 글에는 `_base` 가 남지 않는다. (3) 같은 id 루틴이 통째 교체가 아니라 속성 합치기가 되고 순서가 뒤집힌다. 추상 항목과 펼친 뒤의 소속 표가 생기면 옮긴다 |
| `SkeletonVariant` 의 칸 | 뼈 비율은 곱해지고 클립 규칙은 쌓이며 본은 뒤에 붙는다. 덮어쓰면 변형의 변형이 다른 값이 된다. 사슬(순환 · 깊이 · 없는 부모)만 공통이고 칸은 `Skeleton::mergeVariantText` 가 얹어 평평한 `<Skeleton>` 글을 낸다(쿠킹본에 `_base` 가 남지 않는다). 부모 본 번호를 바꾸지 않는지는 합친 뒤에 본다 |

## 상용 엔진과 비교

언리얼 CDO 델타 · 유니티 Prefab Variant · Godot 씬 상속과 같은 모델이다. 다른 점: 목록을 인덱스가 아니라 열쇠(이름)로 짚어 부모에 끼워 넣어도 자식이 어긋나지 않고, 부모가 지운 대상을 가리키면 오류다. 카탈로그는 자산 단위가 아니라 표 안의 행끼리 물려받는다. 쿠킹이 XML 파일 자체를 펼쳐 런타임 코드 경로가 하나다.
