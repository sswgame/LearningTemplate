# GameFramework (장르 공통 뼈대)

여러 게임에서 반복적으로 사용되는 장르별 공통 로직이나 키트(Kits)가 모여있는 곳입니다.

App은 이 라이브러리를 링크하지 않습니다. 게임플레이 입력은 `Engine/Input/ActionMap.h`의 인스턴스를 사용합니다.

## 하위 폴더 구성

`GameFramework` 타겟(= 모든 키트가 깔고 앉는 기반):

- **Ability**: 언리얼 Gameplay Ability System 과 같은 어빌리티 시스템 — `AbilitySystemComponent`(어트리뷰트 · 이펙트 · 어빌리티 · 태그 개수),
  `AttributeSet` · `CombatAttributeSet`, `GameplayEffectDef` · `GameplayEffectSpec`(즉시 · 지속 · 무한 · 주기 · 스택 · 실행 계산), `GameplayAbility`
  (태그 조건 · 비용 · 쿨다운 · 트리거 · 입력) · `AbilityTask`, XML 카탈로그(`AbilityCatalog`). 장르를 가리지 않아 키트가 아니라 기반에 있습니다(턴제는
  틱을 끄고 턴마다 `advanceTime( 1 )`). 같은 오브젝트의 `HPBarBaseComponent` · `DamageUIComponent` 와 이어집니다. 자세한 것은 `Ability/README.md`,
  쓰는 예는 `Source/Games/AbilityArena`
- **Base**: 키트 공통 수명(`IGame`, `GameInstanceBase`, `GameService`), 세이브 베이스(`SaveGame`),
  장르 무관 컴포넌트(`EffectBaseComponent`, `GravityComponent`, `DontDestroyOnLoadComponent`).
  공유 타입은 `GameFrameworkMinimal.h`. "game" 채널의 수명주기 이벤트(`GameEvents.h`)는 프레임워크가 그 자리에서 낸다 —
  `GameInstanceBase` 가 세이브 · 로드 완료와 씬 로드 요청 · 완료, `GameModeStateMachine` 이 일시정지 진입 · 해제
- **Data**: `GameData`, `GameStrings`
- **Transition**: `GameModeStateMachine`(일시정지 모드 전이에 `GamePausedEvent` · `GameResumedEvent`), `ScreenTransitionManager`
- **UI**: 장르 무관 UI 컴포넌트 — `RuntimeHud`, `DialogueRunnerComponent`,
  `HPBarBaseComponent`, `DamageUIComponent`. HP 바 · 데미지 숫자는 월드 공간 스프라이트(`SpriteInstanceBatch`)로 그린다 — 저장되는
  컴포넌트를 만들지 않는다. 입력은 `HPBarBaseComponent::setTargetRatio` · `DamageUIComponent::setDamageValue` 하나씩이다.
  `EffectBaseComponent` 의 흐림은 같은 오브젝트 스프라이트들의 색 알파에 곱해진다.

별도 타겟:

- **Kits**: 키트끼리 링크하지 않음. 공유 타입은 Base/UI 로.
  - `ActionCombat`: 공격 히트박스(`AttackBaseComponent`), 투사체, 유닛 스탯, 액션 룸.
    피해는 한 길이다 — 투사체(`ProjectileComponent`)와 공격 판정은 같은 오브젝트의 `BoxCollider2DComponent` 겹침으로 맞음을 알고
    `UnitStatsComponent::takeDamage( 피해, 쏜 쪽 )` 을 부르며, HP 가 깎인 그 자리에서 컴포넌트 델리게이트(`registerDamageApplied`)가 불리고
    `DamageAppliedEvent`("game" 채널)가 나간다. 액션 룸은 시작 · 클리어 · 패배에 룸 이벤트를 낸다. 채널 이벤트는 `GameEventUtil::send` 하나로 낸다 —
    버스 스레드면 그 자리에서, 아니면 다음 `processEvents` 에.
  - `Overworld`: 오픈월드형 필드 탐색 시스템
  - `TurnBattle`: 턴제 전투 시스템

## 무엇이 키트에 들어가고 무엇이 기반에 남는가

**의존 관계로는 판별되지 않는다.** 키트 컴포넌트는 하나같이 `Engine` 만 include 하므로, 컴파일러
입장에서는 어디에 둬도 똑같다. 그래서 기준을 적어 둔다 — 적어 두지 않으면 "처음 필요해진 키트"
에 남고, 다른 장르는 그걸 쓰려고 키트를 링크하거나(금지) 복사한다.

기준: **다른 장르의 게임이 이 타입을 그대로 쓰겠는가?**

- 쓴다 → `Base` (로직·수명) 또는 `UI` (화면에 뜨는 것).
  HP 바와 데미지 숫자는 턴제도 쓴다. 중력은 플랫포머도, 탄막도 쓴다.
  이 셋은 `Kits/ActionCombat` 에 있었는데 액션에만 쓰이는 것이 아니었다.
- 안 쓴다 → 그 키트. 공격 히트박스·투사체·액션 룸처럼 **장르의 규칙을 담은 것**이 여기 해당한다.

## 리플렉션 — 폴더를 늘릴 때

리플렉션 대상 헤더는 소스와 **같은 규칙**으로 모은다(재귀 GLOB). 예전에는 `GameFramework` 가
`Base`/`Data`/`Transition`/`UI` 네 폴더를 이름으로 적어 두었고, 키트는 루트의 `*.h` 만 모았다.
그래서 최상위 폴더나 키트 하위 폴더를 새로 만들면 **그 안의 `REFLECT()` 타입이 조용히 등록되지
않았다** — 컴파일은 통과하고 역직렬화만 실패하므로 원인을 찾기 어렵다. 지금 키트는
`sw_addReflectionStep` 에 헤더 목록을 넘기지 않고 자동 탐색에 맡긴다.

## 사용법
`Source/Games/내게임/CMakeLists.txt`에서 빌드 시 필요한 키트만 골라서 링크(`target_link_libraries`)하면 해당 기능들을 가져다 쓸 수 있습니다.

## 키트를 만들 때 — 장르의 뼈대이지 게임 하나의 스키마가 아니다

키트는 **장르 전체**가 쓰는 것이라, 게임 하나의 규칙이 타입에 박히면 그 장르의 다른 게임은
이 키트를 못 쓴다. 실제로 세 곳이 그랬고 다음 규칙으로 고쳤다.

**개수를 코드가 정하지 않는다.** `SpeciesDef` 는 기술이 `_move0` / `_move1` 두 칸,
`PartyMember` 는 PP 가 `_pp0` / `_pp1` 두 칸이었다. 기술이 넷인 턴제 게임은 만들 수 없었다.
지금은 `vector` 이고 XML 이 `move0`, `move1`, ... 을 끊길 때까지 읽는다. 세이브에는 개수를 함께
적고, 개수가 없는 예전 세이브는 두 칸 형식으로 폴백한다.

**종류를 코드가 정하지 않는다.** `MonsterDef` 는 보상이 `_dropExp` / `_dropGold` 뿐이라
소울·탄약·파편을 주는 액션 게임은 표현할 수 없었다. 지금은 `_mapDrop` 이고
`<Drop exp="10" souls="3"/>` 처럼 **속성 이름이 곧 보상 이름**이다. `RuntimeHud` 가 게이지를
이름 맵으로 다루는 것과 같은 방식이다.

**같은 문제는 같은 방식으로 푼다.** id → 행 조회를 `MonsterDataCatalog` 는 `hashed_string` 맵으로,
`SpeciesCatalog` 는 벡터 선형 탐색 + `string` 비교로 하고 있었다. 한 프레임워크 안에서 같은 일을
두 방식으로 하면 읽는 사람이 어느 쪽이 정석인지 알 수 없다. 둘 다 맵이다 — 다만 인덱스가
직렬화되는 곳(`SpeciesDef::_listMoveIndex`)은 **벡터의 자리를 그대로 두고** 맵을 곁에 둔다.

키트에 새 타입을 넣기 전에 물어볼 것: *이 장르의 다른 게임이 이 필드를 그대로 쓸 수 있나?*
"슬롯 2개", "통화 2종", "스탯 이름 고정" 이 나오면 거의 항상 아니다.
