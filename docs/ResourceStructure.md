# 리소스 공통 구조

> 질문: 리소스는 어떻게 정의 · 합성 · 공유 · 참조되고, 변형은 어디에 적는가?
> 독자: 새 리소스 형식을 더하거나 합성 · 캐시 · 참조를 고치는 세션(에이전트와 사람).
> 결론: 종류 정의(`AssetTypeDef`) · 합성 읽기(`ResourceComposer`) · 공유 캐시(`SharedAssetCache<T>`) · 타입 있는 참조(`AssetRef<T>`)가 한 벌이고, 변형은 부모 → 조건 → 덮어쓰기 층의 한 길로 처리한다. 행동 데이터는 실행기를 합치지 않고 조건 · 동작 어휘만 공유한다.

파일에 적는 법(`_base` · `_when` · 층 · 경로 교체의 규칙 표)은 [DataInheritance](agents/DataInheritance.md) 가 정본이다. 이 문서는 구조와 까닭을 적는다. 엔진 비교는 기억에 의존한 설명이라 고치기 전에 그 기능의 문서를 다시 확인한다.

```
리소스 파일
  └─ 합성 읽기: 부모(_base) → 조건(_when) → 덮어쓰기 층 → 펼친 글
       └─ 종류 정의(AssetTypeDef)의 로더: 글 → const T
            └─ 공유 캐시: 경로 하나에 객체 하나, 다시 읽기, 로드 상태
                 └─ AssetRef<T> 가 꺼낸다 → 실행 상태는 쓰는 쪽이 따로 가진다
```

새 형식은 종류 정의 한 줄로 합성 · 공유 · 다시 읽기 · 쿠킹 · 참조 색인 · 에디터 선택기를 얻는다. (예전에는 종류 표 둘 — `SW_ASSET_TYPE` · 데이터 종류 표 —, 형식마다 다른 합성 입구, 캐시 둘 — `SharedAssetCache` · `GameDataCache` —, 손 이름 표의 경로 칸이 따로였다.)

### 1. 종류 정의

등록 줄은 `SW_ASSET_TYPE` 하나다: `SW_ASSET_TYPE( id, 정의.withPathRule( … ).withLoader( AssetTypeLoader::of<T>( 태그, 방식 ) ) )`.

- 정의(`AssetTypeDef`)가 이름 · 접미사 · 판 · 쿠킹 접미사 · 경로 규칙(`/data/items.xml` 처럼 접미사로 가를 수 없는 파일) · 로더 · 다시 읽기 방식 · 메모리 태그를 든다. 헤더의 `kAssetType` 은 쿠킹이 쓰는 사실만, 로더는 등록 표의 사본에만 있다.
- 검증 전용 로더는 두지 않는다. 스키마 테스트도 진짜 로더를 부른다.
- 공유 캐시가 있는 종류는 `SW_SHARED_ASSET_CACHE( id, T, 정의 )`(Engine) · `SW_MODULE_SHARED_ASSET_CACHE( id, T, 내보내기, 정의 )`(모듈) 한 줄이 종류 등록 · 캐시 인스턴스화 · 레지스트리 등록을 함께 한다. 캐시 이름도 정의의 이름 하나다(`AssetReloadRoute::sharedCache( 정의 )`).
- 제약: 팩 쿠킹은 게임 모듈을 올리기 전에 돈다(`HeadlessSubsystem::runCookTask`). 쿠킹이 알아야 하는 합성 규칙은 데이터(`Content/engine/settings/datacataloginheritance.xml`)에 둔다. 배포 런타임도 이 표를 읽는다(덮어쓰기 층).

### 2. 합성 읽기 `ResourceComposer`

**한 길이다.** 리소스 글은 모두 `ResourceComposer::compose( text, id, context )` 를 지난다: 부모(파일 · 항목 `_base`) → 조건(`_when`) → 덮어쓰기 층. 읽는 쪽은 단계를 모르고 펼친 글만 받는다. 부모와 층은 `DataInheritance` 의 합치기 규칙(열쇠 · `_op` · `_before` · `_after`)을 쓰고, 조건은 쓰지 않는다(아래).

- **규칙 출처의 순서:** 문서 표 · 카탈로그 표(`datacataloginheritance.xml`) → 합치기 함수(`SW_DATA_INHERITANCE_MERGE`) → 손 형식 → 리플렉션 형식. 쿠킹은 Engine 코드만 보므로 게임 · 키트 형식은 표에만 있어도 되고, 표와 코드가 맞는지는 `ResourceComposerTest` 가 본다.
- **프리팹:** `PrefabOverrides` 는 열쇠 모양이 달라 따로 두고, 문서 머리의 조건 · 층만 같은 길을 지난다.
- **스켈레톤:** 공통 사슬(`DataInheritance::collectChain`) + 합치기 함수가 평평한 `<Skeleton>` 글을 낸다(뼈 비율은 곱하고 클립 규칙은 쌓는다). "부모 본 번호를 바꾸지 않는다" 는 합친 뒤 검증이다. 읽기와 쿠커가 같은 함수를 지나 쿠킹본에 `_base` 가 남지 않는다.
- **리소스 입구:** id 로 읽는 입구와 글 입구 금지 규칙은 [ExistingFacilities](agents/ExistingFacilities.md) "리소스 읽기 입구와 에셋 참조" 가 정본이다.

#### 조건별 값

Godot 기능 태그처럼 태그 집합 하나에 묻되, 언리얼 Device Profile 처럼 쿠킹 축과 로드 축을 나눠 쿠킹 축은 쿠킹본에서 지운다. 글 형식은 고칠 원소 안에 `<_when _if="…" 칸="값"/>` 또는 `_op="Remove"` 를 적는다(규칙 표: [DataInheritance](agents/DataInheritance.md) 조건 절).

- **조건은 자기를 품은 원소 하나만 고친다.** 그래야 합치기 규칙 없이 쿠커 · 개발 실행 · 배포 런타임이 같은 함수(`ResourceComposer::resolveConditions`)로 같은 답을 낸다. 잃는 것은 "참이면 목록에 원소를 더한다" 표현뿐이다(원소를 적고 반대 조건으로 뺀다).
- 식은 [조건 어휘](#5-행동-어휘)의 `ConditionExpr` 이고 그냥 이름은 태그 조건(`resourceTag`)이다. 태그는 코어 범위 리소스 루트(`ContentRootSubsystem::getResourceTags`)가 든 `ResourceTags` 이고, 합성 기록(`ResourceComposition`)도 같은 주인이 든다.
- 바이너리(텍스처 · 오디오 · 메시)는 경로를 바꾼다(`ResourceRemap`, 게임 팩 설정 `<Engine _resourceRemap>` 의 `<Remap id when to>`). 같은 id 는 위에서 처음 참인 줄, 사슬은 오류. `AssetIO` 입구가 찾기 전에 한 번 바꾸고, 쿠킹 작업 안에서는 바꾸지 않는다(쿠킹본이 기계마다 달라지지 않게). 두 파일 모두 팩에 든다.

| 축 | 정해지는 때 | 값을 넣는 곳 | 쿠킹본 |
|---|---|---|---|
| `platform` · `target` | 쿠킹 | 엔진 기동(쿠커도 같은 자리) | 펼쳐서 없어진다(로드 축 조건 뒤의 것은 차례를 지키려 남고 런타임이 같은 값으로 푼다) |
| `quality` | 로드 | 사용자 설정 핸들러 `resource.tag` | `_when` 이 남고, 값이 바뀌면 그 태그를 읽은 리소스를 다시 읽는다 |
| `locale` | 로드 | 현지화의 지금 언어(`en-US` → `locale.en_US`) | 위와 같다 |
| `region` · `feature` · `flag` | 로드 | 게임 코드(`ResourceTags::setAxisValue` · `setTagActive`) | 위와 같다 |
| `layer` | 로드 | 켜진 씬 데이터 레이어(`SceneDataLayerSubsystem`) | 위와 같다 |

- 쿠킹본을 런타임이 푼 결과가 소스를 바로 푼 결과와 같은지는 `ResourceConditionTest.CookedTextResolvesLikeTheSource` 가 본다.
- **다시 읽기:** 리소스 id 로 읽은 합성은 기댄 로드 축 태그를 `ResourceComposition` 에 남기고, 태그가 바뀌면 `AssetManager::reloadComposedDependents` → `AssetCacheBase::reloadComposed` 가 그 id 를 쥔 캐시를 다시 읽는다. 배포 구성에도 있다(품질 · 언어는 플레이어가 바꾼다). 시나리오 `game.empty.resourcecondition`.
- **한계:** 캐시 열쇠는 경로뿐이라 한 프로세스의 문맥은 하나다. 서버가 플레이어마다 다른 지역 값을 고르려면 데이터의 키로 나눈다. 에디터에서 씬이 여럿이면 `layer` 축은 마지막에 넣은 씬의 집합이다. 예외 하나: 몇 축만 바꿔 끼우는 미리보기 합성(`ResourceContext::withTagOverride` — UI 미리보기의 `screen` · `orientation`)은 그 캐시가 경로 + 태그 열쇠로 따로 들고(`UITextAssetCache`) 다시 읽기 · 접속 대조 기록에 적지 않는다.
- **네트워크:** 게임플레이 값이 클라이언트마다 다르면 판정이 어긋난다. 클라이언트는 붙자마자, 그리고 새로 읽거나 바뀔 때마다 로드 축 · 층에 기댄 합성 한 줄(리소스 id + 결과 해시, `GameNetSessionMessage::kResourceDigest`)을 보내고, 서버(`ResourceDigestComparer`)는 같은 리소스를 다르게 합성했고 유예(`gv_netResourceDigestGraceSeconds`)가 지나도 다르면 끊는다(`Net.ResourceDigestMismatch`). 서버가 조건 · 층 없이 읽은 표도 해시를 들어, 클라이언트만 모드 층을 얹으면 거절된다. 서버가 읽지 않는 리소스(시각 효과)는 대조하지 않는다. `ResourceTagScope::PerClient` 축(품질 · 언어)은 달라도 되므로 판정 값은 이 축에 기대지 않는다. 일관성 검사이지 부정행위 방지가 아니다. 시뮬레이션 데이터에 쿠킹 축과 로드 축 조건을 섞지 않는다(클라이언트와 서버의 글이 달라 해시가 어긋난다). 시나리오 `editor.netresourcedigest`.
- **편집기 저장:** 사본은 지금 태그로 푼 값을 들므로 저장은 `ResourceComposer::saveText` 가 있던 파일의 조건을 되돌린다(규칙은 `restoreConditions` 머리말). 참인 조건이 정한 칸의 편집은 그 조건의 값이 된다. 층을 얹어 읽은 리소스는 원본 자리에 쓰지 않는다(`ResourceLayer.SaveWouldBake`). 시나리오 `editor.resourcesave`.

#### 덮어쓰기 층

언리얼 Composite Data Table · 팩 우선순위 자리다. 층은 순서 있는 목록(`ResourceLayers`, 아래 층이 먼저)이고, 리소스 `X` 의 층 파일 `<뿌리>/X` 는 원본을 암묵적 부모로 가진 차이다(파일 통째 교체가 아니라 다른 칸만 적는다).

- 목록은 게임 팩 설정 `<Engine _listResourceLayer>`(`game/<팩>/layers/<이름>`)이고 DLC · 모드 팩을 올리는 쪽이 늘린다(`ResourceComposition::setLayers` — 바뀐 층에 파일이 있는 리소스를 다시 읽는다).
- 층은 쿠킹 때 펼치지 않고 읽을 때 얹는다 — 공용 도메인 팩(`common` · `engine`)은 게임마다 같아야 해 게임 층을 펼칠 자리가 없다(`PackCooker::cookDomainPack`). 층이 없는 게임은 글 검색 한 번으로 지나간다.
- 층 파일의 `_when` 은 층의 차이에 얹는다(거짓이면 그 차이를 얹지 않는다).
- 층은 리소스 id 로 읽을 때만 얹는다. 글 입구(`loadFromXMLText` — 미리 보기 · 테스트)는 얹지 않는다.
- 항목 id 를 바꾸지 않으므로 세이브는 그대로다.
- 신뢰 경계: 층 파일은 외부 입력이다. 합성 뒤에 그 형식의 검증이 그대로 돌고, 온라인 게임은 위 해시 대조로 거른다.

### 3. 공유 캐시

`SharedAssetCache<T>` 하나다. 옛 객체를 살려 두는 동작은 `SharedAssetReloadMode::Detach` 와 `shared_ptr` 가 맡는다.

- **모듈 캐시:** 키트 · 게임 종류는 그 모듈이 인스턴스화해 모듈 수명으로 등록한다(`Engine/Module/ModuleSharedAssetCache.h`). `XMLCatalog` 형식은 모두 캐시를 가진다.
- **창구:** `acquire( 경로 )` · `request( 경로 )` · `getReloadCount()` 를 바로 부른다. `XMLCatalog` 에 감싸개를 두지 않는다(내보내는 카탈로그가 베이스 멤버를 모두 인스턴스화해 캐시 없는 카탈로그의 링크가 깨진다).
- **사본을 못 만들게 한다:** `loadFromAsset` 은 비공개이고 종류의 로더(`AssetTypeLoader`)만 friend 다. 쓰는 쪽은 `shared_ptr<const T>` 를 쥐고, 게임 범위 서비스는 `GameInstance::loadRequiredCatalog` 로 받는다. 테스트 · 미리 보기는 `loadFromXMLText`. 쿠커가 읽는 형식만 공개 `loadCopyFromAsset` 을 둔다 — 쿠킹 작업(`CookJob`)은 범위 안의 읽기를 입력으로 기록하므로 캐시의 한 벌을 쓰면 입력이 빠진다.
- **다시 읽기:** 카탈로그는 기본이 제자리(`InPlace`) — 쥔 쪽은 항목 포인터를 프레임을 넘어 들지 않고 id 로 찾는다. 진행 중인 세션이 옛 정의를 읽어야 하거나 옮길 수 없는 표만 `Detach` 다. 시나리오 `editor/catalogshared`.
- **사본이 맞는 것:** 프로세스에 한 벌이고 서비스가 기동 때 자기 상태로 옮기는 엔진 설정(`PhysicsSettings` · `UIScaleSettings` 등, 언리얼 `UDeveloperSettings` 자리), 쓰는 쪽이 고치는 입력(`InputMap` 재바인딩 · 높이장 · `NavMeshAsset` · `TileMapXMLData`), 도구 입력(정점 애니메이션 쿠커 · 머티리얼 그래프 컴파일러 · 에디터 편집 사본).
- **정의와 실행 상태:** 공유 객체는 `const` 다. 실행 상태가 섞인 카탈로그는 나눈다(`BehaviorTree` · `BehaviorTreeRunner`, `AbilityCatalog` · `AbilityClassRegistry`).
- `MeshCache` · `TextureCache` · `MaterialCache` · `PrefabCache` 는 GPU 수명 · 쓰는 쪽의 수정이 있어 합치지 않는다(`SharedAssetCache.h` 머리말). `AssetHandle` · 로드 상태 · 종류 정의는 공유한다.

### 4. 타입 있는 참조 `AssetRef<T>`

언리얼 `TSoftObjectPtr<T>` · 유니티 `AssetReferenceT<T>` 자리다. 경로와 대상 종류를 품은 값이고, 의존성 수집 · 미리 읽기(`PreloadSet`) · 쿠킹 입력 · 참조 색인 · 에디터 선택기가 모두 이 칸을 본다. 칸 규칙(종류 가르기 · 경로 칸 출처 · 쿠킹 도달성)의 정본은 [ExistingFacilities](agents/ExistingFacilities.md) "리소스 읽기 입구와 에셋 참조" 다.

- **값 모양:** `AssetRefBase`(경로 + `const AssetTypeDef*`)와 `AssetRef<T>`. 대상 종류를 값이 품으므로 `vector<AssetRef<T>>` · 중첩 구조체 · 덮어쓰기 패치가 같은 검사를 지난다.
- 쿠킹 도달성 계산과 칸 규칙(`AssetReferenceFields`)은 엔진(`Engine/Asset/Reference`)에 있다. 큰 카탈로그는 항목 단위로 고르고(`_listEntryCatalog` · `_listEntryIDField`), 스테이징 산출물은 원본이 쓰일 때만 담는다(`PackStagedSelection`).

### 5. 행동 어휘

**실행기는 합치지 않는다.** 행동 트리 · 스케줄 · 대화 · 어빌리티 · 기믹 회로는 모델이 다르다(언리얼도 BT · StateTree · GAS 를 나누고 Gameplay Tag 로 어휘를 공유한다). 공유하는 것은 조건과 동작의 이름이다.

**조건 `ConditionExpr`** (`Engine/Behavior/ConditionExpr.h`)

- 문법: `flag && !other`, `tag(Quest.Done)`, `weather(rain, storm)`, `affinity >= 4`. 이름은 `SW_CONDITION( 이름, 칸 스키마, 함수 )` 로 등록한다.
- 읽을 때 후위 배열 + 함수 포인터로 컴파일한다. 모르는 이름은 로드 오류다. 확률 조건은 문맥의 씨앗을 쓴다(결정성).
- 세계는 형식 있는 칸의 문맥(`GameplayContext` — `GameFlagsSlot` · `BlackboardSlot` · `ScheduleConditionSlot`)으로 넘긴다. 없는 칸은 거짓, 없는 값은 NaN(어떤 비교에도 거짓, `hasBlackboard` 로 묻는다).
- 장착 · 외형 조건의 종류 칸(`_kind` · `_setID` · `_tag`)은 남는다 — 조건 순환 · 뒤집힌 규칙 검사가 "무엇을 묻는지" 를 알아야 한다.
- 쓰는 곳: `ScheduleCondition` · `EquipmentCondition` · `AppearanceRuleCondition` · 대화 `_condition` · 행동 트리 블랙보드 조건 · 상점 조건 · 리소스 `_when`. 쓰지 않는 곳: `AnimationConditionOp`(매 프레임 값 비교), `UserSettingConditionOperator`(게임 문맥 없음).

**동작 `ActionRegistry`** (`Engine/Behavior/ActionRegistry.h`)

```cpp
REFLECT()
struct OpenShopParams { REFLECT_BODY(); PROPERTY() AssetRef<ShopCatalog> _shop; };
SW_GAME_ACTION( OpenShop, "shop.open", OpenShopParams, &openShop );
```

- 칸은 리플렉션 구조체라 에디터가 목록과 칸을 자동으로 보인다. 함수는 `Succeeded` · `Failed` · `Running` 을 돌려준다. 읽을 때 칸까지 해석해 든다.
- 글 형식은 `flag.set(hasKey, 1)` · `flag.set(flag=hasKey, value=1)`, 행동 트리는 노드 속성(`<Action task="flag.set" flag="alarm"/>`) — 칸 이름은 노드 속성(`id` · `name` · `task`)과 겹치지 않게 짓는다.
- 쓰는 곳: 행동 트리 `<Action task>` · 대화 `_actionCommand` · 스케줄 `onStart` · `onEnd` · 퀘스트 `<Reward><Action call>`. 시나리오 `game/empty/automation/vocabulary`. 쓰지 않는 곳: 어빌리티 이펙트(복제 · 예측), 기믹 회로 노드(스텝 상태가 저장 대상), 에디터 · 개발 명령.
- 코드로 짓는 트리(`BehaviorTree::addAction( …, BehaviorTaskFunction )`)는 이름 없이 함수를 받는다(언리얼 C++ `UBTTask`).

**행동 리소스 변형**

| 대상 | 변형 | 엔진 |
|---|---|---|
| 행동 트리 | 노드 `id` 가 열쇠(`BehaviorTreeSchema`, `_bSelfNested`)라 `_base` 트리에서 가지만 교체. `<SubTree tree>` 는 읽을 때 펼침(순환 오류) | 언리얼 Run Behavior |
| 행동 트리 슬롯 | `<SubTree slot>` 을 러너가 `setSlotTree` 로 채움 | 언리얼 Run Behavior Dynamic |
| 퀘스트 · 루트 · 스폰 · 상점 | `datacataloginheritance.xml` 의 줄 | — |
| 스케줄 | `<Archetype>` 을 남긴다(아래) | — |

**`<Archetype>` 을 항목 `_base` 로 옮기지 않은 까닭:** (1) `_base` 는 같은 종류만 가리키는데 아키타입은 NPC 가 아니다. (2) 행사의 `archetypes="villager"` 는 사슬 소속을 묻는데 펼친 글에는 `_base` 가 남지 않는다. (3) 같은 id 루틴이 통째 교체가 아니라 속성 합치기가 되고 순서가 뒤집힌다. 추상 항목과 펼친 뒤의 소속 표가 생기면 옮긴다.

### 6. 종류별 변형

앞 층 위에 얹는다. 합치기 코드 대신 `PROPERTY( InheritKey )` 나 규칙 한 줄이 기본이다.

| 축 | 항목 | 엔진 |
|---|---|---|
| 교체표 | 애니메이션 셋 `_base` · 열쇠 목록, 그래프는 슬롯 이름만 | 유니티 Animator Override Controller |
| 교체표 | 그래프 상태가 열쇠, 연결 레이어 `<layer slot>` → `SkeletalAnimatorComponent::_listLinkedLayer`(한 단계) | 언리얼 Linked Anim Layers |
| 교체표 | 스프라이트 라이브러리: 분류 · 라벨 → 프레임(아틀라스는 파일에 하나 — 배치가 깨지지 않게) | 유니티 Sprite Library |
| 파라미터 | 머티리얼 파라미터 묶음(`*.matparams.xml`), 머티리얼 층([MaterialGraph](agents/MaterialGraph.md)) | 언리얼 MPC · Material Layers |
| 파라미터 | VFX 방출기 `InheritKey` · 사용자 파라미터, 오디오 라이브러리 · 믹서 `_base`, 이벤트 `_parentEvent` | Niagara 부모 이미터 · MetaSound Preset |
| 인스턴스 값 | `InstanceCustomData`(float4 둘, 배치 키 아님), `MaterialInstance` 는 드로우 단위 덮기 | 언리얼 Custom Primitive Data |
| 파라미터 | 외형 구간 머티리얼(`AppearanceSectionDef`, 구간 이름마다) | 언리얼 Material Slot 덮어쓰기 |
| 조립 | 씬 인스턴스(`<entity scene>` + `<Part path>`, 읽을 때 펼침) · 씬 상속(루트 `_base`) · 데이터 레이어(`layers="Winter,Night"`, 2 절 `layer` 축) | 언리얼 Level Instance · Data Layers, Godot 씬 상속 |
| UI | 스타일 시트 `_base`, `<TypeVariation>`, 에디터 테마 토큰 `_base` | Godot Theme Type Variation |
| 기타 | 리그 · 리타겟 · 물리 · 소켓 · 알림 표 · 입력 레이어에 `InheritKey` 또는 표 한 줄 | — |

- **애니메이션 LOD:** 등급 한 축(`AnimationLODState::_lodGrade`)으로 그래프 `<layer>` · 블렌드 `<node>` · 리그 노드(IK · 스프링 본)가 `max_lod` 칸에서 꺼지고 `_nodeFadeSeconds` 동안 가중치를 옮긴다(`AnimationLODFade`). 본 LOD(`SkeletonBoneLOD`)와 따로 돈다.
- **천 LOD:** `ClothComponent` 는 조상 스켈레탈 메시의 등급으로 반복 수를 낮추고(`_listLODIterationCount`) `_maxLOD` 를 넘으면 시뮬레이션을 내린다. 다시 켜면 지금 고정 자세에서 새로 만든다(옛 정점에서 이으면 튄다). 확인 `ClothLODTest` · `game.empty.clothlod`.
- **뺀 것:** 물리 셰이프 · 충돌 쌍, 리그 · 리타겟 안의 뼈 목록(순서가 뜻)은 통째로 바뀐다. 입력 맵의 `<override>` · `<remove>` 는 실행 중 쌓기다([InputMapBundle](InputMapBundle.md)).
- **메시 런타임 조립(언리얼 Mutable 자리):** 부품 피팅 · 모프 굽기(`AppearanceMeshPart::assemble`) → 구간 있는 메시 하나로 병합(`mergeParts`) → 결과 캐시(`AppearanceBakedMeshCache`) · 진행 중 짓기 나눔(`CharacterAppearanceInFlightTable`), 짓기는 워커. 캐릭터마다 텍스처 층을 굽지 않는다 — 염색 · 마스크 · 표면 상태는 머티리얼 매개변수라 텍스처 메모리가 들지 않고 군중이 머티리얼을 나눈다. 고정 차림 아틀라스는 에디터가 쿠킹한다(`OutfitAtlasSpec`).

### 7. 무기와 발사체 조합

무기 N 종과 발사체 M 종이 파일 N + M 개로 조합된다(`GameFramework/Runtime/Character/Combat/Projectile/`). 언리얼 Lyra 무기 조각 · 유니티 ScriptableObject 탄 정의와 같은 방향이다.

| 자리 | 형식 |
|---|---|
| 탄 | `ProjectileDef` · `ProjectileCatalog`(`data/projectiles.xml`): 운동(광선 · 탄도 · 유도) · 탄속 · 중력 · 저항 · 수명 · 관통 · 튕김 · 폭발 · 태그 · `<OnHit when call>`(조건 · 동작 어휘) |
| 맞음 표 | 같은 파일 `<Impact id surface when color size>`, 처음 맞는 줄(`ProjectileCatalog::findImpact`) — 효과는 탄 × 표면이라 탄이 들지 않는다 |
| 무기 | `WeaponDef::_projectileID`(필수) · `_accepts`(탄 태그 식 — 호환은 조합 표가 아니라 태그 조건) · `<Modifier field op value>` |
| 수정자 | `ShotModifierDef`(`ShotField`, `Add` · `Multiply`, 음수는 0 에서 멈춤). 순서는 탄 → 무기 → 수정자 |
| 조합 | `ResolvedShot::resolve` — 장전 · 부착물이 바뀔 때 한 번(`WeaponState::equip` · `loadProjectile`) |
| 시뮬레이션 | `ProjectileSimulation` · `ProjectileState` · `IProjectileWorld` — 탄은 게임 오브젝트가 아니다 |

- **키트는 데이터와 반응만 갖는다.** `IProjectileWorld` 로 맞을 대상을 답하고 `ProjectileEvent` 를 받아 반응한다. 반응 수치는 탄에 베끼지 않고 가리킨다.
- **2D 는 평면 제약 3D 다**(언리얼 ProjectileMovement 의 평면 제약). 차원마다 관통 · 튕김 · 유도를 두 벌 쓰지 않는다.
- **결정성:** `ProjectileDef.cpp` · `ProjectileSimulation.cpp` 는 `sw_mark_deterministic_sources` 에 있고 삼각함수는 `DeterministicMathUtil` 이다. 중력은 값으로 받는다. 상태 바이트는 `ProjectileSimulation::writeState` 한 벌. 걸음 안에서 세계를 바꾸지 않는다(사건은 걸음 뒤).
- **키트 탄 표:** 키트는 자기 카탈로그를 받는 길로 탄 카탈로그를 받는다(게임 서비스 또는 생성 인자 — 판이 서비스를 찾지 않아 테스트 · 리플레이 · 서버가 같은 판을 연다). 벽 없는 판은 수명 · 사거리 없는 탄을 받지 않는다.
- **어빌리티 탄:** 탄은 시뮬레이션이 돌리고 이펙트 스펙은 탄 id 로 찾는 짐(`ProjectileCargo`)이다 — 런타임 값이라 상태 바이트에 넣지 않는다. 복제가 생기면 시뮬레이션은 서버 권위, 짐은 서버에 둔다(GAS 투사체와 같은 길).
- 확인: `ProjectileTest`, `ActionRoomTest.ShotsResolveFromTheProjectileCatalog` · `KartRaceTest.ItemsFlyByTheirProjectile` · `MechArenaRulesTest`, 시나리오 `game.shooter3d.projectilecombo` · `game.abilityarena.control`.

## 새 형식 체크리스트

1. `SW_ASSET_TYPE` 한 줄(로더 · 다시 읽기 방식 포함). 쿠킹이 펼칠 형식이면 문서 표에도 한 줄([DataInheritance](agents/DataInheritance.md) "새 형식을 올리는 법").
2. 목록 원소의 이름 칸에 `PROPERTY( InheritKey )`, 또는 카탈로그 규칙 한 줄.
3. 다른 리소스를 가리키는 칸은 `AssetRef<T>`.
4. 리소스 id 로 읽는 입구(2 절)를 쓰고, 쓰는 쪽은 `acquire` · `request` 로 받아 실행 상태는 자기 쪽에 둔다.
5. 조건과 동작은 등록된 어휘를 쓴다. 새 이름이 필요하면 어휘에 더한다.
6. 칸을 더하면 실행이 그 칸을 읽고 시나리오가 그 효과를 확인한다. 읽히지 않는 칸은 넣지 않는다.

키트 카탈로그 중 아직 쓰는 게임이 없는 것(SRPG · 정복 · 메트로배니아 등 약 20 종)은 라이브러리라 지우지 않고 캐시 등록과 접미사만 둔다.
