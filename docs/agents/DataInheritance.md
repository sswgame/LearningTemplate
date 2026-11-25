# DataInheritance

데이터 파일 상속의 규칙. 상속은 루트에 `_base="<리소스 id>"`(카탈로그 항목은 `_base="<id>"`)를 적고 다른 칸만 쓴다 — 형식마다 합치기 코드를 쓰지 않는다(리플렉션 형식 `PROPERTY( InheritKey )`, 손 XML 형식은 스키마, 카탈로그는 설정 표 한 줄). 모든 리소스 글은 `ResourceComposer::compose` 한 길(부모 → 조건 → 덮어쓰기 층)을 지나고 런타임은 쿠킹이 펼친 결과만 읽는다. 경로 · 리소스 읽기 · 데이터 칸 읽기는 [AssetLoading](AssetLoading.md).

## 파일에 적는 법

| 자리 | 규칙 |
|---|---|
| 속성 | 자식이 적은 것만 덮는다(빈 값 `attr=""` 도 적은 것) |
| 구조체 칸 | 안으로 들어가 같은 규칙. 두 번 적으면 오류(`FieldRepeated`) |
| 열쇠 있는 목록 | 원소의 `InheritKey` 칸으로 합친다: 작업 없음 = 있으면 합치고 없으면 끝에 더함, `_op="Replace"` · `_op="Remove"`(없는 열쇠는 오류), `_before="X"` · `_after="X"`(둘 다 적으면 오류). 위에서 아래로 차례로 얹는다 |
| 열쇠가 글인 목록(`kTextKey` — 머티리얼 `_alwaysDefines`) | 글 하나씩 더하고 `<item _op="Remove">글</item>` 로 지운다 |
| 열쇠 없는 목록 · 글 값 | 자식이 적으면 통째로 바뀐다. 빈 원소(`<_listInput />`)는 비운다 |

- 순환 · 없는 부모 · 형식이 다른 부모 · 깊이 초과(`_dataInheritanceMaxDepth`)는 로드 실패 — 로드 강제. 부모 없이 자식 칸만으로 읽지 않는다.
- 열쇠를 잘못 적으면 새 원소가 더해진다 — 빈 칸은 그 형식의 검증이 잡으니 새 형식은 검증을 꼭 둔다.

## 새 형식을 올리는 법

1. `SW_ASSET_TYPE` 한 줄([AssetLoading](AssetLoading.md) "리소스 읽기 입구"). 다른 리소스를 가리키는 칸은 `AssetRef<T>`.
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

- 맵 전체 값(시간 · 거리 · 기본 레이어)은 기본 맵에만 적는다. 예외 `virtualCursor`(`common/input/virtualcursor.input.xml`, 묶음에 한 번). 미니게임 하나만 쓰는 가상 커서는 그 미니게임의 레이어에 적는다(`<layer virtualCursor>`).
- 레이어는 한 파일이 정의하고, 공통 파일은 자기 레이어를 하나 갖는다(공통 파일이 게임 키를 막지 않게).
- `<override>` 는 `layer` 를 적지 않으면 덮는 액션의 레이어에 남는다. 한 액션은 묶음에서 한 번만 덮거나 지운다.
- 같은 레이어 · 발화 규칙 · 입력의 두 액션, 없는 액션을 가리킨 차이, 깨진 묶음은 오류로 게임을 띄우지 않는다(`InputMap::loadBundle` — 게임 시작 · 쿠킹 · `InputMapBundleTest` · 패널 다시 읽기 강제).
- **액션 이름**: 두 게임 이상이 같은 뜻이면 접두어 없는 공용 이름 하나(`Move`, `Interact`; 정의 `Content/common/input/`, 코드는 `Engine/Input/Map/CommonInputActions.h`). 한 게임만 쓰면 게임 접두어(`Farm.Use`) — 다른 게임에 같은 뜻이 생기면 공용으로 옮긴다. 묶인 키가 아니라 뜻이 기준. 장치 이름공간(`Camera.Pan`)과 별도 맵 이름(`Photo.Move`, `UI.Pause`)은 접두어를 유지.
- 게임 코드는 키를 묻지 않는다. 한 공용 액션을 읽는 쪽은 하나로 정한다(디렉터 둘이 읽으면 둘 다 발화).
- 우선순위: 공통 < 키트 < 게임 차이 < 플레이어 재배치(`UserSettingsManager::reapplyAll`). 재배치 겹침은 함께 켜진 레이어까지 본다(`canLayersFireTogether`).
- 입력 맵 패널에서 게임만의 변경은 차이 파일에 `<override>` 로 적는다(공통 파일을 고치면 다른 게임도 바뀐다).

## 합치지 않은 것

- `PrefabOverrides` 합치기(열쇠가 지역 id 키, 씬 인스턴스는 사라진 대상을 경고로 넘긴다), `_listRenderPassRef`(여러 파일 믹스인 — 이름이 겹치면 오류), JSON 설정(`ConfigManager` 층), 오디오 `_parentEvent`(래퍼 안 이벤트), `AudioEffectDef` 목록(열쇠가 비는 원소), 스케줄 `<Archetype>`, `SkeletonVariant` 칸(곱해지고 쌓인다 — `Skeleton::mergeVariantText`)은 위 구조에 올리지 않는다. 같은 일을 다시 하려면 이 줄의 까닭이 사라졌는지 먼저 본다.
