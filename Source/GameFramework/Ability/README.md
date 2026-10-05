# Ability — 언리얼 Gameplay Ability System(GAS) 같은 어빌리티 시스템

`GameFramework` 타깃의 일부입니다(키트가 아님 — 장르를 가리지 않습니다). 액션 · 턴제 · 오버월드 어느 게임이든
"숫자 상태 + 그것을 바꾸는 효과 + 조건을 지나 발동하는 행동 + 태그" 가 필요하면 이것을 씁니다.
시험: `EngineTest` 의 `AbilitySystemTest`(`Test/EngineTest/GameFramework/Ability/TestAbilitySystem.cpp`). 실제로 쓰는 게임: `Source/Games/AbilityArena`.

## 구성 — 언리얼 이름과 1:1

| 여기 | 언리얼 GAS | 하는 일 |
|------|-----------|--------|
| `AbilitySystemComponent` | `UAbilitySystemComponent` | 오브젝트 하나의 어트리뷰트 · 걸린 이펙트 · 부여된 어빌리티 · 태그 개수 · 시간 |
| `AttributeSet` · `CombatAttributeSet` | `UAttributeSet` | 이름으로 찾는 숫자 상태(base / current)와 클램프 · 메타 어트리뷰트 훅 |
| `GameplayEffectDef` | `UGameplayEffect` | 즉시 · 지속 · 무한, 주기, 스택, 모디파이어, 실행 계산, 태그 다섯 가지, 큐 |
| `GameplayEffectSpec` | `FGameplayEffectSpec` | 적용 한 번 — 레벨 · 컨텍스트 · SetByCaller · **쏜 쪽 어트리뷰트 스냅샷** |
| `IGameplayEffectExecution` · `DamageExecution` | `UGameplayEffectExecutionCalculation` | 여러 어트리뷰트를 읽는 공식(방어 감쇠 피해) |
| `GameplayAbility` · `GameplayAbilityConfig` | `UGameplayAbility` | 발동 조건 · 커밋(비용 · 쿨다운) · 끝 · 취소 · 입력 · 트리거 |
| `AbilityTask` · `waitDelay` · `waitGameplayEvent` | `UAbilityTask_*` | 어빌리티가 도는 동안 기다리는 일 |
| `GameplayEventData` · `handleGameplayEvent` | `FGameplayEventData` · `SendGameplayEventToActor` | 태그 이벤트 — 트리거 · 대기 작업이 받는다 |
| `GameplayCueEvent` | Gameplay Cue | 연출 전용 알림(델리게이트 + "game" 채널) |
| `AbilityCatalog` | (데이터 에셋 · `ULyraAbilitySet`) | id → 정의, 클래스 이름 → 만드는 함수, XML 읽기 |

태그는 엔진의 `TagID` · `TagContainer`(점 계층, "A" 는 "A.B" 에 맞는다)를 그대로 씁니다.

## 가장 짧은 사용법

```cpp
// 1) 게임이 카탈로그를 들고 게임 서비스로 건다 — 클래스 등록 → 데이터 읽기 순서.
_catalog.registerAbilityClass<MyFireballAbility>( "Fireball" );
(void)_catalog.loadFromResource( "game/mygame/data/abilities.xml" );
game::bindLocalService<AbilityCatalog>( &_catalog );

// 2) 오브젝트에 컴포넌트를 붙이고 세트를 준다(또는 PROPERTY `_abilitySetId` 를 정하면 플레이 시작에 받는다).
AbilitySystemComponent* pAbilitySystem = pObject->addComponent<AbilitySystemComponent>();
(void)pAbilitySystem->grantAbilitySet( "Player" );

// 3) 입력을 넘긴다 — 세트가 준 입력 번호로 발동한다.
if ( input.wasKeyPressed( Key::K ) )
    pAbilitySystem->abilityInputPressed( 2 );

// 4) 반응한다 — 델리게이트 또는 "game" 채널.
pAbilitySystem->registerAttributeChanged( CombatAttributes::health(), onHealthChanged );
dispatcher.subscribe<AbilityOwnerDiedEvent>( gameEventChannel(), onDied );
```

어빌리티 하나는 이렇게 생겼습니다(언리얼과 같은 흐름 — 커밋하고, 일하고, 끝낸다):

```cpp
class MyFireballAbility final : public GameplayAbility
{
public:
    void activateAbility( const GameplayEventData* pTriggerEvent ) override
    {
        if ( commitAbility() == false ) // 비용 · 쿨다운 — 그 사이 못 치르게 됐으면 실패
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
코드 없이 이펙트만 거는 어빌리티는 기본 클래스 `"ApplyEffects"`(`ApplyEffectsAbility`)로 데이터만 적습니다(회복 · 버프 · 패시브 · 가시).

## 규칙 — 언리얼과 같은 것, 다른 것

- **집계 공식**: `((base + ΣAdd) × (1 + Σ(Multiply − 1))) ÷ (1 + Σ(Divide − 1))`, `Override` 가 있으면 마지막이 이깁니다. 곱은 보너스를 **더합니다**
  (×1.5 와 ×1.2 는 ×1.7). 즉시 이펙트 · 주기 실행은 base 를 바꾸고, 지속 · 무한 이펙트는 걸려 있는 동안 current 에만 얹습니다.
- **주기 이펙트**의 모디파이어는 주기마다 base 에 씁니다(도트 · 리젠) — 걸려 있는 동안 current 에 얹지 않습니다. 한 프레임이 주기 여럿을 넘어도
  순서대로 실행하고, 만료 뒤의 주기는 실행하지 않습니다.
- **스택**(`AggregateByTarget`): 대상에 인스턴스 하나, 상한까지 개수가 오르고 모디파이어는 개수만큼 곱해집니다. 만료는 통째로 또는 하나씩.
- **태그는 개수로 셉니다**: 같은 태그를 주는 이펙트 둘이 다 풀려야 태그가 사라집니다. 태그 알림은 생길 때(0→1)와 사라질 때(1→0)만.
- **발동 순서**: 다시 걸기 → 막힌 어빌리티 태그 → 필요한 태그 → 막는 태그 → 쿨다운 → 비용 → `canActivateAbility`. 실패 이유는
  `AbilityActivationResult` 로 돌려줍니다(UI 가 버튼을 끌 때 `canActivateAbility` 를 그대로 씁니다).
- **크기 스냅샷**: 지속 모디파이어 · `AttributeBased` 크기는 **걸리는 순간** 값을 씁니다. 쏜 쪽 어트리뷰트는 스펙을 만든 순간 통째로 찍습니다
  (`GameplayEffectSpec::_mapSourceAttribute`) — 투사체가 나는 동안 쏜 쪽이 쓰러져도 공식이 그 값을 씁니다.
- **메타 어트리뷰트**: 피해 공식은 체력을 직접 깎지 않고 `IncomingDamage` 에 더합니다. `CombatAttributeSet` 이 받자마자 체력으로 옮기고 0 으로 되돌리며,
  그 자리 하나에서 `Event.Hit`(맞을 때마다) · `State.Dead` · `Event.Death` · `AbilityOwnerDiedEvent`(처음 한 번)를 냅니다.
- **언리얼에 없는 것**: 네트워크 예측 · 복제는 없습니다(단일 프로세스). 커브 테이블 대신 `ScalableFloat` 직선(레벨 1 값 + 레벨당 값)입니다.
  인스턴싱은 `InstancedPerActor` 하나뿐입니다(스펙마다 인스턴스 하나).

## 시간 · 스레드

- 컴포넌트 틱이 `advanceTime( 프레임 시간 )` 을 부릅니다. **턴제**는 틱을 끄고(`setCanEverTick( false )`) 턴마다 `advanceTime( 1 )` 을 부르면
  지속 · 주기 · 쿨다운이 턴 단위가 됩니다. 시험도 이 길로 시간을 돌립니다.
- 컴포넌트 틱은 **오브젝트마다 병렬 워커**에서 돕니다. 자기 상태(시간 · 자기 이펙트 · 자기 어빌리티)는 그 자리에서 바꾸고, **다른 오브젝트에**
  이펙트를 걸거나 이벤트를 보내면(`applyGameplayEffectSpecToTarget` · `sendGameplayEventToTarget`) 틱 직후로 미룹니다(`UnitStatsComponent::takeDamage`
  와 같은 규칙). 그래서 틱 안에서 다른 오브젝트에 건 적용은 무효 핸들을 돌려줍니다. 피해 숫자 오브젝트도 틱 직후에 만듭니다.
- 다른 오브젝트의 틱 안에서 남의 컴포넌트를 **직접** 부르지 마십시오(`pOther->applyGameplayEffectSpecToSelf`) — 그 길은 미루지 않습니다.
- 델리게이트는 그 변경이 일어난 스레드에서 불립니다(틱 중이면 그 오브젝트의 워커). "game" 채널은 버스 스레드가 아니면 큐로 갑니다(`GameEventUtil`).

## 핫 리로드 · 저장

- 게임 모듈이 준 어빌리티 · 어트리뷰트 묶음 · 실행 계산은 vtable 이 그 모듈 안에 있습니다. 컴포넌트마다 `IModuleUnloadListener` 보유자가 있어 모듈을
  내리기 **전에** 그 범위의 것을 거둡니다(코드가 아직 있을 때). 다시 올라온 뒤에는 PROPERTY `_abilitySetId` 가 세트를 다시 줍니다 — 코드로 준
  것은 게임이 다시 줍니다.
- 카탈로그는 게임 모듈과 수명이 같습니다(팩토리가 게임 코드). 컴포넌트에 `setCatalog` 로 박아 두기보다 게임 서비스로 거는 쪽이 리로드에 안전합니다.
- 저장되는 것은 PROPERTY(세트 id · 체력 어트리뷰트 이름 · 피해 숫자 설정)뿐입니다. 어트리뷰트 값 · 걸린 이펙트는 저장하지 않습니다 — 세이브가 필요한
  값은 게임의 `SaveGame` 이 어트리뷰트를 읽어 담습니다.

## 연결된 UI

체력 · 최대 체력이 바뀔 때마다 같은 오브젝트의 `HealthListenerComponent`(HP 바 `HealthBarComponent` 가 상속한다)에 비율을 알립니다 — 어빌리티 시스템은 바를 모릅니다. `setShowDamageNumbers( true )` 면 체력이 깎일 때
`DamageNumberComponent` 숫자를 띄웁니다. 어트리뷰트 이름은 `setHealthAttributes` 로 바꿉니다(기본 `Health` · `MaxHealth`).

## 카탈로그 XML

루트 `<AbilityCatalog>` 아래 세 종류를 아무 순서로 적습니다(읽기는 이펙트 → 어빌리티 → 세트 순이라 앞에서 뒤를 가리켜도 됩니다). 열거자는 이름 그대로
(대소문자 무시). 모르는 이름 · 빠진 값은 경고하고 그 항목만 건너뜁니다.

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
    <AttributeSet class="Combat">                  <!-- "Combat" · "Generic" · 게임이 등록한 이름 -->
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

세트의 어트리뷰트 값은 묶음을 컴포넌트에 붙이기 **전에** 정합니다 — 그래서 "체력 ≤ 최대 체력" 같은 훅이 적은 순서에 따라 값을 자르지 않습니다.
