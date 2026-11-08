# 리소스 공통 구조 설계

> 질문: 모든 리소스를 최소 파일로 공유하고 여러 변형을 만들려면 구조를 어떻게 바꾸는가?
> 독자: 이 구조를 구현하거나 새 리소스 형식을 더하는 세션(에이전트와 사람).
> 결론: 리소스 종류 정의 · 합성 읽기 · 공유 캐시 · 타입 있는 참조를 한 벌로 만들고, 변형은 부모 → 조건 → 덮어쓰기 층의 합성 한 길로 처리한다. 행동 데이터는 실행기를 합치지 않고 조건 · 동작 어휘만 공유한다. 기준은 지금 닮은 파일 수가 아니라 앞으로의 기능이다.

엔진 비교는 기억에 의존한 설명이다. 구현 전에 그 기능의 문서를 다시 확인한다.

## 옛 구조와 문제(바꾼 까닭)

| 층 | 지금 | 문제 |
|---|---|---|
| 종류 정의 | `SW_ASSET_TYPE`(접미사 · 판 · 쿠킹), `SW_RESOURCE_DATA_KIND`(경로 규칙 · 검증 로더)가 따로 있다 | 같은 종류를 두 표에 적는다. `AssetDataKind::loadCatalog` 는 읽고 버리는 검증 전용이다 |
| 합성 | 파일 `_base`(`DataInheritance`), 항목 `_base`(`DataCatalogInheritance`), 프리팹 덮어쓰기, 스켈레톤 변형이 있고, 읽는 곳은 `XMLDocument::parseData` · `ReflectedXMLFile::parseDesc` · `Material` · `Skeleton` · `PrefabAsset` 로 나뉜다 | 조건별 값과 덮어쓰기 층을 넣을 자리가 형식마다 생긴다 |
| 공유 | `SharedAssetCache<T>` 와 `GameDataCache<T>` 가 따로 있고, 카탈로그 다수는 쓰는 쪽이 멤버 · 지역 변수로 사본을 읽는다 | 행동 트리 · 스케줄 · 퀘스트 등이 공유되지 않고 다시 읽기도 닿지 않는다 |
| 참조 | `string` 경로 칸과 이름 목록 `Config/Dev/AssetReferenceFields.json` | 칸이 어느 종류를 가리키는지 형식이 모른다. 이름 표는 사람이 맞춘다 |
| 행동 | 조건은 `ConditionExpr`, 동작은 `ActionRegistry` 하나다. `ScheduleActivityRegistry`(가는 곳의 규칙)는 어휘가 아니라 활동 종류라 남는다 | — |

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
- 공유 캐시가 있는 종류는 `SW_SHARED_ASSET_CACHE( id, T, 정의 )`(Engine) · `SW_MODULE_SHARED_ASSET_CACHE( id, T, 내보내기, 정의 )`(모듈) 한 줄이 같은 정의로 종류 등록 · 캐시 인스턴스화 · 레지스트리 등록을 함께 한다. 캐시는 정의의 이름 · 로더 · 다시 읽기 방식만 읽고 따로 적는 특성 표가 없다 — 이름이 둘이던 짝(`Rig`/`RigAsset`, `SpriteClip`/`SpriteClipAsset`, `Fracture`/`FractureAsset`, `BodyShapeSet`/`bodyshape`, `InteractionCatalog`/`interactions`, `ElementRuleCatalog`/`elementrules`)은 정의의 이름 하나가 되었고 에디터 리로드 길도 `AssetReloadRoute::sharedCache( 정의 )` 로 이름을 정의에서 꺼낸다.
- 제약: 팩 쿠킹은 게임 모듈을 올리기 전에 돈다(`HeadlessSubsystem::runCookTask`). 쿠킹이 알아야 하는 합성 규칙은 정적 등록이 아니라 데이터(`Content/engine/settings/datacataloginheritance.xml`)에 둔다. 배포 런타임도 이 표를 읽는다(덮어쓰기 층).

### 2. 합성 읽기 `ResourceComposer`

**한 길이다.** `parseData` · `parseDesc` · 머티리얼 · 스켈레톤의 펼치기를 `ResourceComposer::compose( text, id, context )` 하나로 모은다. 규칙 출처(리플렉션 · `SW_DATA_INHERITANCE_SCHEMA` · 카탈로그 표)는 지금처럼 루트 이름으로 찾는다.

```
1. 부모: 파일 _base, 항목 _base                                   (구현됨)
2. 조건: 플랫폼 · 대상 · 품질 · 언어 · 지역 · 데이터 레이어 · 기능 · 플래그  (구현됨)
3. 층: 게임 · DLC · 모드 · 라이브 운영 · 계절                         (구현됨)
```

부모와 층은 `DataInheritance` 의 합치기 규칙(열쇠 · `_op` · `_before` · `_after`)을 그대로 쓴다. 조건은 합치기 규칙을 쓰지 않는다 — 아래 까닭. 읽는 쪽은 단계를 모르고 펼친 글만 받는다.

**프리팹:** `PrefabOverrides` 는 열쇠 모양이 달라 유지한다(`DataInheritance.md`). 대신 문서 머리의 조건 · 층 단계는 같은 길을 지난다.

**스켈레톤:** `SkeletonVariant` 의 자체 사슬과 깊이 검사를 지우고 공통 사슬(`DataInheritance::collectChain`)로 옮겼다. "부모 본 번호를 바꾸지 않는다" 는 합친 뒤의 검증으로 남긴다. 칸 덮어쓰기로는 풀리지 않으므로(뼈 비율은 곱해지고 클립 규칙은 쌓이며 부모 루트 `<Skeleton>` 과 형식도 다르다) 합치기 함수(`SW_DATA_INHERITANCE_MERGE`)가 평평한 `<Skeleton>` 글을 낸다. 예외 길은 없다 — 읽기와 쿠커가 같은 함수를 지나고 쿠킹본에 `_base` 가 남지 않는다. 임포터의 54 개 파일은 고치지 않는다. 쌓인 값(`SkipIfEmpty` 칸 `_listVariantState`)은 펼친 글에만 있어 공용 스켈레톤의 글 지문은 그대로다.

**규칙 출처의 순서:** 문서 표 · 카탈로그 표(`Content/engine/settings/datacataloginheritance.xml`) → 합치기 함수 → 손 형식 → 리플렉션 형식. 쿠킹은 Engine 코드만 보므로 게임 · 키트 형식은 표에만 있어도 되고, 표와 코드가 어긋나지 않는지는 테스트가 본다.

#### 조건별 값

언리얼 Device Profile · 플랫폼별 ini 층(쿠킹 · 실행 때 CVar 를 고른다), Godot 기능 태그(프로젝트 설정 칸을 `칸.태그` 로 덮는다), 유니티 플랫폼별 임포트 설정(쿠킹 축만)에 해당한다(기억에 의존). 여기는 Godot 처럼 태그 집합 하나에 묻되, 언리얼처럼 쿠킹 축과 실행 축을 나눠 쿠킹 축은 쿠킹본에서 지운다. 형식은 둘이다.

글 형식은 고칠 원소 안에 조건을 적는다(`Engine/Serialization/Inherit/ResourceComposer.h`).

```xml
<VFXEffectDef _name="Sparks" _maxParticles="4096">
	<_when _if="target.server || quality.low" _maxParticles="512" />
	<_when _if="!platform.windows" _op="Remove" />   <!-- 이 원소를 뺀다(루트는 뺄 수 없다) -->
</VFXEffectDef>
```

- 조건은 **자기를 품은 원소 하나**의 속성을 바꾸거나(`칸="값"`) 그 원소를 뺀다(`_op="Remove"`). 항목 · 구조체 칸 · 목록 원소 어디에나 적고, 한 원소의 조건은 적힌 차례로 얹는다(나중이 이긴다). 자식 원소를 품은 조건은 오류다 — 조건부로 더하려면 원소를 적고 반대 조건으로 뺀다.
- 처음 설계는 조건 단계도 `DataInheritance` 의 합치기 규칙을 쓰기로 했다. 그러면 조건을 풀 때마다 그 형식의 규칙이 필요한데 규칙 출처가 쿠커(문서 표 `Config/Dev`), 개발 실행(리플렉션), 배포 런타임(규칙 표가 없다 — 로드 축 조건은 배포에서 푼다)마다 달라 같은 답을 장담할 수 없다. 원소 하나만 고치게 하면 규칙이 필요 없고 세 곳이 같은 함수(`ResourceComposer::resolveConditions`)로 같은 답을 낸다. 잃는 것은 "조건이 참이면 목록에 원소를 더한다" 한 줄짜리 표현뿐이다.
- 조건 속성 이름은 `_if` 다(`test` 는 행동 트리 조건 노드의 칸이다). 식은 [조건 어휘](#5-행동-어휘)와 같은 `ConditionExpr` 이고 그냥 이름은 태그 조건(`resourceTag`)이다. 태그 아닌 조건(`flag(x)` · `chance`) · 모르는 축 · 닫힌 값 목록 밖의 값은 읽기 오류다 — 리소스 문맥에는 게임 세계 칸이 없어 늘 거짓이 되기 때문이다. 게임 플래그는 게임 코드가 `flag` 축 태그로 켠다.
- 태그는 코어 범위의 리소스 루트(`ContentRootSubsystem::getResourceTags`)가 든 `ResourceTags` 다 — 합성 기록(`ResourceComposition`)도 같은 주인이 든다(감춘 싱글턴을 두지 않는다, 언리얼 Device Profile 이 엔진 초기화 앞에 서는 자리). 축 표는 코드 한 벌이다 — 축마다 값을 넣는 코드가 따로 있어 데이터로 늘어나는 정책이 아니다(값은 데이터다).
- 부모(`_base`)를 지나도 조건은 사라지지 않는다. 부모와 자식이 같은 원소에 적은 조건은 쌓이고(부모 다음 자식), 열쇠 목록의 원소로 세지 않으며, 차이 저장은 자식이 더한 조건만 적는다(`DataInheritance.md`).

바이너리(텍스처 · 오디오 · 메시)는 안을 고칠 수 없어 경로를 바꾼다(`ResourceRemap`). Godot 의 언어별 리소스 교체와 같고, 여기는 모든 태그 축을 받는다.

```xml
<ResourceRemap>
	<Remap id="common/ui/logo.dds" when="locale.ko" to="common/ui/logo_ko.dds" />
</ResourceRemap>
```

- 표는 게임 팩 설정 `<Engine _resourceRemap>` 이다. 같은 id 의 줄은 위에서부터 보고 처음 참인 줄을 읽는다. 사슬(바꾼 id 를 또 바꾸는 줄)은 오류다 — 한 단계면 줄 하나가 곧 답이다.
- 리소스 입구(`AssetIO` 의 읽기 · 비동기 읽기 · 있는지 묻기)가 찾기 전에 한 번 바꾼다. 쿠킹 작업 안에서는 바꾸지 않는다 — 쿠커가 기계의 언어로 다른 파일을 읽으면 쿠킹본이 기계마다 달라진다. 두 파일 모두 팩에 든다.

**축마다 정해지는 때가 다르다.**

| 축 | 정해지는 때 | 값을 넣는 곳 | 쿠킹본 |
|---|---|---|---|
| `platform`(windows · linux) · `target`(client · server) | 쿠킹 | 엔진 기동(쿠커도 같은 자리) | 펼쳐서 없어진다. 남은 식(아래)은 런타임이 같은 값으로 푼다 |
| `quality` | 로드 | 사용자 설정 핸들러 `resource.tag`(`graphics.quality` 의 `param="quality"`) | `_when` 이 남는다. 값이 바뀌면 그 태그를 읽은 리소스를 다시 읽는다 |
| `locale` | 로드 | 현지화의 지금 언어(`en-US` 는 `locale.en_US` — 식의 이름 글자에 `-` 가 없다) | 위와 같다 |
| `region` · `feature` · `flag` | 로드 | 게임 코드(`ResourceTags::setAxisValue` · `setTagActive`) | 위와 같다 |
| `layer` | 로드 | 켜진 씬 데이터 레이어(`SceneDataLayerSubsystem`) | 위와 같다 |

- 쿠커는 쿠킹 축 조건만 풀고, 한 원소에서 로드 축 조건이 나온 뒤의 조건은 차례를 지키려 함께 남긴다(뒤의 쿠킹 축 조건을 먼저 얹으면 같은 칸의 값이 뒤집힌다). 쿠킹본을 런타임이 푼 결과가 소스를 바로 푼 결과와 같은지는 `ResourceConditionTest.CookedTextResolvesLikeTheSource` 가 본다.
- 다시 읽기: 리소스 id 로 읽은 합성(`ResourceContext::withAsset` — `XMLDocument::loadResource` · `XMLSerializer::loadFromAsset` · `ReflectedXMLFile`)은 기댄 로드 축 태그를 기록에 남기고(`ResourceComposition`), 태그가 바뀌면 에셋 매니저가 프레임 단계에서 그 id 를 쥔 캐시를 다시 읽는다(`AssetManager::reloadComposedDependents` → `AssetCacheBase::reloadComposed`). 핫 리로드와 달리 배포 구성에도 있다 — 품질 · 언어를 바꾸는 것은 플레이어다. 공유 캐시는 종류의 다시 읽기 방식(제자리 · 떼어 내기)대로 바꾸고, GPU 캐시(텍스처 · 머티리얼 · 메시 · 프리팹)는 다음 읽기가 새 결과를 본다. 확인: 시나리오 `game.empty.resourcecondition`(품질 · 데이터 레이어를 바꾸면 표를 쥔 쪽의 값이 바뀐다).

**한계:** 캐시 열쇠는 경로뿐이므로 한 프로세스의 문맥은 하나다. 서버가 플레이어마다 다른 지역 값을 고르려면 문맥이 아니라 데이터의 키로 나눈다. 에디터처럼 씬이 여럿이면 `layer` 축은 마지막에 넣은 씬의 집합이다. 예외 하나: 한 문맥이 엔진 값에서 몇 축만 바꿔 끼우는 합성(`ResourceContext::withTagOverride` — 에디터 UI 미리보기의 `screen` · `orientation`)은 그 캐시가 경로 + 태그 열쇠로 따로 들고(`UITextAssetCache`), 다시 읽기 · 접속 대조 기록에 적지 않는다.

**품질 · 언어는 대조하지 않는다:** 축의 `ResourceTagScope::PerClient` 는 사용자 설정이라 달라도 된다. 판정 값은 이 축에 기대지 않는다(언리얼 Scalability 도 판정 값을 바꾸지 않는다, 기억에 의존).

**네트워크:** 게임플레이 값이 클라이언트마다 달라지면 판정이 어긋난다. 클라이언트는 서버에 붙자마자 로드 축 · 층에 기댄 합성 한 줄씩(리소스 id + 결과 해시)을 보내고 붙은 뒤 새로 읽거나 바뀐 합성도 그때마다 보낸다(`GameNetSessionMessage::kResourceDigest`). 서버는 받은 줄을 들고 있어(`ResourceDigestComparer`) 어느 쪽이 나중에 읽든 만나고, 같은 리소스를 다르게 합성했고 유예(`gv_netResourceDigestGraceSeconds`, 복제되는 태그는 양쪽이 다른 프레임에 바뀐다)가 지나도 다르면 그 연결을 끊는다(`Net.ResourceDigestMismatch`). 서버가 조건 · 층 없이 읽은 표도 해시를 들어, 클라이언트만 모드 층을 얹으면 거절된다. 서버가 읽지 않는 리소스(전용 서버의 시각 효과)는 대조하지 않으므로 품질 축이 시각 값만 바꾸면 붙고, 시뮬레이션 값을 바꾸면 거절된다. 언리얼은 접속 때 패키지 · 네트워크 버전을 대조하고 Godot 은 대조하지 않는다(기억에 의존) — 여기는 합성 결과 자체를 세션 내내 본다. 일관성 검사이지 부정행위 방지가 아니다(클라이언트는 거짓을 보낼 수 있다). 멀티 프로세스 확인: 시나리오 `editor.netresourcedigest`(전용 서버 + 클라이언트를 같은 씬으로 띄우고 클라이언트만 데이터 레이어를 켜면 끊긴다 — `netplay.start … scene=<씬>`). 쿠킹 축 조건과 로드 축 조건을 한 파일에 섞으면 클라이언트와 서버의 글이 달라 해시가 어긋난다 — 시뮬레이션 데이터에는 섞지 않는다.

#### 덮어쓰기 층

언리얼 Composite Data Table 과 팩 우선순위에 해당한다. 층은 순서 있는 목록(`ResourceLayers`, 아래 층이 먼저)이고, 리소스 `X` 의 층 파일 `<뿌리>/X` 는 원본을 암묵적 부모로 가진 차이다(`_base` 를 적으면 오류). 팩 우선순위의 파일 통째 교체와 달리 다른 칸만 적는다.

- 목록은 게임 팩 설정 `<Engine _listResourceLayer>` 의 뿌리(리소스 id 폴더 — `game/<팩>/layers/<이름>`)이고, DLC · 모드 팩을 올리는 쪽이 늘린다(`ResourceComposition::setLayers` — 리소스 루트가 든 기록, 바뀐 층에 파일이 있던 · 있는 리소스를 다시 읽는다).
- 층의 조건(`_when`)은 층의 차이에 얹는다 — 거짓이면 그 차이를 얹지 않는다. 합치기 규칙은 읽는 쪽의 형식에서 온다(리플렉션 형식은 리플렉션 — 배포 런타임에도 있다, 손 형식은 그 규칙 함수, 합치기 함수 형식은 그 함수, 카탈로그는 표).
- **처음 설계를 바꾼 것:** "게임 쿠킹은 자기 층까지 펼친다" 를 버리고 모든 층을 읽을 때 얹는다. 팩 쿠커는 도메인마다 팩을 굽고(`PackCooker::cookDomainPack`) 공용 도메인(`common` · `engine`) 팩은 게임마다 같아야 하므로, 게임 층을 공용 팩에 펼칠 자리가 없다. 층이 없는 게임은 글 검색 한 번으로 지나간다.
- 층은 리소스 id 로 읽을 때만 얹는다. 글로 받는 입구(`loadFromXMLText` — 편집기 미리 보기 · 테스트)는 id 가 없어 얹지 않는다. 형식의 `loadFromAsset` 은 그 형식의 문맥에 `withAsset` 을 실어 넘긴다(`loadComposed`). 머티리얼 · 씬처럼 범용 문서 입구로 읽던 손 형식은 `XMLDocument::loadResourceAs` 로 규칙을 골라 층이 표에 없는 규칙을 찾다 멈추지 않는다.
- 항목 id 를 바꾸지 않으므로 세이브는 그대로다.
- 신뢰 경계: 층 파일은 외부 입력이다. 합성 뒤에 그 형식의 검증이 그대로 돌고(읽는 쪽은 펼친 글만 받는다), 온라인 게임은 위 해시 대조로 거른다.

### 3. 공유 캐시

`SharedAssetCache<T>` 하나로 합치고 `GameDataCache` 를 지운다. 옛 객체를 살려 두는 동작은 `SharedAssetReloadMode::Detach` 와 `shared_ptr` 가 대신한다.

- **모듈 캐시:** 키트 · 게임 종류의 표를 그 모듈이 인스턴스화하고 모듈 수명으로 등록한다(`SW_MODULE_SHARED_ASSET_CACHE`, `Engine/Module/ModuleSharedAssetCache.h` — 모듈 서브시스템이 캐시를 들고 `AssetManager::registerAssetCache` 로 오르내린다). 핫 리로드의 내림은 `ModuleObjectRegistrar` 와 같은 사슬이다. `XMLCatalog` 형식 55 종 모두가 가진다.
- **창구:** `SharedAssetCache<T>::acquire( 경로 )` · `request( 경로 )` · `getReloadCount()` 를 바로 부른다. `XMLCatalog` 에 감싸개를 두지 않는다 — 내보내는 카탈로그가 베이스 멤버를 모두 인스턴스화해 캐시가 없는 카탈로그에서 링크가 깨졌다.
- **사본을 못 만들게 한다:** `XMLCatalog::loadFromAsset` 은 protected 이고 종류의 로더(`AssetTypeLoader`)만 friend 다. 쓰는 쪽은 `shared_ptr<const T>` 를 쥐고, 게임 범위 서비스는 `GameInstance::loadRequiredCatalog( shared_ptr<const T>& )` 가 받아 건다. 테스트 · 에디터 미리 보기는 `loadFromXMLText` 를 쓴다.
- **다시 읽기:** 카탈로그는 기본이 제자리(`InPlace`)라 게임 범위 서비스 · 디렉터가 쥔 같은 객체에 닿는다 — 쥔 쪽은 항목 포인터를 프레임을 넘어 들지 않고 id 로 찾는다. 진행 중인 세션이 옛 정의를 읽어야 하는 표(상호작용 · 원소 규칙)와 옮길 수 없는 표(`CurrencyCatalog` 는 정책이 자기를 참조, `MonsterCatalog` 는 복사 금지)만 떼어 낸다(`Detach`). 시나리오 `editor/catalogshared` 가 쥔 쪽의 값으로 본다.
- **카탈로그가 아닌 데이터 종류(W4):** 쓰는 쪽이 사본으로 읽던 형식도 같은 줄로 올렸다 — 엔진 `VFXEffectDef`(Detach — 방출기를 별칭 포인터로 쥔다) · `DestructionProfile`(InPlace — 구조 상태는 시작할 때의 값을 들고 다시 읽은 횟수로 다시 시작) · `RetargetProfile` · `FacialRig` · `LipSyncSettings` · `SurfaceChannelTable`(InPlace) · `TileSetAsset`(Detach — 칸마다 브러시 번호를 지어 둔다, 렌더러가 횟수를 보고 다시 짓는다), GameFramework `AIDirectorProfile`(Detach — 감독이 국면 · 풀 번호를 상태에 둔다. 판 도중에 다시 읽으면 `AIDirector::rebindProfile` 이 상태를 써서 새 객체로 옮겨 읽고, 구조가 바뀌었으면 옛 객체로 되돌린다) · `GimmickCircuitDef`, 키트 `ParkLayout`(로더가 같은 팩의 코스터 카탈로그를 받아 쥔다 — 게임 모드의 코스터 경로 칸은 지웠다) · `FighterCatalog`(기반 기술 카탈로그를 루트 `moves` 가 정한다) · `RhythmChart`. 모두 `loadFromAsset` 이 비공개(종류의 로더만 friend)다. 공유 캐시가 이미 있던 형식을 사본으로 읽던 곳(`PhysicsAsset` · `FitTables` · `Skeleton` · `AnimationClip` · `AnimationSet` · `BehaviorTreeAsset`)도 `acquire` 로 바꿨고, 엔진 형식 열하나(`Skeleton` · `AnimationClip` · `AnimationSet` · `SkeletonBoneLOD` · `RigAsset` · `PhysicsAsset` · `FitPartDef` · `BodyShapeSet` · `PairedAnimationDef` · `FractureAsset` · `SpriteLibrary`)의 `loadFromAsset` 도 비공개다.
  - **쿠킹은 사본을 읽는다:** 쿠킹 작업(`CookJob`)은 범위 안의 리소스 읽기를 입력으로 기록하므로 캐시가 쥔 한 벌을 쓰면 입력이 빠져 낡은 결과가 남는다. 그래서 쿠커가 읽는 형식(`Skeleton` · `AnimationClip` · `AnimationSet` — 정점 애니메이션 쿠커)만 의도가 드러나는 공개 이름 `loadCopyFromAsset` 을 둔다. 문맥을 받아 읽는 형식(`SocketSet` 종류 표 · `AnimationNotifyTable` 처리기 · `FitTables` 연산 레지스트리)은 경로만으로 나눌 수 없어 문맥을 받는 공개 읽기를 둔다.
  - **그대로 둔 것과 까닭:** 프로세스에 한 벌이고 서비스가 기동 때 받아 자기 상태로 옮기는 엔진 설정(`PhysicsSettings` · `UIScaleSettings` · `Render2DSettings` · `FontCatalogDef` · `UIThemeCatalog` · `TelemetrySchema` · `UserSettingsSchema` · `EngineDefaultAssets` · `GameSettings` · 오디오 믹서 · 음악 · 이벤트 라이브러리 · 문화권 표 · 애니메이션 LOD · 군중 설정 · 메시 LOD)는 나눌 쓰는 쪽이 없고, 바꾸면 백엔드 · 씬을 다시 세워야 해 제자리 다시 읽기가 닿지 않는다(언리얼 `UDeveloperSettings` · 유니티 Project Settings 처럼 설정 객체 한 벌, 기억에 의존). 물리 이름 검사의 대체 읽기는 에셋 매니저 없이도 돈다. `NavMeshSettings` 는 타일 굽기 워커가 함께 읽어 제자리 다시 읽기가 경쟁하고, 바꾸면 다시 구워야 한다. 쓰는 쪽이 고치거나 곧바로 실행 상태로 바꾸는 입력(`InputMap` 재바인딩 · 높이장 · 가중치 맵(브러시가 고친다) · 반사 · 빛 프로브 굽기 결과 · `NavMeshAsset` · `TileMapXMLData`(칠한다) · 의도 기록 트랙 · Live Link 배치)과 도구 입력(정점 애니메이션 쿠커 · 머티리얼 그래프 컴파일러 · 에디터 편집 사본)은 사본이 맞다.
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
- 쓰는 것만 담는 쿠킹: 팩 쿠커는 폴더째 담지 않고 뿌리에서 이 칸(생성 표, `_base` 부모 포함)과 곁 파일(`SW_ASSET_COMPANION` — 메시 옆 파쇄, 음성 옆 비즘, 스켈레톤 옆 본 LOD)을 따라 닿는 파일만 담는다(`PackReachability`, 언리얼의 "참조된 패키지만 쿠킹" · Primary Asset Rules 의 `AlwaysCook` 과 같은 모양 — 언리얼 쪽은 기억에 의존한 비교다). 뿌리는 `Config/Engine/PackConfig.json` 의 `_cookRules` 다 — 코드가 이름으로 읽어 참조로 닿지 않는 것(엔진 콘텐츠, 게임 데이터 · 맵 · 시나리오, 이름을 이어 붙여 읽는 유닛 모델)의 항상 담기 묶음과, 게임 설정 `<RequiredData kind pack>` 이 가리키는 다른 팩의 데이터(자리는 `_listRequiredData`, 등록 줄과 같은지는 `GameSettingsTest.CookRequiredDataPathsMatchTheKindRegistry` 가 본다). 닿는 참조가 어디에도 없는 파일을 가리키면 쿠킹 오류다(조용히 빼지 않는다). `common.pack` 은 굽는 게임들이 쓰는 것의 합이고, 쿠커가 도메인마다 담은 수 · 크기를 남긴다(`PackCooker.PackReachDomain`). 엔진 도메인도 같다 — 엔진 코드가 상수로 읽는 에셋은 상수 곁의 `SW_ASSET_COOK_ROOT` 가 뿌리이고(엔진 기본 에셋 표가 그 하나라 나머지는 그 칸으로 닿는다), 게임 코드의 리터럴만 항상 담기 데이터다. 자동화 없는 배포는 시나리오 묶음(`_bAutomationOnly`)을 쓰지 않는다. 큰 카탈로그는 항목 단위로 고른다(`_listEntryCatalog` · `_listEntryIDField`) — 필수 데이터나 `파일#id` 로 닿으면 id 칸이 부른 항목의 에셋만 담고, 폴더 참조(표 전체를 보여 주는 캐릭터 편집 화면)로 닿으면 전부 담는다. 스테이징 산출물은 원본(같은 줄기, 셰이더는 `.hlsl` 줄기)이 쓰일 때만 담고 원본을 가릴 수 없는 것은 담는다(`PackStagedSelection`). 칸 규칙(`AssetReferenceFields`)은 그래서 에디터 모듈이 아니라 엔진(`Engine/Asset/Reference`)에 있다.
- 이름 표는 없다: 경로 칸 이름은 리플렉션 선언에서만 온다(생성 목록). 손으로 읽던 XML 칸은 `XMLFields` 로 읽는 스키마 구조체로 옮겼다(타일맵 머리 `TileMapHeaderParams` — 자식 원소 글에서 루트 속성으로, 본 마스크 `file`). `Config/Dev/AssetReferenceFields.json` 에는 칸 이름이 아니라 도메인 · 글 확장자 · 임포트 폴더 · CMake 인자 규칙만 남는다(언리얼이 `FSoftObjectPath` 칸만 참조로 보는 것과 같다).
- 값 모양: `AssetRefBase`(직렬화 층, 경로 + `const AssetTypeDef*`)와 `AssetRef<T>`(에셋 층, 칸을 더하지 않는다). 글 처리기는 값 주소만 받으므로 대상 종류를 프로퍼티 메타가 아니라 값이 품는다. 그래서 `vector<AssetRef<T>>` · 중첩 구조체 · 덮어쓰기 패치가 같은 검사를 지난다. 파서는 `TComponentHandle<T>` 처럼 칸 형식을 내장 이름 `AssetRef` 하나로 모으고 `AssetPath` 메타를 켠다.
- `T` 는 칸이 만들어지는 곳에서 완전해야 한다(`unique_ptr<T>` 소멸자와 같은 규칙). 생성자를 .cpp 에 둔 컴포넌트는 헤더에 앞선 선언만 둔다. `acquire` · `request` 가 멤버 템플릿인 까닭도 같다 — 칸 선언이 `AssetRefCache<T>` 를 인스턴스화하면 뒤에 오는 캐시 헤더의 특수화가 컴파일 오류다.
- 생성 목록: 파서가 대상마다 `AssetReferenceFields.list` 를 쓰고 빌드가 저장소의 표 `Config/Dev/AssetReferenceFields.generated.json` 으로 모은다(주인별, 이 구성이 짓는 주인의 줄만 갈아 끼운다). 커밋 전 린트와 `move-asset` 이 빌드 없이 돌기 때문에 표를 저장소에 둔다. 낡음은 `--check-asset-refs` 가 잡는다.
- 종류를 가르는 셋: 접미사(`.sockets.xml`), 카탈로그의 경로 규칙(`/data/crops.xml` — 헤더의 `kAssetType = AssetTypeDef::makeDataFile( … )` 에 두고 등록 줄은 로더만 더한다), 내장 이름(`withBuiltinNames` — 메시의 `Cube` 처럼 파일이 없는 에셋). 셋 다 없는 종류(`makeCacheOnly` — 지금은 캐시 테스트의 형식뿐이다)는 경로로 가를 수 없어 `AssetRef<T>` 가 컴파일에서 막는다(`AssetTypeDef::canTellPath`) — 접미사나 규칙을 먼저 준다. 내장 이름은 철자 그대로만 맞고 모르는 이름은 폴백 없이 오류다. 머티리얼 칸의 `None` 은 내장 이름이 아니라 `hashed_string` 이 빈 이름을 쓴 철자였다 — 데이터를 빈 글로 옮겼고 이제 로드 오류다.
- 칸이 만들어지는 곳에서 `T` 가 완전해야 하므로, 대상 헤더를 읽을 수 없는 구조체(`SpriteClipAsset` · `SpriteLibrary` · `BehaviorSubTreeParams`)는 생성자를 .cpp 로 옮기고 헤더는 앞선 선언만 둔다. 재생 입구처럼 종류 둘을 받는 칸은 표에 올리지 않는 검사용 종류(`AudioPlayableAsset`)를 대상으로 삼는다.
- 생성 목록은 칸마다 C++ 이름과 XML 속성 이름(`XMLFields::getAttributeName` — `_shaderPath` → `shaderPath`, `Name = "mesh"` 는 그대로)을 함께 낸다. 그래서 속성으로 읽는 문서의 경로 칸도 손 표 없이 참조 검사 · 옮기기가 안다.
- 전환하지 않는 칸은 `PROPERTY( AssetPath )` 로 두면 생성 목록에 든다: 인턴 열쇠(`hashed_string`)로 쓰는 칸(외형 데이터 · 오디오 이벤트 · 음악 층), 종류가 여럿인 칸(머티리얼 그래프 노드 에셋, 셰이더 소스), 종류 정의가 없는 파일(글꼴, 나일 자동 계획), 편집 사본이 치는 중인 글(`SequenceTrackItem::_prefab`).

### 5. 행동 어휘

**실행기는 합치지 않는다.** 행동 트리(틱 반응), 스케줄(게임 시간표), 대화(사람의 선택 대기), 어빌리티(수명 · 복제), 기믹 회로(고정 스텝 상태)는 모델이 다르다. 언리얼도 Behavior Tree · StateTree · GAS 를 나누고 Gameplay Tag 로 어휘를 공유한다.

**조건 `ConditionExpr`** (`Engine/Behavior/ConditionExpr.h`)

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

합치는 곳: `ScheduleCondition`, `EquipmentCondition`, `AppearanceRuleCondition`, 대화 `_condition`, `BehaviorBlackboardCondition`, `IShopConditionEvaluator`.

합치지 않는 곳: `AnimationConditionOp`(매 프레임 캐릭터마다 도는 값 비교), `UserSettingConditionOperator`(게임 문맥이 없는 설정 화면).

**동작 `ActionRegistry`** (`Engine/Behavior/ActionRegistry.h`)

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
| 퀘스트 · 루트 · 스폰 · 상점 | `datacataloginheritance.xml` 에 줄 추가(퀘스트 · 스폰은 있었고 루트 · 상점을 더함) | — |

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
| 인스턴스 값 | 파일 없이 인스턴스마다 float4 둘(`InstanceCustomData`, 인스턴스 원소 160 바이트). 내용이고 배치 키가 아니다. `MaterialInstance` 는 값 · 텍스처 · 키워드를 덮는 드로우 단위로 남는다. 구현됨 | 언리얼 Custom Primitive Data · 유니티 MaterialPropertyBlock |
| 파라미터 | 외형 구간 머티리얼: 부품 · 변형 · 규칙 · 피해 단계가 메시 구간 이름마다 머티리얼과 매개변수를 정한다(`AppearanceSectionDef`, `ResolvedMaterialValue::_section`). 병합해도 구간이 남는다. 런타임 아틀라스는 두지 않는다. 구현됨 | 언리얼 Material Slot 덮어쓰기 · 유니티 `Renderer.materials`(여기는 번호가 아니라 이름) |
| 조립 | 씬 인스턴스(`<entity scene="…">` + `<Part path>`): 씬을 다른 씬에 넣고 덮어쓴다. 읽을 때 평평한 엔티티로 펼치고 쿠킹본도 펼친 것을 담는다 | 언리얼 Level Instance · Godot 씬 상속 |
| 조립 | 씬 상속(루트 `_base`): 엔티티 id 가 열쇠인 차이만 적는다. 씬 템플릿 자리를 겸한다 | Godot 씬 상속 |
| 조립 | 데이터 레이어(`layers="Winter,Night"`): 켜진 이름 집합의 객체만 올린다(계절 · 이벤트). 2 절 조건의 문맥 태그가 이 이름을 켠다 | 언리얼 Data Layers |
| UI | 스타일 시트 `_base`(규칙 표 한 줄), `<TypeVariation>`(읽을 때 규칙 복제), 에디터 테마 토큰 `_base`(JSON 이라 키 덮기). 구현됨 | Godot Theme Type Variation · 유니티 USS |
| 기타 | 리그 · 리타겟 · 물리 · 소켓 · 알림 표 · 입력 레이어에 `InheritKey` | — |

구현에서 정한 것:

- **애니메이션 셋:** `base` 를 공통 `_base` 로 옮겼다. `set`(줄 하나를 다른 셋에서 빌림)은 여러 파일을 섞는 믹스인이라 `_listRenderPassRef` 와 같은 까닭으로 남긴다. 셋을 바꾸면 상태 · 위상과 코드 레이어가 새 셋의 같은 이름 클립으로 잇는다.
- **그래프:** 상태는 이름(자식이 더한 상태의 id 가 부모와 겹치면 오류로 드러난다), 전이는 id, 레이어 · 마스크는 이름이 열쇠다. 연결 레이어는 `<layer slot>` 을 `SkeletalAnimatorComponent::_listLinkedLayer` 의 그래프로 채운다. 채운 그래프의 클립 이름과 파라미터는 애니메이터의 것을 함께 쓰고, 레이어 안의 레이어는 없다(한 단계).
- **노드 · 레이어 LOD:** 등급은 한 축이다(`AnimationLODState::_lodGrade` = `animationlod.xml` 의 주기 단계 + 예산이 주기를 두 배로 늘린 횟수). 그래프 `<layer>` · 블렌드 스페이스 `<node>` 와 리그 노드(IK · 룩앳 · 스프링 본)가 `max_lod` 칸으로 그 등급에서 꺼지고, `_nodeFadeSeconds` 동안 가중치를 옮겨 튀지 않는다(`AnimationLODFade`, 언리얼 LOD Threshold 는 끊는다). 칸이라 먼 NPC 변형은 `<layer name="Aim" max_lod="1"/>` 만 적는다. 본 LOD 와는 따로 돈다 — 리그 노드가 쓰는 본은 본 LOD(`SkeletonBoneLOD`)의 핵심 본(`SkeletalMeshComponent::requireBones`)이라 빠지지 않고, 레이어가 빠진 본을 써도 그 본은 그리는 메시가 쓰지 않는다.
- **2 차 물리 LOD:** 흔들림 본 · 망토 · 머리 가닥은 리그 `SpringChain` 이라 위 `max_lod` 로 끈다(다 꺼지면 상태를 버리고, 켜지면 애니메이션 포즈에서 섞여 들어온다). 물리 천(`ClothComponent`, Jolt 소프트 바디)은 조상 스켈레탈 메시의 같은 등급을 읽어 반복 수를 낮추고(`_listLODIterationCount` — 등급 g 는 g − 1 번째, 끝을 넘으면 마지막, 등급 0 은 `_iterationCount`. 다시 만들지 않고 `IPhysicsSoftBodyScene::setSoftBodyIterationCount`) `_maxLOD` 를 넘으면 내린다. 내린 동안은 고정 자세에 건 쉬는 격자를 그린다(언리얼 Chaos Cloth 가 시뮬레이션을 끈 LOD 에서 스킨 메시를 그리는 자리 — 그쪽은 LOD 마다 시뮬레이션 메시를 따로 두지만 여기 격자는 하나다, 기억에 의존한 비교). 다시 켜면 지금 고정 자세에서 새로 만든다(스킨이면 그 프레임에 스킨 위치로) — 옛 정점에서 이으면 내린 동안 옮겨 간 고정점이 천을 잡아채 튄다. 스켈레탈 메시 조상이 없는 천(커튼 · 깃발)은 등급 0 이다. 확인은 단위 테스트 `ClothLODTest` 와 값 시나리오 `game.empty.clothlod`(기사 등의 천 `maps/clothlod.scene.xml` — 카메라를 `object.place` 로 멀리 옮기면 감시 값 `Cloth.LODGrade` · `Cloth.IterationCount` · `Cloth.LODSuspended` 가 6 → 4 → 2 → 내림 → 6 으로 바뀐다)다.
- **스프라이트 라이브러리:** 아틀라스는 파일에 하나다. 라벨마다 텍스처가 다르면 프레임을 넘길 때 머티리얼 인스턴스가 바뀌어 배치가 깨진다.
- **소켓 · 알림 표:** 원소 종류가 섞이고 열쇠 칸이 달라 `datacataloginheritance.xml` 규칙 한 줄이다. 리그 · 리타겟은 `SW_DATA_INHERITANCE_SCHEMA`, 물리 에셋은 `PROPERTY( InheritKey )` 다.
- **입력 맵:** 손 형식이고 읽기가 범용 문서 읽기(`XMLDocument::loadResource`)라 카탈로그 표 한 줄이다(`<Catalog root="InputMap">`). 레이어 정의(`<layers><layer name>`)는 래퍼 안의 목록이라 항목이 아닌 루트 칸(카탈로그 줄 바로 아래 `<Field>`)이고, 액션을 담는 `<layer name>` · 루트 `<action name>` 은 이름이 열쇠인 항목이다. 바인딩 원소는 열쇠가 없어 종류째 바뀐다. 묶음(`<override>` · `<remove>`)은 실행 중 쌓기이고 상속은 파일 쪽이다(언리얼 Enhanced Input 매핑 컨텍스트 우선순위와 같은 자리).
- **뺀 것:** 물리 셰이프(이름이 없다) · 충돌 쌍(열쇠가 뼈 둘이다), 리그 · 리타겟 안의 뼈 목록(순서가 뜻이다)은 통째로 바뀐다.

### 7. 무기와 발사체 조합

지금은 탄속 · 낙차 · 피해가 `WeaponDef` 안에 있어 "소총 + 폭발탄" 하나에 무기 정의 하나가 든다. 발사체 구조체도 키트마다 따로였다(`Ballistics::Projectile`, `ProjectileComponent`, `MechProjectile`, `KartProjectile`, `ActionProjectile`, `ArenaProjectileComponent` — 모두 아래 시뮬레이션 하나로 옮겼다). 정의를 나눠 무기 N 종과 발사체 M 종이 파일 N + M 개로 조합되게 한다.

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
- **키트 탄 표:** 몬스터 사격(`MonsterShotDef` — `angle` · `projectile`) · 기체 특수기(Special 슬롯의 `projectile`) · 카트 아이템(날거나 놓이는 아이템의 `projectile`)은 쏘는 법과 맞은 뒤의 반응만 갖고 탄 수치는 탄 정의다. 키트가 카탈로그를 받는 길은 그 키트가 자기 카탈로그를 받는 길과 같다 — 액션 룸은 게임 서비스(몬스터 카탈로그처럼 `game::findSubsystem`, 종을 처음 세울 때 탄을 풀어 둔다), 기체 대전 · 카트 경기는 생성 인자(`initialize` — 판이 서비스를 찾지 않아 테스트 · 리플레이 · 서버가 같은 판을 연다). 카트 탄의 정의 id 는 반응을 찾는 아이템 id 로 둔다(상태 바이트가 그대로다). 벽이 없는 판(액션 룸 · 아레나)은 수명이나 사거리가 없는 탄을, 기체 특수기는 사거리 없는 탄을 받지 않는다.
- **어빌리티 탄(AbilityArena):** 탄은 디렉터의 시뮬레이션 하나가 바닥 평면에서 돌리고(`ArenaGameModeComponent`, PrePhysics 스냅샷으로 맞음), 실은 어빌리티 이펙트 스펙은 탄 id 로 찾는 짐(`ProjectileCargo`)이다. 스펙은 쏜 쪽 어빌리티 시스템을 가리키는 런타임 값이라 탄 상태 바이트에 넣지 않고 상태 저장 전에 모습과 함께 걷는다. 맞으면 틱 뒤 게임 스레드에서 맞은 쪽 어빌리티 시스템에 건다 — 적용 · 차단은 그 시스템의 규칙이고 탄은 나르기만 한다. 이 엔진의 어빌리티는 복제 · 예측이 없고, 언리얼 GAS 도 투사체는 서버 액터가 스펙을 들고 날아 서버에서 거는 길이라(기억에 의존한 비교) 복제가 생기면 결정적 시뮬레이션은 서버 권위로 두고 짐만 서버에 남긴다. `ArenaProjectileComponent` 는 탄을 읽기만 하는 모습이다. 탄 수치는 어빌리티의 `<Param name="projectile">` 이 가리키는 탄(`game/abilityarena/data/projectiles.xml`)이고 피해는 피해 이펙트의 SetByCaller 로 간다.
- 확인: 단위 테스트 `ProjectileTest`(조합 · 받음 · 순서 · 맞음 표 · 맞음 동작 · 관통 · 튕김 · 유도 · 사거리 · 수명 · 상태 왕복), `ActionRoomTest.ShotsResolveFromTheProjectileCatalog` · `KartRaceTest.ItemsFlyByTheirProjectile` · `MechArenaRulesTest`(특수기의 탄), 시나리오 `game.shooter3d.projectilecombo`(같은 소총에 소이탄을 넣으면 피해 감시값과 탄도선 그림이 바뀐다), `game.abilityarena.control`(화염구가 날아 처치 수가 오른다).

## 새 형식 체크리스트

1. `SW_ASSET_TYPE` 한 줄(로더 · 다시 읽기 방식 포함).
2. 목록 원소의 이름 칸에 `PROPERTY( InheritKey )`, 또는 카탈로그 규칙 한 줄.
3. 다른 리소스를 가리키는 칸은 `AssetRef<T>`.
4. 쓰는 쪽은 `acquire` · `request` 로 받고, 실행 상태는 자기 쪽에 둔다.
5. 조건과 동작은 등록된 어휘를 쓴다. 새 이름이 필요하면 어휘에 더한다.
6. 칸을 더하면 실행이 그 칸을 읽고, 시나리오가 그 효과를 확인한다. 읽히지 않는 칸은 넣지 않는다.

## 위험

| 위험 | 대응 |
|---|---|
| 조건 어휘 비용 | 읽을 때 컴파일하고, 매 틱 조건은 측정으로 확인한다 |
| 로드 축 `_when` 이 많아지면 품질 변경 때 다시 읽기가 몰린다 | 태그별로 기댄 리소스를 기록해 그것만 다시 읽는다(`ResourceComposition`, 구현됨) |
| 코드 기본값 변경이 적지 않은 칸의 값을 바꾼다 | 이미 있는 규칙(`DataInheritance.md`)을 따른다 |
| 합성 코드가 배포 구성에 남는다 | DLC · 모드 층이 없는 게임은 쿠킹본에 합성할 것이 없어 글 검색 한 번으로 지나간다 |

## 정한 것

구현과 함께 정한 결정이다. 사용자 결정이 필요한 항목은 없다.

- 메시 런타임 조립(언리얼 Mutable): 이미 이 엔진의 길이다 — 부품 피팅 · 체형 모프 굽기(`AppearanceMeshPart::assemble`) → 구간 있는 메시 하나로 병합 · LOD(`AppearanceMeshPart::mergeParts`) → 결과 캐시(`AppearanceBakedMeshCache`, 키는 해석의 메시 해시 · 부품 · 원본 내용 토큰 · 데이터 판)와 진행 중 짓기 나눔(`CharacterAppearanceInFlightTable`), 짓기는 워커(`CharacterAppearanceMeshBuild`). 조립 단계는 씬 · 컴포넌트 없이 돌아 테스트(`AppearanceMeshAssemblyTest`)가 그대로 부르고, 컴포넌트는 입력을 모으고 결과를 걸기만 한다. 피부 층 텍스처를 캐릭터마다 굽는 단계(Mutable 의 텍스처 층 합성)는 넣지 않는다: 염색 · 마스크 채널 · 표면 상태는 머티리얼 매개변수(`ResolvedMaterialValue`, `SurfaceState`)라 캐릭터마다 텍스처 메모리가 들지 않고 군중이 머티리얼을 나누며, 그리기 줄이기용 아틀라스는 고정 차림을 에디터가 쿠킹한다(`OutfitAtlasSpec` — 엔진의 `OutfitAtlasComposer` · `mergeMeshes` 훅). Mutable 은 텍스처 층을 굽는 대신 인스턴스마다 텍스처를 들고, 유니티 UMA 도 런타임 아틀라스 메모리를 치른다 — 이 엔진은 매개변수 쪽을 골랐다.
- 리소스 입구: 파일 하나가 리플렉션 형식 하나인 리소스(`FacialRig` · `LipSyncSettings` · `LiveLinkFaceLayout` · `LiveLinkFaceMapping` · `AnimationLODSettings` · `MeshLODSettings` · `SpriteClipAsset` · `SpriteLibrary` · `DialogueGraphAsset` · `SequenceAsset`)는 `ConfigManager::readComposedXMLDocument` 로, 루트를 손으로 읽는 형식(`AnimationCrowdSettings` · `CharacterLODSettings` · `VisemeTrack`)은 `XMLReadUtil::parseResourceRoot` 로, `TileSetAsset` · `Render2DSettings` 는 `XMLDocument::parseResourceText` 로, `SkeletonBoneLOD` 는 자기 규칙의 `ResourceComposer::compose` 로 읽어 리소스 id 로 읽으면 조건 · 층 · 다시 읽기 기록 · 접속 대조를 모두 받는다(테스트 `ResourceEntranceTest.SettingsAndRootFormatsRecordTheComposition`). 글을 읽은 뒤 id 없이 글 입구로 넘기는 `loadFromAsset` · `loadFromFile` 은 린트(`common-shapes` 의 `assetEntrance`)가 막는다. 예외 넷(`Config/Dev/CommonShapeExceptions.json`): 문화권 표(locale 축을 정하는 쪽), 경로 교체 표(입구가 찾기 전에 쓰는 표), 버텍스 애니메이션 쿠킹 목록(쿠킹 문맥), 에디터 아이콘 글리프 표(에디터 설정). 팩 설정(`PackEngineSettings`)은 층 · 조건이 기대는 값을 정하는 쪽이라 `readConfigXMLElement` 로 그대로 읽는다. `Render2DSettings` 는 하위 시스템이 쥐어 캐시 다시 읽기(`reloadComposed`)에는 닿지 않는다 — 조건 · 층 · 대조만 받는다.
- 편집기 저장: 사본은 지금 태그로 푼 값을 든다(형식의 칸에 조건이 들 자리가 없다). 저장 입구(`ResourceComposer::saveText`)가 있던 파일의 조건을 되돌리므로 `_when` 은 파일에 남고, 참인 조건이 정한 칸의 편집은 그 조건의 값이 된다(규칙은 `restoreConditions` 머리말). 조건 원소 자체를 고치는 화면은 없다 — 원문을 고치는 창(카탈로그 · 데이터 표)이나 텍스트 편집기로 고친다. 참인 조건이 정한 칸을 기본값으로 되돌린 편집은 글에 칸이 없어 가릴 수 없어 조건 값이 남는다. 층을 얹어 읽은 리소스는 원본 자리에 쓰지 않는다(`ResourceLayer.SaveWouldBake`). 에디터 확인: 시나리오 `editor.resourcesave`(애니메이션 그래프를 두 품질에서 저장하고 다른 품질로 다시 읽어 조건이 남았는지 전이 수로 본다).
- `XMLCatalog` 계열 중 파일에서 읽는 쪽이 저장소에 없는 키트 카탈로그(약 20 종 — SRPG · 정복 · 메트로배니아 · 공포 · 몬스터 · 파티 · 카트 등)는 지우지 않았다. 규칙 클래스가 `const T*` 를 받는 라이브러리이고 아직 그것을 쓰는 게임이 없을 뿐이라, 캐시 등록과 접미사(`.shops.xml` · `.loot.xml` · `.monsters.xml` …)만 정했다. 접미사가 있어 칸은 `AssetRef<T>` 로 받는다. 시연이 쓰는 셋은 Empty 가 세웠다(장르 키트 시연 K1): `ActionPlatformerCatalog` · `SRPGCatalog` 는 디렉터의 `AssetRef<T>`(`game/empty/data/kitdemo/`), 액션 룸이 게임 서비스로 찾는 `MonsterCatalog` 는 탄 카탈로그처럼 팩 경로 규칙(`data/monsters.xml`, 종류 `MonsterCatalog`)을 더해 게임이 필수 데이터로 건다.
