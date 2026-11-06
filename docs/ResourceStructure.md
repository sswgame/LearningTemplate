# 리소스 공통 구조 설계

> 질문: 모든 리소스를 최소 파일로 공유하고 여러 변형을 만들려면 구조를 어떻게 바꾸는가?
> 독자: 이 구조를 구현하거나 새 리소스 형식을 더하는 세션(에이전트와 사람).
> 결론: 리소스 종류 정의 · 합성 읽기 · 공유 캐시 · 타입 있는 참조를 한 벌로 만들고, 변형은 부모 → 조건 → 덮어쓰기 층의 합성 한 길로 처리한다. 행동 데이터는 실행기를 합치지 않고 조건 · 동작 어휘만 공유한다. 기준은 지금 닮은 파일 수가 아니라 앞으로의 기능이다.

엔진 비교는 기억에 의존한 설명이다. 구현 전에 그 기능의 문서를 다시 확인한다.

## 지금 구조와 문제

| 층 | 지금 | 문제 |
|---|---|---|
| 종류 정의 | `SW_ASSET_TYPE`(접미사 · 판 · 쿠킹), `SW_RESOURCE_DATA_KIND`(경로 규칙 · 검증 로더)가 따로 있다 | 같은 종류를 두 표에 적는다. `AssetDataKind::loadCatalog` 는 읽고 버리는 검증 전용이다 |
| 합성 | 파일 `_base`(`DataInheritance`), 항목 `_base`(`DataCatalogInheritance`), 프리팹 덮어쓰기, 스켈레톤 변형이 있고, 읽는 곳은 `XMLDocument::parseData` · `ReflectedXMLFile::parseDesc` · `Material` · `Skeleton` · `PrefabAsset` 로 나뉜다 | 조건별 값과 덮어쓰기 층을 넣을 자리가 형식마다 생긴다 |
| 공유 | `SharedAssetCache<T>` 와 `GameDataCache<T>` 가 따로 있고, 카탈로그 다수는 쓰는 쪽이 멤버 · 지역 변수로 사본을 읽는다 | 행동 트리 · 스케줄 · 퀘스트 등이 공유되지 않고 다시 읽기도 닿지 않는다 |
| 참조 | `string` 경로 칸과 이름 목록 `Config/Dev/AssetReferenceFields.json` | 칸이 어느 종류를 가리키는지 형식이 모른다. 이름 표는 사람이 맞춘다 |
| 행동 | 조건은 `ConditionExpr`, 동작은 `ActionRegistry` 하나다(단계 6). `ScheduleActivityRegistry`(가는 곳의 규칙)는 어휘가 아니라 활동 종류라 남는다 | — |

## 목표 구조

```
리소스 파일
  └─ 합성 읽기: 부모(_base) → 조건(_when) → 덮어쓰기 층 → 펼친 글
       └─ 종류 정의(ResourceType)의 로더: 글 → const T
            └─ 공유 캐시: 경로 하나에 객체 하나, 다시 읽기, 로드 상태
                 └─ 타입 있는 참조 AssetRef<T> 가 꺼낸다 → 실행 상태는 쓰는 쪽이 따로 가진다
```

새 형식은 종류 정의 한 줄로 합성 · 공유 · 다시 읽기 · 쿠킹 · 참조 색인 · 에디터 선택기를 얻는다.

### 1. 종류 정의 `ResourceType`

`AssetTypeDef` 를 확장해 `AssetDataKind` 를 흡수한다. 등록 줄은 `SW_ASSET_TYPE` 하나다.

| 칸 | 출처 |
|---|---|
| 이름 · 접미사 · 판 · 쿠킹 접미사 · 제외 묶음 | `AssetTypeDef` |
| 경로 규칙(`/data/items.xml` 처럼 접미사로 가를 수 없는 파일) | `AssetDataKind` |
| 로더 · 다시 읽기 방식 · 메모리 태그 | 예전 `SharedAssetCacheTraits`(지금은 `AssetTypeLoader`) |

- 검증 로더는 따로 두지 않는다. 스키마 테스트도 진짜 로더를 부른다.
- 등록은 `SW_ASSET_TYPE( id, 정의.withPathRule( … ).withLoader( AssetTypeLoader::of<T>( 태그, 방식 ) ) )` 한 줄이다. 헤더의 `kAssetType` 은 쿠킹이 쓰는 사실만 들고, 로더는 표의 사본에만 있다(로더 함수가 기능 .cpp 에 있다).
- 공유 캐시가 있는 종류는 `SW_SHARED_ASSET_CACHE( id, T, 정의 )`(Engine) · `SW_MODULE_SHARED_ASSET_CACHE( id, T, 내보내기, 정의 )`(모듈) 한 줄이 같은 정의로 종류 등록 · 캐시 인스턴스화 · 레지스트리 등록을 함께 한다. 캐시는 정의의 이름 · 로더 · 다시 읽기 방식만 읽고 따로 적는 특성 표가 없다 — 이름이 둘이던 짝(`Rig`/`RigAsset`, `SpriteClip`/`SpriteClipAsset`, `Fracture`/`FractureAsset`, `BodyShapeSet`/`bodyshape`, `InteractionCatalog`/`interactions`, `ElementRuleTable`/`elementrules`)은 정의의 이름 하나가 되었고 에디터 리로드 길도 `AssetReloadRoute::sharedCache( 정의 )` 로 이름을 정의에서 꺼낸다.
- 제약: 팩 쿠킹은 게임 모듈을 올리기 전에 돈다(`HeadlessSubsystem::runCookTask`). 쿠킹이 알아야 하는 합성 규칙은 정적 등록이 아니라 데이터(`Config/Dev/`)에 둔다. 이 까닭은 `DataCatalogInheritance.json` 과 같다.

### 2. 합성 읽기 `ResourceComposer`

**한 길이다.** `parseData` · `parseDesc` · 머티리얼 · 스켈레톤의 펼치기를 `ResourceComposer::compose( text, id, context )` 하나로 모은다. 규칙 출처(리플렉션 · `SW_DATA_INHERITANCE_SCHEMA` · 카탈로그 표)는 지금처럼 루트 이름으로 찾는다.

```
1. 부모: 파일 _base, 항목 _base            (구현됨)
2. 조건: 플랫폼 · 품질 · 언어 · 지역 · 기능 태그  (신규)
3. 층: DLC · 모드 · 라이브 운영 · 계절          (신규)
```

세 단계 모두 `DataInheritance` 의 합치기 규칙(열쇠 · `_op` · `_before` · `_after`)을 그대로 쓴다. 차이 하나를 얹는 일이므로 합치기 코드는 새로 없다.

**프리팹:** `PrefabOverrides` 는 열쇠 모양이 달라 유지한다(`DataInheritance.md`). 대신 문서 머리의 조건 · 층 단계는 같은 길을 지난다.

**스켈레톤:** `SkeletonVariant` 의 자체 사슬과 깊이 검사를 지우고 공통 사슬(`DataInheritance::collectChain`)로 옮겼다. "부모 본 번호를 바꾸지 않는다" 는 합친 뒤의 검증으로 남긴다. 칸 덮어쓰기로는 풀리지 않으므로(뼈 비율은 곱해지고 클립 규칙은 쌓이며 부모 루트 `<Skeleton>` 과 형식도 다르다) 합치기 함수(`SW_DATA_INHERITANCE_MERGE`)가 평평한 `<Skeleton>` 글을 낸다. 예외 길은 없다 — 읽기와 쿠커가 같은 함수를 지나고 쿠킹본에 `_base` 가 남지 않는다. 임포터의 54 개 파일은 고치지 않는다. 쌓인 값(`SkipIfEmpty` 칸 `_listVariantState`)은 펼친 글에만 있어 공용 스켈레톤의 글 지문은 그대로다.

**규칙 출처의 순서:** 문서 표 · 카탈로그 표(`Config/Dev/DataCatalogInheritance.json`) → 합치기 함수 → 손 형식 → 리플렉션 형식. 쿠킹은 Engine 코드만 보므로 게임 · 키트 형식은 표에만 있어도 되고, 표와 코드가 어긋나지 않는지는 테스트가 본다.

#### 조건별 값

언리얼 Device Profile · 플랫폼별 값, Godot 기능 태그, 유니티 플랫폼별 임포트 설정에 해당한다. 형식은 둘이다.

글 형식은 그 자리에 차이를 적는다.

```xml
<VFXEffectDef _name="Sparks" _maxParticles="4096">
	<_when test="mobile || quality.low" _maxParticles="512" />
</VFXEffectDef>
```

- 항목 · 구조체 칸 어디에나 적을 수 있고, 위에서 아래로 얹는다.
- 식은 [조건 어휘](#5-행동-어휘)와 같은 문법의 컴파일된 식이다. 값은 태그 집합 `ResourceContext` 에서 온다.

바이너리(텍스처 · 오디오 · 메시)는 안을 고칠 수 없어 경로를 바꾼다. Godot 의 언어별 리소스 교체와 같다.

```xml
<ResourceRemap>
	<Remap id="common/ui/logo.dds" when="locale.ko" to="common/ui/logo_ko.dds" />
</ResourceRemap>
```

**축마다 정해지는 때가 다르다.**

| 축 | 정해지는 때 | 쿠킹본 |
|---|---|---|
| 플랫폼 · 빌드 대상 | 쿠킹 | 펼쳐서 없어진다. 런타임 비용이 없다 |
| 품질 · 언어 · 지역 · 기능 태그 | 로드 | `_when` 이 남는다. 값이 바뀌면 그 축을 쓰는 리소스를 다시 읽는다(`reloadShared`) |

**한계:** 캐시 열쇠는 경로뿐이므로 한 프로세스의 문맥은 하나다. 서버가 플레이어마다 다른 지역 값을 고르려면 문맥이 아니라 데이터의 키로 나눈다.

**네트워크:** 게임플레이 값이 클라이언트마다 달라지면 판정이 어긋난다. 합성 결과의 해시를 접속 때 대조해 서버와 다르면 거절한다. 품질 축이 시뮬레이션 값을 건드리는지는 이 대조가 드러낸다.

#### 덮어쓰기 층

언리얼 Composite Data Table 과 팩 우선순위에 해당한다. 층은 순서 있는 목록(`engine → common → game → dlc → mod`)이고, 층 안의 `<같은 리소스 id>` 파일은 원본을 암묵적 부모로 가진 차이다.

- 게임 쿠킹은 자기 층까지 펼친다. DLC · 모드 층은 팩을 올릴 때 합성한다. 그래서 합성 코드는 배포 구성에도 남는다.
- 항목 id 를 바꾸지 않으므로 세이브는 그대로다.
- 신뢰 경계: 층 파일은 외부 입력이다. 합성 뒤에 그 형식의 검증을 다시 돌리고, 온라인 게임은 위 해시 대조로 거른다.

### 3. 공유 캐시

`SharedAssetCache<T>` 하나로 합치고 `GameDataCache` 를 지운다. 옛 객체를 살려 두는 동작은 `SharedAssetReloadMode::Detach` 와 `shared_ptr` 가 대신한다.

- **모듈 캐시:** 키트 · 게임 종류의 표를 그 모듈이 인스턴스화하고 모듈 수명으로 등록한다(`SW_MODULE_SHARED_ASSET_CACHE`, `Engine/Module/ModuleSharedAssetCache.h` — 모듈 서브시스템이 캐시를 들고 `AssetManager::registerAssetCache` 로 오르내린다). 핫 리로드의 내림은 `ModuleObjectRegistrar` 와 같은 사슬이다. `XMLCatalog` 형식 55 종 모두가 가진다(단계 2 · 4).
- **창구:** `SharedAssetCache<T>::acquire( 경로 )` · `request( 경로 )` · `getReloadCount()` 를 바로 부른다. `XMLCatalog` 에 감싸개를 두지 않는다 — 내보내는 카탈로그가 베이스 멤버를 모두 인스턴스화해 캐시가 없는 카탈로그에서 링크가 깨졌다.
- **사본을 못 만들게 한다(단계 4, 끝남):** `XMLCatalog::loadFromAsset` 은 protected 이고 종류의 로더(`AssetTypeLoader`)만 friend 다. 쓰는 쪽은 `shared_ptr<const T>` 를 쥐고, 게임 범위 서비스는 `GameInstance::loadRequiredCatalog( shared_ptr<const T>& )` 가 받아 건다. 테스트 · 에디터 미리 보기는 `loadFromXMLText` 를 쓴다.
- **다시 읽기:** 카탈로그는 기본이 제자리(`InPlace`)라 게임 범위 서비스 · 디렉터가 쥔 같은 객체에 닿는다 — 쥔 쪽은 항목 포인터를 프레임을 넘어 들지 않고 id 로 찾는다. 진행 중인 세션이 옛 정의를 읽어야 하는 표(상호작용 · 원소 규칙)와 옮길 수 없는 표(`CurrencyCatalog` 는 정책이 자기를 참조, `MonsterCatalog` 는 복사 금지)만 떼어 낸다(`Detach`). 시나리오 `editor/catalogshared` 가 쥔 쪽의 값으로 본다.
- **정의와 실행 상태:** 공유 객체는 `const` 다. 실행 상태가 섞인 카탈로그는 나눈다 — `BehaviorTree` · `BehaviorTreeRunner`, 어빌리티 데이터(`AbilityCatalog`)와 클래스 팩토리(`AbilityClassRegistry`, 게임 코드). 읽기 전에 쓰는 쪽이 넣던 검사 문맥(작물의 알려 둔 계절)은 데이터로 옮긴다(작물 파일 루트 `seasons`).

`MeshCache` · `TextureCache` · `MaterialCache` · `PrefabCache` 는 GPU 수명이나 쓰는 쪽의 수정이 있어 합치지 않는다(`SharedAssetCache.h` 머리말). `AssetHandle` · 로드 상태 · 종류 정의는 공유한다.

### 4. 타입 있는 참조 `AssetRef<T>`

언리얼 `TSoftObjectPtr<T>`, 유니티 `AssetReferenceT<T>` 에 해당한다. 경로와 대상 종류를 품은 값이고 스스로 읽지 않는다.

```cpp
PROPERTY()
AssetRef<BehaviorTreeAsset> _tree;

shared_ptr<const BehaviorTreeAsset> tree = _tree.acquire();  // 또는 request() 로 비동기
```

- 읽기: 접미사가 대상 종류와 다르면 오류다. 잘못 연결한 참조가 로드 때 잡힌다.
- 파생: 의존성 수집 · 미리 읽기(`PreloadSet`) · 쿠킹 입력 · 참조 색인 · 에디터 선택기 필터가 모두 이 칸을 본다.
- 이름 표를 없앤다: 리플렉션 파서가 참조 칸 목록을 생성물로 내보내고, `AssetReferenceFields.json` 은 손으로 읽는 XML 칸이 남은 동안만 유지한다. 손으로 읽는 칸은 `XMLFields` 쪽 선언으로 옮겨 표를 지우는 것이 끝이다.
- 값 모양: `AssetRefBase`(직렬화 층, 경로 + `const AssetTypeDef*`)와 `AssetRef<T>`(에셋 층, 칸을 더하지 않는다). 글 처리기는 값 주소만 받으므로 대상 종류를 프로퍼티 메타가 아니라 값이 품는다. 그래서 `vector<AssetRef<T>>` · 중첩 구조체 · 덮어쓰기 패치가 같은 검사를 지난다. 파서는 `TComponentHandle<T>` 처럼 칸 형식을 내장 이름 `AssetRef` 하나로 모으고 `AssetPath` 메타를 켠다.
- `T` 는 칸이 만들어지는 곳에서 완전해야 한다(`unique_ptr<T>` 소멸자와 같은 규칙). 생성자를 .cpp 에 둔 컴포넌트는 헤더에 앞선 선언만 둔다. `acquire` · `request` 가 멤버 템플릿인 까닭도 같다 — 칸 선언이 `AssetRefCache<T>` 를 인스턴스화하면 뒤에 오는 캐시 헤더의 특수화가 컴파일 오류다.
- 생성 목록: 파서가 대상마다 `AssetReferenceFields.list` 를 쓰고 빌드가 저장소의 표 `Config/Dev/AssetReferenceFields.generated.json` 으로 모은다(주인별, 이 구성이 짓는 주인의 줄만 갈아 끼운다). 커밋 전 린트와 `move-asset` 이 빌드 없이 돌기 때문에 표를 저장소에 둔다. 낡음은 `--check-asset-refs` 가 잡는다.
- 전환하지 않는 칸: 종류가 여럿이거나(오디오 클립 · 그래프) 내장 이름을 함께 받거나(`_meshID` 의 `Cube`, `_materialPath` 의 `None`), 편집 사본이 치는 중인 글을 들거나(`SequenceTrackItem::_prefab`), 인턴 열쇠로 쓰는(`hashed_string`) 칸. 카탈로그 칸은 종류 정의(단계 1)가 `kAssetType` 을 주면 옮긴다. 이런 칸은 `PROPERTY( AssetPath )` 로 두면 생성 목록에 든다.

### 5. 행동 어휘

**실행기는 합치지 않는다.** 행동 트리(틱 반응), 스케줄(게임 시간표), 대화(사람의 선택 대기), 어빌리티(수명 · 복제), 기믹 회로(고정 스텝 상태)는 모델이 다르다. 언리얼도 Behavior Tree · StateTree · GAS 를 나누고 Gameplay Tag 로 어휘를 공유한다.

**조건 `ConditionExpr`** (`Engine/Vocabulary/ConditionExpr.h`)

- 문법은 `GameFlags` 식을 넓힌다: `flag && !other`, `tag(Quest.Done)`, `weather(rain, storm)`, `affinity >= 4`.
- 이름은 `SW_CONDITION( 이름, 칸 스키마, 함수 )` 로 등록한다.
- **읽을 때 컴파일한다.** 후위 배열과 함수 포인터로 바꿔, 매 틱 글을 해석하지 않는다. 모르는 이름은 로드 오류다.
- 스케줄의 `weathers="rain"` 같은 속성은 같은 식으로 컴파일하는 줄임말로 남겨 데이터를 옮기지 않는다.
- 결정성: 확률 조건은 문맥의 씨앗을 쓴다. 지금 스케줄의 "날과 키의 해시" 규칙을 문맥이 제공한다.
- `ResourceContext` 의 `_when` 도 같은 문법이므로 파서는 Engine 에 둔다. 그냥 이름을 넘길 조건(`pBareCondition`)을 고를 수 있어 `_when` 은 문맥 태그 조건을 넘기면 된다.
- 세계는 형식 있는 칸의 문맥(`GameplayContext`)으로 넘긴다. 칸 형식은 그 값을 가진 층이 정한다(`GameFlagsSlot` · `BlackboardSlot` · `ScheduleConditionSlot`). 없는 칸은 "모름" 이라 거짓이다. 언리얼 `FGameplayEffectContext` 와 같은 자리다.
- 값이 없음은 NaN 이다. 없는 블랙보드 키는 어떤 비교에도 거짓이다(`hasBlackboard` 로 묻는다). 예전 블랙보드 조건의 규칙을 식이 그대로 갖는다.
- 등록된 이름: `flag` · `tag` · `chance` · `bodyShape` · `bodyType`(GameFramework), `weekday` · `season` · `weather` · `phase` · `dayOfSeason` · `week` · `date` · `affinity`(스케줄 문맥), `blackboard` · `hasBlackboard`(행동 트리), `setComplete` · `setPieces` · `equippedTag`(장착), `targetTag` · `anyTag` · `occupied`(외형).
- 장착 · 외형 조건의 종류 칸(`_kind` · `_setID` · `_tag`)은 남는다. 식은 함수 포인터라 무엇을 묻는지 답하지 못하는데, 조건 순환 검사와 "같은 질문을 뒤집은 규칙" 검사는 그 답이 필요하다.

합치는 곳: `ScheduleCondition`, `EquipCondition`, `AppearanceRuleCondition`, 대화 `_condition`, `BehaviorBlackboardCondition`, `IShopConditionEvaluator`.

합치지 않는 곳: `AnimationConditionOp`(매 프레임 캐릭터마다 도는 값 비교), `UserSettingConditionOperator`(게임 문맥이 없는 설정 화면).

**동작 `ActionRegistry`** (`Engine/Vocabulary/ActionRegistry.h`)

```cpp
REFLECT()
struct OpenShopParams { REFLECT_BODY(); PROPERTY() AssetRef<ShopCatalog> _shop; };
SW_GAME_ACTION( OpenShop, "shop.open", OpenShopParams, &openShop );
```

- 칸을 리플렉션 구조체로 받아, 고정 칸이 사라지고 에디터가 목록과 칸을 자동으로 보인다.
- 함수는 `Succeeded` · `Failed` · `Running` 을 돌려준다. 즉시 끝나는 동작도 같은 형식이다.
- 읽을 때 칸까지 해석해 함수 포인터와 값을 들고 있는다. 매번 글을 읽지 않는다.
- 글 형식은 `flag.set(hasKey, 1)` · `flag.set(flag=hasKey, value=1)` 이고, 행동 트리는 칸을 노드 속성으로 적는다(`<Action task="flag.set" flag="alarm"/>`). 그래서 칸 이름은 노드 속성(`id` · `name` · `task`)과 겹치지 않게 짓는다.
- 등록된 이름: `flag.set` · `flag.add` · `event`(GameFramework), `blackboard.set` · `blackboard.clear`(행동 트리), `rts.*`(RTS 키트). 대화 `_actionCommand`, 스케줄 칸의 `onStart` · `onEnd`, 퀘스트 `<Reward><Action call="…"/>` 가 같은 이름을 부른다. 시나리오: `game/empty/automation/vocabulary`.
- 코드로 짓는 트리(`BehaviorTree::addAction( …, BehaviorTaskFunction )`)는 이름 없이 함수를 받는 길로 남긴다(언리얼 C++ `UBTTask`). 데이터 트리만 이름이 필요하다.

합치는 곳: `BehaviorTaskRegistry`, 대화 `_actionCommand`, 스케줄 활동의 시작 · 끝 동작, 퀘스트 보상.

합치지 않는 곳: 어빌리티 이펙트(복제 · 예측 규칙이 있다), 기믹 회로 노드(스텝 상태 슬롯이 저장 대상이다. 회로의 출력이 동작을 부를 수는 있다), `EditorCommandRegistry` · `DevCommandRegistry`(게임플레이가 아니다).

**행동 리소스 변형**

| 대상 | 변형 | 엔진 |
|---|---|---|
| 행동 트리 | 노드 `id` 를 `InheritKey` 로 두어 `_base` 트리에서 가지만 교체, `<SubTree tree="…"/>` 는 읽을 때 펼침(순환 오류) | 언리얼 Run Behavior |
| 행동 트리 슬롯 | `<SubTree slot="combat"/>` 를 쓰는 쪽의 표가 채움 | 언리얼 Run Behavior Dynamic |
| 스케줄 | `<Archetype>` 은 남긴다. 아래 까닭 | — |
| 퀘스트 · 루트 · 스폰 · 상점 | `DataCatalogInheritance.json` 에 줄 추가(퀘스트 · 스폰은 있었고 루트 · 상점을 더함) | — |

행동 트리 변형은 구현했다. 노드 `id` 가 열쇠이고(`BehaviorTreeSchema` — 쿠커가 루트 이름으로 찾도록 Engine 에 둔다), 노드의 자식이 곧 같은 규칙의 목록이다(`DataInheritanceSchema::_bSelfNested`). 부모에 없는 가지는 안쪽에 열쇠를 적지 않아도 된다. `<SubTree tree>` 는 읽을 때 펼치고 순환은 오류다. `<SubTree slot>` 은 러너마다 `setSlotTree` 로 채운다.

**`<Archetype>` 을 항목 `_base` 로 옮기지 않은 까닭.** 루틴 합치기는 "NPC 자기 루틴 다음에 아키타입 사슬의 루틴, 같은 id 는 가까운 쪽이 통째로 이긴다" 이다. 항목 합치기로 옮기면 셋이 달라진다. (1) `_base` 는 같은 종류의 항목만 가리키는데 아키타입은 NPC 가 아니다 — NPC 로 만들면 그 NPC 가 세계에 선다. (2) 행사의 `archetypes="villager"` 는 사슬 소속을 묻는데, 펼친 글에는 `_base` 가 남지 않아 물을 곳이 없다. (3) 같은 id 의 루틴은 통째로 바뀌지 않고 속성이 합쳐져(자식이 `days` 를 지우려면 `_op="Replace"`) 데이터의 뜻이 바뀌고, 순서가 "부모 먼저" 가 되어 우선순위가 같은 칸의 순서가 뒤집힌다. 추상 항목(만들지 않는 항목)과 펼친 뒤의 소속 표가 생기면 옮긴다.


### 6. 종류별 변형

앞 층 위에 얹는다. 합치기 코드는 쓰지 않고 `PROPERTY( InheritKey )` 나 규칙 한 줄을 더하는 것이 기본이다.

| 축 | 항목 | 엔진 |
|---|---|---|
| 교체표 | 애니메이션 셋을 `_base` · 열쇠 목록으로 두고, 그래프는 슬롯 이름만 쓴다 | 유니티 Animator Override Controller |
| 교체표 | 애니메이션 그래프의 상태를 열쇠로 두고, 연결 레이어 슬롯을 둔다 | 언리얼 Linked Anim Layers |
| 교체표 | 스프라이트 라이브러리: 분류 · 라벨 → 프레임. 클립은 라벨만 쓴다 | 유니티 Sprite Library |
| 파라미터 | 머티리얼 파라미터 묶음(`*.matparams.xml`): 프로퍼티가 묶음의 이름을 가리키고 값이 바뀌면 다시 패킹한다. 슬롯을 새로 두지 않아 네 백엔드가 같은 길이다. 구현됨 | 언리얼 Material Parameter Collection(묶음마다 유니폼 버퍼 — 값 바꾸기가 O(1) 이지만 셰이더가 배치를 안다) |
| 파라미터 | 머티리얼 층: 층 파일 한 벌을 Layer 노드로 쌓고 켜고 끄기 · 값은 자식 `.material` 칸(`docs/agents/MaterialGraph.md` 의 층) | 언리얼 Material Layers · 유니티 HDRP Layered Lit |
| 파라미터 | VFX 방출기 `InheritKey` 와 사용자 파라미터(색 · 크기 곱, 재생 중 덮기), 오디오 라이브러리 · 믹서 `_base`, 이벤트 프리셋 `_parentEvent`. 구현됨 | 언리얼 Niagara 부모 이미터 · User Parameters · MetaSound Preset |
| 인스턴스 값 | 파일 없이 인스턴스마다 float4 둘(`GPUInstanceCustomData`, 인스턴스 원소 160 바이트). 내용이고 배치 키가 아니다. `MaterialInstance` 는 값 · 텍스처 · 키워드를 덮는 드로우 단위로 남는다. 구현됨 | 언리얼 Custom Primitive Data · 유니티 MaterialPropertyBlock |
| 파라미터 | 외형 구간 머티리얼: 부품 · 변형 · 규칙 · 피해 단계가 메시 구간 이름마다 머티리얼과 매개변수를 정한다(`AppearanceSectionDef`, `ResolvedMaterialValue::_section`). 병합해도 구간이 남는다. 런타임 아틀라스는 두지 않는다. 구현됨 | 언리얼 Material Slot 덮어쓰기 · 유니티 `Renderer.materials`(여기는 번호가 아니라 이름) |
| 조립 | 씬 인스턴스(`<entity scene="…">` + `<Part path>`): 씬을 다른 씬에 넣고 덮어쓴다. 읽을 때 평평한 엔티티로 펼치고 쿠킹본도 펼친 것을 담는다 | 언리얼 Level Instance · Godot 씬 상속 |
| 조립 | 씬 상속(루트 `_base`): 엔티티 id 가 열쇠인 차이만 적는다. 씬 템플릿 자리를 겸한다 | Godot 씬 상속 |
| 조립 | 데이터 레이어(`layers="Winter,Night"`): 켜진 이름 집합의 객체만 올린다(계절 · 이벤트). 단계 5 의 문맥 태그가 이 이름을 켠다 | 언리얼 Data Layers |
| UI | 스타일 시트 `_base`(규칙 표 한 줄), `<TypeVariation>`(읽을 때 규칙 복제), 에디터 테마 토큰 `_base`(JSON 이라 키 덮기). 구현됨 | Godot Theme Type Variation · 유니티 USS |
| 기타 | 리그 · 리타겟 · 물리 · 소켓 · 알림 표 · 입력 레이어에 `InheritKey` | — |

구현에서 정한 것:

- **애니메이션 셋:** `base` 를 공통 `_base` 로 옮겼다. `set`(줄 하나를 다른 셋에서 빌림)은 여러 파일을 섞는 믹스인이라 `_listRenderPassRef` 와 같은 까닭으로 남긴다. 셋을 바꾸면 상태 · 위상과 코드 레이어가 새 셋의 같은 이름 클립으로 잇는다.
- **그래프:** 상태는 이름(자식이 더한 상태의 id 가 부모와 겹치면 오류로 드러난다), 전이는 id, 레이어 · 마스크는 이름이 열쇠다. 연결 레이어는 `<layer slot>` 을 `SkeletalAnimatorComponent::_listLinkedLayer` 의 그래프로 채운다. 채운 그래프의 클립 이름과 파라미터는 애니메이터의 것을 함께 쓰고, 레이어 안의 레이어는 없다(한 단계).
- **노드 · 레이어 LOD:** 등급은 한 축이다(`AnimationLODState::_lodGrade` = `animationlod.xml` 의 주기 단계 + 예산이 주기를 두 배로 늘린 횟수). 그래프 `<layer>` · 블렌드 스페이스 `<node>` 와 리그 노드(IK · 룩앳 · 스프링 본)가 `max_lod` 칸으로 그 등급에서 꺼지고, `_nodeFadeSeconds` 동안 가중치를 옮겨 튀지 않는다(`AnimationLODFade`, 언리얼 LOD Threshold 는 끊는다). 칸이라 먼 NPC 변형은 `<layer name="Aim" max_lod="1"/>` 만 적는다. 본 LOD 와는 따로 돈다 — 리그 노드가 쓰는 본은 본 LOD(`SkeletonBoneLOD`)의 핵심 본(`SkeletalMeshComponent::requireBones`)이라 빠지지 않고, 레이어가 빠진 본을 써도 그 본은 그리는 메시가 쓰지 않는다.
- **2 차 물리 LOD:** 흔들림 본 · 망토 · 머리 가닥은 리그 `SpringChain` 이라 위 `max_lod` 로 끈다(다 꺼지면 상태를 버리고, 켜지면 애니메이션 포즈에서 섞여 들어온다). 물리 천(`ClothComponent`, Jolt 소프트 바디)은 아직 LOD 가 없다 — 확장 지점은 천이 조상 스켈레탈 메시의 등급을 읽고(`findBoneWorld` 가 이미 그 메시를 찾는다) 등급마다 반복 수를 낮추거나(`IPhysicsSoftBodyScene` 에 반복 수 · 잠재우기 함수를 더한다) 끄는 것이다. 다시 켤 때 고정점이 옮겨 가 튀므로 스킨 구속으로 한 프레임 붙인 뒤 풀어야 한다.
- **스프라이트 라이브러리:** 아틀라스는 파일에 하나다. 라벨마다 텍스처가 다르면 프레임을 넘길 때 머티리얼 인스턴스가 바뀌어 배치가 깨진다.
- **소켓 · 알림 표:** 원소 종류가 섞이고 열쇠 칸이 달라 `DataCatalogInheritance.json` 규칙 한 줄이다. 리그 · 리타겟은 `SW_DATA_INHERITANCE_SCHEMA`, 물리 에셋은 `PROPERTY( InheritKey )` 다.
- **뺀 것:** 물리 셰이프(이름이 없다) · 충돌 쌍(열쇠가 뼈 둘이다), 리그 · 리타겟 안의 뼈 목록(순서가 뜻이다)은 통째로 바뀐다. 입력 레이어는 이번에 하지 않았다.

### 7. 무기와 발사체 조합

지금은 탄속 · 낙차 · 피해가 `WeaponDef` 안에 있어 "소총 + 폭발탄" 하나에 무기 정의 하나가 든다. 발사체 구조체도 키트마다 따로다(`Ballistics::Projectile`, `ProjectileComponent`, `MechProjectile`, `KartProjectile`, `ActionProjectile`, `ArenaProjectileComponent`). 정의를 나눠 무기 N 종과 발사체 M 종이 파일 N + M 개로 조합되게 한다.

| 정의 | 가지는 것 |
|---|---|
| `WeaponDef`(쏘는 법) | 연사 · 탄창 · 재장전 · 퍼짐 · 반동 · 알 수 · 총구 효과, 받는 탄의 태그 조건, 기본 탄, 탄 값에 곱하는 배율 |
| `ProjectileDef`(날아가는 것) | 운동(광선 · 탄도 · 유도), 탄속 · 중력 · 저항 · 수명, 관통 · 튕김 · 폭발, 궤적 · 충돌 효과, 태그, 맞을 때의 동작 목록(`ActionRegistry` + `ConditionExpr`) |
| 부착물 · 버프 | 수정자 목록(칸, 더하기 · 곱하기). 합치는 순서는 탄 → 무기 → 수정자로 한 벌이다 |

- 호환은 조합 표가 아니라 태그 조건이다(`accepts="Ammo.Rifle"`). 표로 적으면 M × N 이 그대로 돌아온다.
- 조합 결과(`ResolvedShot`)는 장전 · 부착물이 바뀔 때 한 번 계산해 든다. 쏠 때마다 계산하지 않는다.
- 충돌 효과는 탄과 표면의 조합이라 발사체가 직접 들지 않고 표면 표에서 찾는다(언리얼 Physical Material 과 같은 자리).
- 런타임은 발사체 시뮬레이션 하나로 모은다. 탄을 게임 오브젝트로 만들지 않아 탄 수가 많아도 비용이 오르지 않고, 롤백 · 복제가 같은 결과를 내도록 결정적으로 돈다. 키트별 구조체는 옮기기 전에 복제 · 롤백 규칙을 읽어, 옮길 수 없으면 까닭을 적는다.
- 세이브의 탄 id 는 지금 `_ammoID` 를 그대로 잇는다.

언리얼 Lyra 의 무기 조각 · 유니티 ScriptableObject 탄 정의와 같은 방향이다(기억에 의존한 비교).

**구현** (`GameFramework/Runtime/Character/Combat/Projectile/`)

| 자리 | 형식 |
|---|---|
| 탄 정의 | `ProjectileDef` · `ProjectileCatalog`(`data/projectiles.xml`, 종류 `projectiles`). 항목 `_base` · `<OnHit when call>`(조건 · 동작 어휘, 맞은 쪽 문맥) |
| 맞음 표 | 같은 파일의 `<Impact id surface when color size>`. 위에서 처음 맞는 줄이 이긴다(`ProjectileCatalog::findImpact`) |
| 무기 | `WeaponDef::_projectileID`(`projectile`, 필수) · `_accepts`(탄 태그 식, 그냥 이름은 `tag`) · `<Modifier field op value>` |
| 수정자 | `ShotModifierDef`(`ShotField` 열두 칸, `Add` · `Multiply`). 음수가 되는 칸은 0 에서 멈춘다 |
| 조합 | `ResolvedShot::resolve( 탄, 무기 수정자, 부착물 )`. `WeaponState::equip` · `loadProjectile` 이 받음을 보고 한 번 계산한다. 감쇠 · 머리 배율도 여기다(`DamageMath` 에는 방어만 남았다) |
| 시뮬레이션 | `ProjectileSimulation` · `ProjectileState` · `IProjectileWorld`. 수명 → 유도 → 저항 → 중력 → 평면 제약 → 사거리로 자른 선분 → 맞음(관통 · 튕김) → 사거리 끝 |

- **키트는 데이터와 반응만 갖는다.** 기체 대전 · 카트 · 액션 플랫포머 · 액션 룸이 `IProjectileWorld` 로 맞을 대상(적 조종사 구 · 차의 수평 거리 · 벽 칸 · 없음)을 답하고, 사건(`ProjectileEvent`)을 받아 다운치 · 아이템 효과 · 폭발 범위를 준다. 반응 수치는 탄에 베끼지 않고 가리킨다(기체 대전은 `_payload` = 기체 번호 × 256 + 슬롯, 카트는 `_defID` = 아이템 id).
- **2D 는 평면 제약 3D 다.** 언리얼 ProjectileMovement 의 평면 제약과 같은 길이다. 유니티 · Godot 는 2D 물리를 따로 두지만, 그러면 관통 · 튕김 · 유도 · 반사를 차원마다 두 벌 쓴다. 액션 플랫포머 · 액션 룸은 (x, y, 0) 에 법선 Z, 카트는 법선 Y 로 돌고 높이는 키트가 트랙을 따라 맞춘다.
- **결정성:** `ProjectileDef.cpp` · `ProjectileSimulation.cpp` 는 `sw_markDeterministicSources` 에 있고 삼각함수는 `DeterministicMathUtil` 이다. 중력은 쓰는 쪽이 값으로 넘겨 물리 설정을 읽지 않는다. 상태 바이트는 한 벌(`ProjectileSimulation::writeState`)이고 쓰는 쪽은 자기 번호의 범위만 본다. 걸음 안에서 세계를 바꾸지 않고(사건은 걸음 뒤에 반응), 그래서 같은 걸음에 둘이 한 대상을 맞히면 둘 다 소비된다(예전 기체 대전은 앞 탄이 격추하면 뒤 탄이 지나갔다).
- **옮기지 않은 것:** `ArenaProjectileComponent`(AbilityArena 게임)는 탄이 어빌리티 이펙트 스펙을 실어 나르는 오브젝트다. 어빌리티 이펙트는 복제 · 예측 규칙 때문에 어휘에도 합치지 않은 자리라(5 절) 그 키트의 복제 규칙과 함께 옮긴다. 몬스터 사격(`MonsterShotDef`) · 기체 특수기 · 카트 아이템은 탄 수치를 자기 표에 두고 공통 시뮬레이션만 쓴다 — 탄 표(`ProjectileCatalog`)를 가리키게 바꾸면 그 키트의 게임이 카탈로그를 서비스로 걸어야 하는데 아직 그 게임이 없다.
- 확인: 단위 테스트 `ProjectileTest`(조합 · 받음 · 순서 · 맞음 표 · 맞음 동작 · 관통 · 튕김 · 유도 · 사거리 · 수명 · 상태 왕복), 시나리오 `game.shooter3d.projectilecombo`(같은 소총에 소이탄을 넣으면 피해 감시값과 탄도선 그림이 바뀐다).

## 순서

| 단계 | 내용 | 끝났다고 보는 조건 |
|---|---|---|
| 0 | 작업자 병합, Windows 검증, 전체 재임포트 | 통합 빌드와 테스트 통과 |
| 1 | 종류 정의 통합, `ResourceComposer` 로 읽는 길 통합(동작 변화 없음) | 기존 시나리오 그대로 통과, 읽는 길 한 곳 |
| 2 | 공유 캐시 통합, `GameDataCache` 삭제, `acquire` 도입 | 같은 경로가 같은 객체라는 테스트, 다시 읽기가 쓰는 쪽에 닿는 시나리오 |
| 3 | `AssetRef<T>` 와 생성 목록, `string` 칸 전환 | 잘못된 종류 연결이 로드 오류, 손 표가 줄어듦 |
| 4 | 카탈로그 공유 전환(코드 끝남, 컴파일 전) | 공개 `loadFromAsset` 제거가 컴파일됨 |
| 5 | 조건 · 층, 경로 교체, 합성 해시 대조 | 조건 · 층마다 값과 그림이 바뀌는 시나리오 |
| 6 | `ConditionExpr` · `ActionRegistry`, 행동 트리 하위 트리(구현 — 시나리오 `game/empty/automation/vocabulary`. `<Archetype>` 은 위 까닭으로 남김) | 같은 이름의 조건 · 동작을 행동 트리 · 스케줄 · 대화가 함께 쓰는 시나리오 |
| 7 | 종류별 변형(위 표) | 표 한 줄마다 효과를 확인하는 시나리오 |

1 · 2 는 한 에이전트가 차례로 한다. 공통 파일이 겹친다. 3 · 4 와 6 · 7 은 범위가 나뉘므로 병렬로 할 수 있다. 작업자는 코드 → 린트 → 커밋만 하고 통합이 검증한다.

칸을 더하면 실행이 그 칸을 읽고, 시나리오가 그 효과를 확인한다. 읽히지 않는 칸은 넣지 않는다.

## 새 형식 체크리스트

1. `SW_ASSET_TYPE` 한 줄(로더 · 다시 읽기 방식 포함).
2. 목록 원소의 이름 칸에 `PROPERTY( InheritKey )`, 또는 카탈로그 규칙 한 줄.
3. 다른 리소스를 가리키는 칸은 `AssetRef<T>`.
4. 쓰는 쪽은 `acquire` · `request` 로 받고, 실행 상태는 자기 쪽에 둔다.
5. 조건과 동작은 등록된 어휘를 쓴다. 새 이름이 필요하면 어휘에 더한다.

## 위험

| 위험 | 대응 |
|---|---|
| 공유 전환에서 사본을 고쳐 쓰던 코드가 대량으로 드러난다 | `const` 컴파일 오류로 빠짐없이 찾는다. 분량이 크므로 단계 4 를 범위별로 나눈다 |
| 조건 어휘 비용 | 읽을 때 컴파일하고, 매 틱 조건은 측정으로 확인한다 |
| 로드 축 `_when` 이 많아지면 품질 변경 때 다시 읽기가 몰린다 | 축별로 쓰는 리소스를 기록해 그것만 다시 읽는다 |
| 코드 기본값 변경이 적지 않은 칸의 값을 바꾼다 | 이미 있는 규칙(`DataInheritance.md`)을 따른다 |
| 합성 코드가 배포 구성에 남는다 | DLC · 모드 층이 없는 게임은 쿠킹본에 합성할 것이 없어 글 검색 한 번으로 지나간다 |

## 정하지 않은 것

구현 전에 코드를 읽어 정한다. 사용자 결정이 필요한 항목은 없다.

- 메시 런타임 조립(언리얼 Mutable)은 `BodyShape` · 메시 병합 코드를 조사한 뒤 넣을지 정한다. 이 설계에 포함하지 않았다.
- `XMLCatalog` 계열 중 파일에서 읽는 쪽이 저장소에 없는 키트 카탈로그(약 20 종 — SRPG · 정복 · 메트로배니아 · 공포 · 몬스터 · 파티 · 카트 등)는 지우지 않았다. 규칙 클래스가 `const T*` 를 받는 라이브러리이고 아직 그것을 쓰는 게임이 없을 뿐이라, 캐시 등록(파일 규칙 없는 `makeCacheOnly`)만 붙였다. 그 키트를 쓰는 게임이 생기면 데이터 파일 규칙(`makeDataFile`)을 정한다.
