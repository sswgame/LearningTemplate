# AbilityArena — 어빌리티 시스템 테스트 게임

## 이 게임으로 무엇을 배우나

탑다운 웨이브 아레나입니다. 기반의 어빌리티 시스템(`GameFramework/Base/Gameplay/Ability`, 언리얼 GAS 와 같은 구조)을 실제 게임 흐름에서 씁니다.
키트는 링크하지 않습니다. 어빌리티 시스템이 기반에 있기 때문입니다.

- 코드로 쓴 어빌리티와 데이터만으로 만든 어빌리티를 함께 쓰는 방법
- 플레이어와 적과 자동 전투가 **같은 몸**을 쓰고 조종자만 다른 구조
- 어빌리티가 게임 서비스 없이 핸들로 디렉터를 찾는 방법
- 체력 알림을 받는 HP 바와 데미지 숫자

유닛과 바닥과 벽과 소품은 Kenney Mini Dungeon 모델(`Resource/game/abilityarena/credits.md`)이고, 투사체만 내장 구입니다.

## 빌드와 실행

```powershell
cmake --preset Ninja-Debug-AbilityArena
cmake --build --preset Ninja-Debug-AbilityArena
cd build/Ninja-Debug-AbilityArena/Bin
./App.exe -dx12                          # 에디터 없이. 월드가 처음부터 플레이 중이다
./App.exe -dx12 -gv_arenaAutoPlay=1      # 자동 전투 AI 조종자가 플레이어 폰을 잡는다
./App.exe -dx12 -scenario=game/abilityarena/automation/control.scenario.xml
./App.exe -dx12 -EnableEditor "-gv_editorStartupScene=game/abilityarena/maps/arena.scene.xml"
```

에디터로 띄우면 Play 를 눌러야 디렉터가 플레이어와 첫 웨이브를 만들고, Stop 은 플레이 전 씬으로 돌아갑니다.

`control.scenario.xml` 은 가상 키로 플레이어를 오른쪽으로 걷게 하고(D 를 반 초, 3m 남짓) 화염구를 한 번 쏜 뒤(K),
자동 플레이를 켜서 자동 전투 AI 가 폰을 잡고 적을 쓰러뜨리는지, 끄면 플레이어 조종자가 되찾는지 봅니다.
탐침은 `Arena.PlayerX`, `Arena.PlayerZ`, `Arena.PlayerShotCount`, `Arena.KillCount`, `Arena.PlayerControllerKind`(0 플레이어, 1 AI)입니다.

## 조작

| 키 | 입력 번호 | 어빌리티 | 보여 주는 것 |
|----|----------|---------|-------------|
| WASD, 방향키 | | 이동 | 이동 속도는 `MoveSpeed` 어트리뷰트의 current 값이라 대시 버프가 그대로 반영됩니다 |
| J, Space | 1 | `GA_Melee`(`MeleeStrike`) | 쿨다운, SetByCaller 피해, 방어 감쇠(`DamageExecution`) |
| K, 2 | 2 | `GA_Fireball`(`Projectile`) | 마나 비용, 쏜 순간의 스냅샷 스펙, 화상(주기, 스택 3, 시간 갱신) |
| L, 3 | 3 | `GA_Heal`(`ApplyEffects`) | 코드 없는 데이터 어빌리티, 레벨 스케일, 정화 |
| LeftShift, 4 | 4 | `GA_Dash`(`Dash`) | 시간이 걸리는 어빌리티(`waitDelay`), 무적 태그, 공격 막기 |

키 배치는 `data/arena.input.xml` 에 있습니다.

적은 둘입니다. **Grunt**(붉은 오크)는 다가와 때리고, 맞으면 가시(`GA_Thorns`)로 때린 쪽에 고정 피해를 줍니다. 가시는 피격 트리거와 대상 이펙트만으로 된 데이터 어빌리티입니다.
**Caster**(보랏빛 오크)는 거리를 두고 화염탄을 쏩니다. 웨이브가 오를수록 수가 늘고 체력과 공격력이 오릅니다. 플레이어는 쓰러지면 잠시 뒤 가운데에 다시 섭니다.
HP 바는 어빌리티 시스템의 체력 알림을 받는 `HealthBarComponent` 이고, 피해 숫자는 `DamageNumberComponent` 입니다.

## 구조

아레나는 씬 하나(`Resource/game/abilityarena/maps/arena.scene.xml`)입니다.

| 무엇 | 어디 |
|------|------|
| 바닥, 벽, 기둥, 깃발, 상자 같은 소품, 함정, 해, 카메라, 디렉터 | 씬 |
| 플레이어, Grunt, Caster, 투사체 | 프리팹(`prefabs/`). 디렉터가 런타임에 스폰합니다 |
| 적 AI 와 자동 전투 AI 조종자 | 프리팹 `gruntai`, `casterai`, `autobattleai`. 디렉터가 만들어 폰에 빙의시킵니다 |
| 어빌리티, 이펙트, 어트리뷰트, 태그 | `AbilitySystemComponent` 와 데이터 `data/abilities.xml` |
| 웨이브, 쓰러뜨린 수, 플레이어 다시 세우기, 투사체 스폰, 로그 | `ArenaDirectorComponent` |
| 유닛 하나의 몸(폰) | `PawnComponent`(버튼 `Arena.Melee`, `Arena.Fireball`, `Arena.Heal`, `Arena.Dash`, 이동 `Arena.Move`)과 `ArenaUnitComponent` |
| 유닛을 잡는 조종자 | 플레이어는 `PlayerControllerComponent`(자동 빙의 `Player0`), 적은 `ArenaEnemyAIComponent`, 자동 전투는 `ArenaAutoBattleAIComponent` |
| 투사체 하나 | `ArenaProjectileComponent`. 자기를 옮기고 처음 닿은 적대 유닛에 이펙트를 겁니다 |
| 카메라 | `ArenaCameraComponent`. 플레이어 메시의 이번 프레임 위치를 위에서 봅니다 |
| 머티리얼 | 팔레트 `materials/palette.material`. 편 색과 투사체 색은 디렉터의 PROPERTY 이고, 디렉터가 머티리얼 인스턴스를 만들어 나눠 씁니다 |

**조종.** 유닛은 폰이고 의도(`ControlIntent`)만 읽습니다. 플레이어와 적과 자동 전투가 같은 몸(`ArenaUnitComponent`)으로 움직이고, 다른 것은 의도를 내는 조종자뿐입니다.
몸은 의도 버튼이 발동하면 `abilityInputPressed( 입력 번호 )` 를 부릅니다(`pressAbilityButtons`).

- 적은 디렉터가 스폰할 때 종류의 AI 프리팹을 함께 만들어 빙의시킵니다(언리얼 GameMode 가 AIController 를 만드는 것과 같습니다). 쓰러져 정리할 때 그 조종자도 정리합니다.
- 자동 플레이 스위치(`-gv_arenaAutoPlay`, 에디터 툴바)를 켜면 디렉터가 틱 뒤에 자동 전투 AI 를 플레이어 폰에 빙의시키고, 끄면 플레이어 조종자가 되찾습니다.
  쓰러졌다 다시 선 플레이어도 같은 규칙으로 잡힙니다.
- 플레이어 유닛은 움직이는 쪽(`MoveDirection`)을 보고, 적은 조종 요(`ControlYaw`)를 봅니다. AI 가 대상을 초점으로 돌리므로 물러나면서도 대상을 봅니다.
- AI 의 판단은 틱 전이라 대상의 이번 프레임 시작 위치를 봅니다. 언리얼 AIController 처럼, 쫓는 쪽은 대상의 이번 걸음을 모릅니다.

**틱.** 디렉터는 `PrePhysics` 에서 쓰러짐, 웨이브, 다시 세우기를 돌리고 이번 프레임의 유닛 모습(`ArenaUnitView`)을 적습니다.
유닛과 투사체는 기본 그룹(`DuringPhysics`)에서 그것을 읽기만 하고(목록은 `data()` 로) 자기 오브젝트에만 씁니다.
유닛은 같은 오브젝트의 어빌리티 시스템과 같은 워커에서 돌기 때문에, 버튼이 발동하면 어빌리티가 그 자리에서 발동합니다. 다른 유닛에 거는 이펙트는 어빌리티 시스템이 틱 뒤로 미룹니다.
카메라는 `PostUpdate` 라 물리 뒤의 플레이어 메시 위치를 직접 읽습니다. 스폰 요청과 효과음과 투사체 발사는 모두 틱 뒤에 처리합니다.

**어빌리티가 디렉터를 찾는 방법.** 어빌리티는 게임 서비스를 쓰지 않고 `ArenaDirectorComponent::findForUnit` 으로 디렉터를 찾습니다.
같은 오브젝트의 `ArenaUnitComponent` 가 가진 디렉터 핸들(PROPERTY)로 씬에서 찾으므로, 핫 리로드가 디렉터를 다시 만들어도(같은 id) 이어집니다.
게임 서비스는 어빌리티 카탈로그 하나뿐이고, 게임 인스턴스가 시작할 때마다 다시 등록합니다.

**핫 리로드와 상태 저장.** 웨이브와 쓰러뜨린 수가 디렉터의 `writeState` 로 실립니다. 다시 만든 디렉터는 진행을 받아 플레이어와 같은 웨이브를 새로 만듭니다.
유닛의 체력과 쿨다운은 새로 시작합니다. 손으로 확인할 때는 `-gv_reloadGameAtFrame=N` 으로 리로드를 겁니다.

## 데이터

- `Resource/game/abilityarena/data/abilities.xml` 에 피해, 쿨다운, 비용, 체력 같은 밸런스 값이 모두 있습니다. 코드를 고치지 않고 바꿉니다.
- `ArenaAbilities` 는 코드로 쓴 어빌리티 셋(근접, 투사체, 대시)입니다. 숫자는 모두 데이터(`<Param>`)라서 같은 클래스를 플레이어와 적이 다른 값으로 씁니다.
- AI 거리(AI 프리팹), 아레나 크기, 편 색은 컴포넌트 PROPERTY 입니다.

## 더 볼 곳

- [Ability](../../GameFramework/Base/Gameplay/Ability/README.md) — 어빌리티 시스템과 카탈로그 형식
- [GameFramework](../../GameFramework/README.md) — 폰과 조종자
- [Games](../README.md) — 씬, 프리팹, 디렉터 구조
