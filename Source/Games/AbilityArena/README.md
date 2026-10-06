# AbilityArena — 어빌리티 시스템 시험 게임

`GameFramework/Base/Ability`(언리얼 GAS 와 같은 어빌리티 시스템)를 실제 게임 흐름에서 쓰는 탑다운 웨이브 아레나입니다.
키트를 링크하지 않습니다 — 어빌리티 시스템은 `GameFramework` 기반에 있습니다. 유닛 · 바닥 · 벽 · 소품은 Kenney Mini Dungeon 모델
(`Resource/game/abilityarena/credits.md`)이고, 투사체만 내장 구입니다.

## 빌드 · 실행

```powershell
cmake --preset Ninja-Debug-AbilityArena
cmake --build --preset Ninja-Debug-AbilityArena
cd build/Ninja-Debug-AbilityArena/Bin
./App.exe -dx12                          # 에디터 없이 — 월드가 처음부터 플레이 중이다
./App.exe -dx12 -gv_arenaAutoPlay=1      # 자동 전투 AI 조종자가 플레이어 폰을 쥔다(입력 없이 한 판을 끝까지)
./App.exe -dx12 -scenario=game/abilityarena/automation/control.scenario.xml   # 조종 시나리오(종료 코드 0 = 통과)
./App.exe -dx12 -EnableEditor "-gv_editorStartupScene=game/abilityarena/maps/arena.scene.xml"
```

에디터(`-EnableEditor`)로 띄우면 Play 를 눌러야 디렉터가 플레이어와 첫 웨이브를 세우고, Stop 은 플레이 전 씬으로 돌아갑니다.

## 조작

| 키 | 입력 번호 | 어빌리티 | 보여 주는 것 |
|----|----------|---------|-------------|
| WASD · 방향키 | — | 이동 | 이동 속도는 `MoveSpeed` 어트리뷰트(current) — 대시 버프가 그대로 반영된다 |
| J · Space | 1 | `GA_Melee` (`MeleeStrike`) | 쿨다운, SetByCaller 피해, 방어 감쇠(`DamageExecution`) |
| K · 2 | 2 | `GA_Fireball` (`Projectile`) | 마나 비용, 쏜 순간의 스냅샷 스펙, 화상(주기 · 스택 3 · 시간 갱신) |
| L · 3 | 3 | `GA_Heal` (`ApplyEffects`) | **코드 없는 데이터 어빌리티**, 레벨 스케일, 정화(`RemoveEffectsWithTag`) |
| LeftShift · 4 | 4 | `GA_Dash` (`Dash`) | 시간이 걸리는 어빌리티(`waitDelay`), 이동 속도 ×3, 무적 태그가 피해 이펙트를 막음, 공격 막기(`BlockTag`) |

적: **Grunt**(붉은 오크) — 다가와 때리고, 맞으면 가시(`GA_Thorns`, 피격 트리거 + `targetEffect` — 데이터만)로 때린 쪽에 고정 피해.
**Caster**(보랏빛 오크) — 거리를 두고 화염탄. 웨이브가 오를수록 수가 늘고 체력 · 공격력이 오릅니다. 플레이어는 쓰러지면 잠시 뒤 가운데에 다시 섭니다.
HP 바는 어빌리티 시스템의 체력 알림(`HealthListenerComponent`)을 받는 같은 오브젝트의 `HealthBarComponent` 이고, 피해 숫자는 `DamageNumberComponent` 로 띄웁니다.

## 구조 — 씬 · 프리팹 · 컴포넌트

아레나는 씬 하나(`Resource/game/abilityarena/maps/arena.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)입니다. 본보기는 `ThemeParkTycoon` 이고
레시피는 `Source/Games/README.md` 의 "새 게임 = 씬 + 프리팹 + 디렉터 · 뷰 컴포넌트" 입니다.

| 무엇 | 어디 |
|------|------|
| 바닥 칸 · 벽 · 기둥 · 깃발 · 상자 · 물약 · 동전 · 바위 · 함정 · 해 · 카메라 · 디렉터 | 씬(엔티티) — 에디터에서 옮긴다 |
| 플레이어 · Grunt · Caster · 투사체 | 프리팹(`prefabs/*.prefab.xml`) — 디렉터가 런타임에 스폰한다 |
| 적 AI · 자동 전투 AI 조종자 | 프리팹(`prefabs/gruntai` · `casterai` · `autobattleai.prefab.xml`) — 디렉터가 세워 폰에 빙의시킨다 |
| 어빌리티 · 이펙트 · 어트리뷰트 · 태그 | 프레임워크 `AbilitySystemComponent` + 데이터 `data/abilities.xml` |
| 웨이브 · 쓰러뜨린 수 · 플레이어 다시 세우기 · 투사체 스폰 · 로그 | `ArenaDirectorComponent`(씬에 하나 — 언리얼 GameMode/GameState 자리) |
| 유닛 하나의 몸(폰) | `PawnComponent`(버튼 `Arena.Melee · Fireball · Heal · Dash`, 이동 `Arena.Move`) + `ArenaUnitComponent` — **의도만 읽어** 움직이고 어빌리티 입력 번호를 누른다 |
| 유닛을 쥐는 조종자 | 플레이어: GameFramework `PlayerControllerComponent`(입력 맵 → 의도, 자동 빙의 `Player0`). 적: `ArenaEnemyAiComponent`(Grunt · Caster). 자동 전투: `ArenaAutoBattleAiComponent` |
| 투사체 하나 | `ArenaProjectileComponent` — 자기를 옮기고 처음 닿은 적대 유닛에 이펙트를 건다 |
| 카메라 | `ArenaCameraComponent` — 플레이어 메시의 이번 프레임 자리를 오프셋 위에서 본다 |
| 모습 | 팔레트 머티리얼 `materials/palette.material`(albedoMap = 키트 색 칸 텍스처). 편 색(플레이어 파랑 · Grunt 빨강 · Caster 보라)과 투사체 주황은 디렉터의 PROPERTY 이고, 디렉터가 머티리얼 인스턴스 하나씩을 만들어 나눠 쓴다 |

**조종 — 폰 · 조종자 · 의도.** 유닛은 폰이고 의도(`ControlIntent`)만 읽습니다. 플레이어 · 적 · 자동 전투가 **같은 몸**(`ArenaUnitComponent`)으로 움직이고,
다른 것은 의도를 내는 조종자뿐입니다. 조종 시스템(GameFramework `ControlSystem`)이 틱 전에 게임 스레드에서 조종자마다 의도를 만들어 폰에 넣습니다.
- 적은 디렉터가 세울 때 종류의 AI 프리팹을 함께 세워 쥐게 하고(언리얼 GameMode 가 AIController 를 세우는 자리), 쓰러져 걷을 때 그 조종자도 걷습니다.
- 플레이어 폰은 자동 빙의(`Player0`)로 플레이어 조종자가 쥡니다. **자동 플레이 스위치**(`-gv_arenaAutoPlay` · 에디터 툴바)를 켜면 디렉터가 틱 뒤에 자동 전투
  AI 를 그 폰에 빙의시키고, 끄면 플레이어 조종자가 되찾습니다 — 몸 안에 자동 플레이 분기가 없습니다. 쓰러졌다 다시 선 플레이어도 같은 규칙으로 쥐어진다.
- 바라보는 쪽: 플레이어 유닛은 움직이는 쪽(`MoveDirection`), 적은 조종 요(`ControlYaw` — AI 가 대상을 초점으로 돌린다, 물러나도 대상을 본다).
- AI 의 판단은 틱 전이라 대상의 **이번 프레임 시작** 자리(메시의 월드 자리)를 봅니다 — 언리얼 AIController 와 같이 쫓는 쪽은 대상의 이번 걸음을 모른다.

**틱.** 디렉터는 `TickGroup::PrePhysics` 에서 쓰러짐 · 웨이브 · 다시 세우기를 돌리고 이번 프레임의 유닛 모습(자리 · 편 · 살아 있음, `ArenaUnitView`)을 적습니다.
유닛 · 투사체는 기본 그룹(`DuringPhysics`)에서 그것을 **읽기만** 하고(목록은 `data()` 로) 자기 오브젝트에만 씁니다. 유닛은 같은 오브젝트의 어빌리티
시스템과 같은 워커에서 돌기 때문에 의도 버튼이 발동하면 어빌리티가 그 자리에서 발동하고, 다른 유닛에 거는 이펙트는 어빌리티 시스템이 틱 뒤로 미룹니다. 카메라는
`PostUpdate` 이고 물리 뒤 단계라 플레이어 메시의 이번 프레임 자리를 직접 읽습니다(`getPlayerObject`).

틱 안에서는 오브젝트를 만들 수 없으므로 디렉터는 스폰 요청 · 효과음을 쌓아 두고 `executeOrDeferPostTick` 한 번으로 틱 뒤에 세웁니다.
투사체 발사(어빌리티가 워커에서 부른다)는 틱 뒤로 미룬 일 하나로 세웁니다.

**어빌리티가 디렉터를 찾는 길.** 어빌리티는 게임 서비스를 쓰지 않고 `ArenaDirectorComponent::findForUnit` 으로 찾습니다 — 같은 오브젝트의 `ArenaUnitComponent` 가 든
디렉터 핸들(PROPERTY)을 씬에서 풉니다. 핫 리로드가 디렉터를 다시 만들어도(같은 id) 이어집니다. 어빌리티 카탈로그만 게임 서비스이고, 게임 인스턴스가 시작할
때마다 다시 겁니다.

**핫 리로드 · 상태 저장.** 판의 진행(웨이브 · 쓰러뜨린 수)은 PROPERTY 가 아니라 디렉터의 `writeState` 로 상태 스냅샷의 컴포넌트 섹션에 실려 넘어갑니다
(`ComponentStateStore`). 상태를 쓰기 전에 게임 인스턴스(생성자의 `registerDirector` 한 줄 — `GameInstanceBase`)가 진행을 싣고 디렉터가 세운 유닛 · AI 조종자 · 투사체를 걷으며, 다시 만든 디렉터는 진행을 받아
플레이어와 **같은 웨이브**를 새로 세웁니다(유닛의 체력 · 쿨다운은 새 판). 손으로는 `-gv_reloadGameAtFrame=N` 으로 리로드를 걸어 본다.

## 파일

- `AbilityArenaGame` — 카탈로그(클래스 등록 → `Resource/game/abilityarena/data/abilities.xml`)를 들고 게임 서비스로 걸고, 첫 씬을 열고, 상태 저장 전에
  디렉터가 세운 것을 걷습니다.
- `ArenaDirectorComponent` · `ArenaUnitComponent` · `ArenaEnemyAiComponent` · `ArenaAutoBattleAiComponent` · `ArenaProjectileComponent` ·
  `ArenaCameraComponent` — 위 표. 디렉터는 시나리오 탐침(`Arena.PlayerX` · `PlayerZ` · `PlayerShotCount` · `KillCount` · `PlayerControllerKind`)도 등록한다.
- `Resource/game/abilityarena/automation/control.scenario.xml` — 가상 키로 이동 · 화염구 한 번, 자동 플레이를 켜고 끄면 빙의가 오가는지.
- `ArenaAbilities` — 코드로 둔 어빌리티 셋(근접 · 투사체 · 대시). **숫자는 모두 데이터**(`<Param>`)라 같은 클래스를 플레이어와 적이 다른 값으로 씁니다.
- `Resource/game/abilityarena/maps/arena.scene.xml` · `prefabs/` — 엔진 직렬화기가 쓴 파일입니다(손으로 고치면 씬 · 프리팹 형식을 깨기 쉽다 — 에디터로).

밸런스(피해 · 쿨다운 · 비용 · 체력)는 전부 `abilities.xml` 에 있습니다 — 코드를 고치지 않고 바꿉니다. AI 거리(AI 프리팹) · 아레나 크기 · 편 색은 컴포넌트 PROPERTY 입니다.
