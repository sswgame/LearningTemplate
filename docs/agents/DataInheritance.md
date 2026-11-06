# DataInheritance

> 질문: 데이터 파일이 부모를 가리키고 차이만 적게 하려면 무엇을 하는가?
> 독자: 파이프라인 · 머티리얼 · 프리팹 데이터를 쓰거나, 새 데이터 형식(아이템 · 어빌리티)을 이 구조에 올리는 사람과 에이전트.
> 결론: 루트에 `_base="<리소스 id>"` 를 적고 다른 칸만 쓴다. 형식마다 합치기 코드를 쓰지 않는다 — 리플렉션 형식은 `PROPERTY( InheritKey )` 한 줄이면 되고, 손으로 읽는 XML 형식은 규칙을 값으로 적는다. 런타임은 쿠킹이 펼친 결과만 읽는다.

구현: `Source/Engine/Serialization/Inherit/DataInheritance.h`.

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
| 그 밖의 자식 원소(열쇠 없는 목록 · 글 값) | 자식이 적으면 통째로 바뀐다. 빈 원소(`<_listInput />`)는 비운다 |

| 목록 원소에 적는 것 | 뜻 | 오류 |
|---|---|---|
| (작업 없음) | 있으면 적은 칸만 합치고, 없으면 끝에 더한다 | 같은 열쇠를 두 번 적음 |
| `_op="Replace"` | 부모 칸을 물려받지 않고 바꾼다. 자리는 그대로다 | 없는 열쇠 |
| `_op="Remove"` | 지운다 | 없는 열쇠 |
| `_before="X"` · `_after="X"` | X 앞 · 뒤로 옮긴다. 칸 없이 자리만 옮겨도 된다 | 없는 X, 둘 다 적음 |

위에서 아래로 차례로 얹는다. 앞 줄이 더하거나 지운 결과를 뒤 줄이 본다.

오류: 순환, 없는 부모, 형식이 다른 부모, 깊이 초과(`Config/Engine/EngineConfig.json` 의 `_dataInheritanceMaxDepth`). 오류면 로드 실패다. 부모 없이 자식 칸만으로 읽지 않는다.

열쇠를 잘못 적으면 "없는 원소를 더한 것" 이 된다. 삭제 · 교체만 없는 열쇠가 오류인 까닭은, 차이 파일에 새 원소를 적을 때마다 표시를 강제하지 않기 위해서다. 더해진 원소의 빈 칸은 그 형식의 검증(파이프라인은 `validate`)이 잡는다.

## 새 형식을 올리는 법

**리플렉션 형식(`ReflectedXMLFile::loadDesc` · `XMLSerializer::loadFile` 로 읽는 것).** 읽기는 이미 합친다. 할 일은 목록 원소의 이름 칸에 표시를 하는 것뿐이다.

```cpp
REFLECT()
struct ItemDef
{
    REFLECT_BODY();
    PROPERTY( InheritKey )
    string _name;
    PROPERTY()
    float32 _weight{ 1.0f };
};
```

열쇠가 없는 목록은 통째로 바뀐다. 문서 루트 이름이 형식 이름이므로 팩 쿠커도 알아서 펼친다.

**손으로 읽는 XML 형식(`XMLFields::readFields` 로 칸을 읽는 것).** 규칙을 값으로 적고 파일을 읽기 전에 펼친다. 본보기는 `MaterialXML::makeInheritanceSchema` 다.

```cpp
DataInheritanceSchema makeSchema()
{
    DataInheritanceSchema schema{ "ItemTable" };
    schema._listKeptAttribute.emplace_back( "formatVersion" );
    DataInheritanceSchema items{ "_items" };
    items._keyAttribute = "id";
    schema._listField.push_back( std::move( items ) );
    return schema;
}
SW_DATA_INHERITANCE_SCHEMA( ItemTable, &makeSchema ); // 팩 쿠커가 루트 이름으로 찾는다

// 읽기
string flatText;
if ( DataInheritance::findBasePath( text ).empty() == false && DataInheritance::flattenFromResources( text, sourceName, makeSchema(), flatText ) == false )
    return false;
```

맨 위 노드의 `_keyAttribute` 를 채우면 루트의 자식들이 곧 목록이다(래퍼 원소가 없는 형식).

그 밖에 형식마다 해야 할 일은 둘이다.

- `Config/Dev/AssetReferenceFields.json` 에는 `_base` 가 이미 있어 참조 색인 · 린트가 따라간다. 핫 리로드가 있는 종류면 표의 칸을 `AssetReferenceUse::InlinedAtLoad` 로 둔다 — 부모가 바뀌면 `AssetReloadCascade` 가 자식을 의존 순서로 다시 읽는다.
- 저장이 있으면 `ReflectedXMLFile::saveDescAsChild`(부모와 다른 칸만) · `saveDesc`(형식 기본값과 다른 칸만)를 쓴다. 손 형식은 `DataInheritance::makeDelta` 다. 두 글은 같은 직렬화기가 쓴 것이어야 한다.

## 쿠킹과 런타임

- 팩 쿠커(`PackCooker`)는 소스를 담을 때 `_base` 가 있는 파일을 펼친 한 벌로 담는다. 런타임은 글 검색 한 번으로 "부모 없음" 을 알고 지나간다. 쿠킹본만 읽는 구성에서 `_base` 를 만나면 오류다(`DataInheritance.NotFlattened`).
- 부모를 `AssetIO` 로 읽으므로 그 읽기가 쿠킹 작업의 입력이 된다. 의존 목록을 따로 적지 않아도 부모가 바뀌면 자식의 쿠킹 키가 바뀐다(`CookJob`).
- 에디터 · 개발 실행은 원본을 읽어 합친다.

## 도구가 묻는 것

- 이 칸이 어디서 왔나: `DataInheritance::flatten` 의 `DataInheritanceProvenance`(`findOrigin( "_listPass[Present]._cullMode" )`), 프리팹은 `PrefabCache::findFieldOrigin`.
- 이 부모의 자식 목록: `DataInheritance::collectChildren`(파일을 훑는다). 에디터는 `EditorReferenceIndex::findReferrers` 도 같은 답을 준다.

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

## 기본값을 쓰지 않는 저장

저장은 부모(없으면 형식 기본값)와 같은 칸을 쓰지 않는다. 프리팹은 뺀 글을 다시 읽어 값이 같을 때만 쓰고, 다르면 모든 칸을 쓰며 경고한다(`PrefabAsset.DefaultOmitMismatch`).

**코드에서 기본값을 바꾸면 그 칸을 적지 않은 모든 데이터의 값이 바뀐다.** 언리얼의 CDO 와 같다. 바꾸면 안 되는 값은 기본값을 고치지 말고 데이터에 적는다. 값이 그림 · 규칙을 바꾸면 쓰는 쪽 자료를 대조한다. 필수 칸(`PROPERTY( Required )`)은 저장이 지우지 않는다.

## 합치지 않은 것과 까닭

| 코드 | 까닭 |
|---|---|
| `PrefabOverrides` 의 합치기 | 컴포넌트 목록의 열쇠가 칸 값이 아니라 `이름#n` 이고 원소 이름이 타입이다. 씬 인스턴스는 프리팹에서 사라진 대상을 오류가 아니라 경고로 넘겨야 한다(프리팹을 고쳤다고 씬이 읽히지 않으면 안 된다). 변형은 같은 형식을 쓰되 `checkOverrideTargets` 로 엄격하게 먼저 본다 |
| `_listRenderPassRef` | 부모 하나가 아니라 여러 파일을 섞는 믹스인이다. 단일 부모로 옮기면 첨부 묶음을 파이프라인마다 베껴야 한다. 합친 결과의 이름이 겹치면 오류인 것도 다르다(덮어쓰지 않는다) |
| JSON 형식 | 설정은 층(`ConfigManager`)으로 겹친다. 프리팹 JSON 은 변형을 읽고 쓰지만 기본값은 빼지 않는다 |

머티리얼의 `parent` 는 합쳤다(`_base`).

## 상용 엔진과 비교

기억에 의존한 설명이다. 판마다 다를 수 있다.

| 엔진 | 방식 | 여기와 같은 점 · 다른 점 |
|---|---|---|
| 언리얼 | 아키타입(CDO) 위에 다른 칸만 쓰는 델타 직렬화, 블루프린트 자식 클래스 | 부모 값을 고치면 적지 않은 칸이 따라간다. 기본값을 코드에서 바꾸면 데이터가 따라가는 위험도 같다. 배열은 통째로 델타이지만 여기는 열쇠가 있으면 원소 단위다 |
| 유니티 | Prefab Variant(`m_Modifications` — 경로와 값의 목록) | 프리팹 변형이 같은 모델이다. 유니티 배열은 인덱스로 짚어 부모에 끼워 넣으면 자식이 어긋나지만, 여기는 이름으로 짚어 어긋나지 않는다 |
| Godot | 씬 상속(부모 씬 + 바뀐 속성 · 더한 노드) | 노드 경로로 짚는 것이 열쇠와 같다. 부모가 지운 노드를 자식이 가리키면 여기는 오류다(변형) |

여기서만 하는 일은 쿠킹에서 펼치는 것이다. 세 엔진도 쿠킹본에는 결과만 남기지만, 여기는 XML 파일 자체를 펼쳐 런타임 코드 경로가 하나다.
