# Ability — 어빌리티 시스템

## 이것은 무엇이고 왜 있나

체력과 마나 같은 숫자 상태, 그 숫자를 바꾸는 효과, 조건을 지나야 발동하는 기술, 그리고 상태를 나타내는 태그는 장르를 가리지 않고 필요합니다.
액션 게임의 화염구, 턴제 RPG 의 독, 오버월드의 버프가 모두 같은 구조입니다. 이 폴더는 그 구조를 언리얼의 Gameplay Ability System(GAS)과 같은 이름과 흐름으로 구현합니다.
장르를 가리지 않으므로 키트가 아니라 기반에 있습니다.

GAS 를 아는 사람은 이름만 보고 바로 쓸 수 있고, 처음 보는 사람은 이 문서와 테스트 게임 `AbilityArena` 로 GAS 의 구조를 배울 수 있습니다.
테스트는 `EngineTest` 의 `AbilitySystemTest`(`Test/EngineTest/GameFramework/Ability/TestAbilitySystem.cpp`)입니다.

## 머릿속 그림

| 여기 | 언리얼 GAS | 하는 일 |
|------|-----------|--------|
| `AbilitySystemComponent` | `UAbilitySystemComponent` | 오브젝트 하나의 어트리뷰트, 걸린 이펙트, 부여된 어빌리티, 태그 개수, 시간 |
| `AttributeSet`, `CombatAttributeSet` | `UAttributeSet` | 이름으로 찾는 숫자 상태(base 와 current), 클램프, 메타 어트리뷰트 훅 |
| `GameplayEffectDef` | `UGameplayEffect` | 즉시, 지속, 무한 효과와 주기, 스택, 모디파이어, 실행 계산 |
| `GameplayEffectSpec` | `FGameplayEffectSpec` | 적용 한 번의 레벨, SetByCaller 값, 쏜 쪽 어트리뷰트 스냅샷 |
| `IGameplayEffectExecution` | `UGameplayEffectExecutionCalculation` | 여러 어트리뷰트를 읽는 공식(`DamageExecution` 은 방어 감쇠 피해) |
| `GameplayAbility` | `UGameplayAbility` | 발동 조건, 커밋(비용과 쿨다운), 끝, 취소, 입력, 트리거 |
| `AbilityTask` | `UAbilityTask_*` | 어빌리티가 도는 동안 기다리는 일(`waitDelay`, `waitGameplayEvent`) |
| `GameplayEventData` | `FGameplayEventData` | 태그 이벤트. 트리거와 대기 작업이 받습니다 |
| `GameplayCueEvent` | Gameplay Cue | 연출 전용 알림(델리게이트와 "game" 채널) |
| `AbilityCatalog` | 데이터 에셋, `ULyraAbilitySet` | id 로 정의를 찾고, 클래스 이름으로 객체를 만들고, XML 을 읽습니다 |

**어트리뷰트.** 체력처럼 이름 붙은 숫자입니다. 기본값(base)과, 걸린 효과를 모두 더한 현재값(current) 두 개를 가집니다.

**이펙트.** 어트리뷰트를 바꾸거나 태그를 주는 데이터입니다. 즉시 이펙트는 base 를 한 번 바꾸고, 지속 이펙트는 걸려 있는 동안 current 에만 더해집니다.

**어빌리티.** 발동 조건을 확인하고, 비용과 쿨다운을 치르고(커밋), 일을 한 뒤 끝나는 행동입니다. 코드로 쓰거나, 이펙트만 거는 경우는 데이터만으로 씁니다.

**태그.** `TagID` 와 `TagContainer`(점 계층)를 그대로 씁니다. "A" 는 "A.B" 에 맞습니다.

## 따라 해 보기 — 화염구 어빌리티 하나

`AbilityArena` 의 플레이어가 K 키로 쏘는 화염구를 따라갑니다.

### 1단계 — 카탈로그 준비

게임 인스턴스가 카탈로그를 가지고, 코드로 쓴 어빌리티 클래스를 이름으로 등록한 뒤 데이터를 읽어 게임 서비스로 등록합니다.
클래스를 먼저 등록하는 것은, 데이터가 `class="Projectile"` 처럼 이름으로 클래스를 가리키기 때문입니다.

<!-- snippet: Source/Games/AbilityArena/ArenaAbilities.cpp · AbilityArenaGame.cpp 의 등록과 읽기 — 5b U7 에서 대조 -->
```cpp
catalog.registerAbilityClass<ArenaProjectileAbility>( "Projectile" );
(void)catalog.loadFromResource( "game/abilityarena/data/abilities.xml" );
game::bindLocalService<AbilityCatalog>( &catalog );
```

### 2단계 — 오브젝트에 어빌리티 시스템 붙이기

유닛 오브젝트에 `AbilitySystemComponent` 를 붙이고 어빌리티 세트를 줍니다. PROPERTY `_abilitySetId` 를 정해 두면 플레이를 시작할 때 받고, 코드에서는 `grantAbilitySet( "Player" )` 를 부릅니다.
세트는 어트리뷰트의 시작값과 어빌리티, 입력 번호, 시작 이펙트를 함께 줍니다.

### 3단계 — 입력을 넘기기

어빌리티 시스템은 키보드를 읽지 않습니다. 유닛의 몸 컴포넌트가 폰의 의도 버튼을 보고 입력 번호를 넘깁니다.
`ArenaUnitComponent::pressAbilityButtons` 가 이렇게 합니다.

<!-- snippet: Source/Games/AbilityArena/ArenaUnitComponent.cpp 의 pressAbilityButtons — 5b U7 에서 대조 -->
```cpp
if ( intent.wasTriggered( slot ) )
    abilitySystem.abilityInputPressed( inputId );
if ( bWasDown && intent.isDown( slot ) == false )
    abilitySystem.abilityInputReleased( inputId );
```

플레이어든 AI 든 같은 버튼을 누르므로 어빌리티 코드는 누가 조종하는지 모릅니다. 폰과 의도는 [GameFramework](../../README.md)의 "조종 — 폰, 조종자, 의도"를 보세요.

### 4단계 — 어빌리티 클래스

어빌리티는 언리얼과 같은 흐름입니다. 커밋하고, 일하고, 끝냅니다.

<!-- snippet: 화염구 어빌리티 — 5b U7 에서 문서 예시 테스트로 대조 -->
```cpp
class MyFireballAbility final : public GameplayAbility
{
public:
    void activateAbility( const GameplayEventData* pTriggerEvent ) override
    {
        if ( commitAbility() == false ) // 비용과 쿨다운. 그사이 못 치르게 됐으면 실패
        {
            cancelAbility();
            return;
        }
        GameplayEffectSpec spec = makeOutgoingSpec( getNameParameter( "damageEffect" ) );
        spec.setSetByCallerMagnitude( "Damage", getParameter( "damage" ) );
        (void)applyEffectSpecToTarget( spec, findTarget() );
        endAbility();
    }
};
```

시간이 걸리면 `waitDelay( 초, SW_DELEGATE_METHOD( Delegate<void()>, &MyAbility::onDone, this ) )` 로 기다렸다가 끝냅니다.
숫자(`damage`)는 코드에 두지 않고 데이터의 `<Param>` 으로 받습니다. 그래서 같은 클래스를 플레이어와 적이 다른 값으로 씁니다.
이펙트만 거는 어빌리티(회복, 버프, 패시브, 가시)는 클래스를 만들지 않고 기본 클래스 `"ApplyEffects"`(`ApplyEffectsAbility`)를 데이터에 적습니다.

반응은 델리게이트(`registerAttributeChanged`)나 "game" 채널 이벤트(`AbilityOwnerDiedEvent`)로 받습니다.

## 작동 원리

### 계산 규칙

**집계 공식**은 `((base + ΣAdd) × (1 + Σ(Multiply − 1))) ÷ (1 + Σ(Divide − 1))` 이고, `Override` 가 있으면 마지막 것이 이깁니다.
곱은 보너스를 더하는 방식이라 ×1.5 와 ×1.2 가 함께 걸리면 ×1.7 입니다. 즉시 이펙트와 주기 실행은 base 를 바꾸고, 지속 이펙트와 무한 이펙트는 걸려 있는 동안 current 에만 더해집니다.

**주기 이펙트**의 모디파이어는 주기마다 base 에 씁니다(도트, 리젠). 한 프레임이 주기 여럿을 넘어도 순서대로 모두 실행하고, 만료 뒤의 주기는 실행하지 않습니다.

**스택**(`AggregateByTarget`)은 대상마다 인스턴스 하나에 개수만 올리고, 모디파이어는 개수만큼 곱해집니다. 만료는 통째로 하거나 하나씩 합니다.

**태그는 개수로 셉니다.** 같은 태그를 주는 이펙트 둘이 모두 풀려야 태그가 사라집니다. 태그 알림은 생길 때(0에서 1)와 사라질 때(1에서 0)만 보냅니다.

**발동 판정 순서**는 다시 걸기, 막힌 어빌리티 태그, 필요한 태그, 막는 태그, 쿨다운, 비용, `canActivateAbility` 입니다.
실패 이유는 `AbilityActivationResult` 로 돌려주므로, UI 가 버튼을 끌 때 `canActivateAbility` 를 그대로 씁니다.

**크기는 걸리는 순간의 값입니다.** 지속 모디파이어와 `AttributeBased` 크기는 걸리는 순간 값을 씁니다. 쏜 쪽 어트리뷰트는 스펙을 만든 순간 통째로 찍어 둡니다(`GameplayEffectSpec::_mapSourceAttribute`).
그래서 투사체가 날아가는 동안 쏜 쪽이 쓰러져도 공식은 그 값을 씁니다.

**메타 어트리뷰트.** 피해 공식은 체력을 직접 깎지 않고 `IncomingDamage` 에 더합니다. `CombatAttributeSet` 이 받자마자 체력으로 옮기고 0 으로 되돌립니다.
그 한 곳에서 맞음 이벤트(`Event.Hit`)와 쓰러짐(`State.Dead`, `Event.Death`, 처음 한 번의 `AbilityOwnerDiedEvent`)을 보냅니다.

**언리얼과 다른 점.** 네트워크 예측과 복제는 없습니다. 커브 테이블 대신 레벨 1 값에 레벨당 값을 더하는 직선(`ScalableFloat`)을 씁니다.
인스턴싱은 스펙마다 인스턴스 하나인 `InstancedPerActor` 하나뿐입니다.

### 시간과 스레드

컴포넌트 틱이 `advanceTime( 프레임 시간 )` 을 부릅니다. **턴제 게임**은 틱을 끄고(`setCanEverTick( false )`) 턴마다 `advanceTime( 1 )` 을 부르면 지속 시간과 주기와 쿨다운이 턴 단위가 됩니다.
테스트도 이 방법으로 시간을 돌립니다.

컴포넌트 틱은 오브젝트마다 병렬 워커에서 돕니다. 자기 상태는 그 자리에서 바꾸고, 다른 오브젝트에 이펙트를 걸거나 이벤트를 보내면 틱 직후로 미룹니다.
그래서 틱 안에서 다른 오브젝트에 건 적용은 무효 핸들을 돌려줍니다. 데미지 숫자 오브젝트도 틱 직후에 만듭니다.
델리게이트는 그 변경이 일어난 스레드에서 불립니다. "game" 채널은 버스 스레드가 아니면 큐로 갑니다(`GameEventUtil`).

### 체력 UI

어빌리티 시스템은 체력 원천(`HealthSourceComponent`)입니다. 체력이나 최대 체력이 바뀔 때마다 같은 오브젝트의 `HealthListenerComponent` 에 비율을 알리고, 체력이 0 이면 쓰러짐으로 알립니다.
HP 바(`HealthBarComponent`)가 그 리스너를 상속하므로, 어빌리티 시스템은 바를 모릅니다. `setShowDamageNumbers( true )` 면 체력이 깎일 때 데미지 숫자를 띄웁니다.
체력 어트리뷰트의 이름은 `setHealthAttributes` 로 바꿉니다(기본 `Health`, `MaxHealth`).

### 핫 리로드와 저장

게임 모듈이 준 어빌리티와 어트리뷰트 세트, 실행 계산은 vtable 이 그 모듈 안에 있습니다. 컴포넌트마다 `IModuleUnloadListener` 가 있어서 모듈을 언로드하기 전에 그 모듈의 것을 회수합니다.
다시 로드된 뒤에는 PROPERTY `_abilitySetId` 가 세트를 다시 줍니다. 코드로 준 것은 게임이 다시 줍니다.

저장되는 것은 PROPERTY(세트 id, 체력 어트리뷰트 이름, 데미지 숫자 설정)뿐입니다. 어트리뷰트 값과 걸린 이펙트는 저장하지 않습니다. 저장이 필요한 값은 게임이 상태 데이터에 담습니다.

### 카탈로그 XML

루트 `<AbilityCatalog>` 아래 세 종류를 아무 순서로 적습니다. 읽기는 이펙트, 어빌리티, 세트 순이라 앞에서 뒤를 가리켜도 됩니다.
열거자는 이름 그대로 적고 대소문자는 가리지 않습니다. 모르는 이름이나 빠진 값은 경고하고 그 항목만 건너뜁니다. 같은 id 를 다시 읽으면 뒤의 것이 이깁니다.

```xml
<AbilityCatalog>
  <GameplayEffect id="GE_Burn" duration="HasDuration" seconds="3" secondsPerLevel="0" period="1" executeOnApplication="false"
                  stacking="AggregateByTarget" stackLimit="3" refreshOnStack="true" stackExpiration="ClearEntireStack">
    <Modifier attribute="IncomingDamage" op="Add" magnitude="3" perLevel="1"/>                  <!-- ScalableFloat(기본) -->
    <Modifier attribute="Armor" op="Add" source="AttributeBased" backing="Armor" from="Target" coefficient="-0.1" pre="0" post="0"/>
    <Modifier attribute="IncomingDamage" op="Add" source="SetByCaller" name="Damage"/>
    <Execution class="Damage" setByCaller="Damage" attackPowerCoefficient="1" base="0"/>
    <AssetTag tag="Effect.Damage.Fire"/>           <!-- 이 이펙트를 설명(정화가 고른다) -->
    <GrantedTag tag="State.Burning"/>              <!-- 걸린 동안 대상에게 -->
    <RequiredTag tag="..."/> <BlockedTag tag="State.Immune.Fire"/>   <!-- 대상 조건(면역) -->
    <RemoveEffectsWithTag tag="State.Frozen"/>     <!-- 걸리는 순간 지울 것 -->
    <Cue tag="GameplayCue.Burning"/>
  </GameplayEffect>

  <Ability id="GA_Fireball" class="Fireball" cost="GE_Cost_Mana15" cooldown="GE_Cooldown_Fireball" retrigger="false" activateOnGranted="false">
    <AbilityTag tag="Ability.Skill.Fireball"/>     <!-- 이 어빌리티를 설명 -->
    <CancelTag tag="Ability.Skill.Channel"/>       <!-- 발동하며 취소할 것 -->
    <BlockTag tag="Ability.Movement"/>             <!-- 도는 동안 막을 것 -->
    <OwnedTag tag="State.Casting"/>                <!-- 도는 동안 주인에게 -->
    <RequiredTag tag="..."/> <BlockedTag tag="State.Dead"/>   <!-- 주인 조건 -->
    <TriggerTag tag="Event.Hit"/>                  <!-- 이 이벤트가 오면 스스로 발동 -->
    <Param name="damage" value="18"/>              <!-- getParameter -->
    <Param name="damageEffect" text="GE_Damage"/>  <!-- getNameParameter -->
  </Ability>

  <AbilitySet id="Player">
    <AttributeSet class="Combat">                  <!-- "Combat", "Generic", 게임이 등록한 이름 -->
      <Attribute name="Health" base="120"/>
    </AttributeSet>
    <AttributeSet class="Generic">
      <Attribute name="Stamina" base="50" min="0" max="100"/>
    </AttributeSet>
    <Ability id="GA_Fireball" level="1" input="2"/>
    <Effect id="GE_ManaRegen" level="1"/>          <!-- 시작 이펙트 -->
    <Tag tag="Team.Player"/>                       <!-- 시작 태그 -->
  </AbilitySet>
</AbilityCatalog>
```

세트의 어트리뷰트 값은 세트를 컴포넌트에 붙이기 전에 정합니다. 그래서 "체력은 최대 체력 이하" 같은 훅이 적은 순서에 따라 값을 자르지 않습니다.

## 확장하는 법

### 새 어빌리티 클래스

1. `GameplayAbility` 를 상속하고 `activateAbility` 를 구현합니다. 숫자는 `getParameter` 로, 이펙트 id 는 `getNameParameter` 로 받습니다.
2. 게임 인스턴스에서 카탈로그를 읽기 전에 `registerAbilityClass<T>( "이름" )` 으로 등록합니다.
3. 데이터의 `<Ability class="이름">` 으로 씁니다.

### 새 실행 계산과 어트리뷰트 세트

여러 어트리뷰트를 읽는 공식은 `IGameplayEffectExecution` 을 구현해 `registerExecutionClass` 로 등록하고, 데이터의 `<Execution class>` 로 씁니다.
어트리뷰트의 개수와 이름은 코드가 정하지 않습니다. 데이터의 `<AttributeSet>` 이 어트리뷰트를 더하고, 클램프 같은 규칙이 필요한 세트만 파생 클래스를 만들어 `registerAttributeSetClass` 로 등록합니다.
이름 범위만 필요하면 `Generic` 세트를 그대로 씁니다.

## 함정과 주의

- **다른 오브젝트로 가는 적용과 이벤트는 `applyGameplayEffectSpecToTarget` 과 `sendGameplayEventToTarget` 으로만 보냅니다.** 틱 중이면 틱 직후로 미뤄 줍니다.
  틱 안에서 다른 오브젝트의 `applyGameplayEffectSpecToSelf` 를 직접 부르면 미루지 않으므로 데이터 경쟁이 생깁니다.
- **콜백 안에서 이펙트를 지워도 됩니다.** 걸린 이펙트와 스펙은 `unique_ptr` 목록이고, 콜백 도중의 지우기는 표시만 해 두었다가 `ScopedListLock` 이 풀릴 때 지웁니다.
  그래서 콜백이 목록을 늘리거나 줄여도 순회 중인 포인터가 유효합니다.
- **카탈로그를 컴포넌트에 `setCatalog` 로 박아 두지 않습니다.** 카탈로그는 게임 모듈과 수명이 같아서, 핫 리로드 뒤에는 예전 카탈로그를 가리키게 됩니다. 게임 서비스로 등록하는 쪽이 안전합니다.

## 더 볼 곳

- [AbilityArena](../../../Games/AbilityArena/README.md) — 이 시스템을 쓰는 테스트 게임
- [GameFramework](../../README.md) — 폰과 의도, 체력 리스너
- `Resource/game/abilityarena/data/abilities.xml` — 실제 카탈로그

| 파일 | 내용 |
|------|------|
| `AbilitySystemComponent.h` | 컴포넌트와 공개 API |
| `GameplayAbility.h` | 어빌리티 베이스와 `ApplyEffectsAbility` |
| `GameplayEffect.h` | 이펙트 정의와 스펙, 실행 계산 |
| `AttributeSet.h`, `CombatAttributeSet.h` | 어트리뷰트 세트 |
| `AbilityCatalog.h` | 카탈로그와 클래스 등록 |
