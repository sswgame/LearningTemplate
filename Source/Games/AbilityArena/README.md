# AbilityArena — 어빌리티 시스템 시험 게임

`GameFramework/Ability`(언리얼 GAS 와 같은 어빌리티 시스템)를 실제 게임 흐름에서 쓰는 탑다운 웨이브 아레나입니다.
키트를 링크하지 않습니다 — 어빌리티 시스템은 `GameFramework` 기반에 있습니다.

## 빌드 · 실행

```powershell
cmake --preset Ninja-Debug -DSW_ACTIVE_GAME=AbilityArena   # 기존 빌드 디렉터리는 옛 값을 들고 있으니 다시 구성한다
cmake --build --preset Ninja-Debug
cd build/Ninja-Debug/Bin
./App.exe -dx12                          # 에디터 없이 — 월드가 처음부터 플레이 중이다
./App.exe -dx12 -gv_arenaAutoPlay=1      # 플레이어도 AI 가 움직인다(입력 없이 한 판을 끝까지)
```

에디터(`-EnableEditor`)로 띄우면 Play 를 눌러야 컴포넌트 틱(쿨다운 · 지속 시간)이 플레이 규칙대로 돕니다.

## 조작

| 키 | 입력 번호 | 어빌리티 | 보여 주는 것 |
|----|----------|---------|-------------|
| WASD · 방향키 | — | 이동 | 이동 속도는 `MoveSpeed` 어트리뷰트(current) — 대시 버프가 그대로 반영된다 |
| J · Space | 1 | `GA_Melee` (`MeleeStrike`) | 쿨다운, SetByCaller 피해, 방어 감쇠(`DamageExecution`) |
| K · 2 | 2 | `GA_Fireball` (`Projectile`) | 마나 비용, 쏜 순간의 스냅샷 스펙, 화상(주기 · 스택 3 · 시간 갱신) |
| L · 3 | 3 | `GA_Heal` (`ApplyEffects`) | **코드 없는 데이터 어빌리티**, 레벨 스케일, 정화(`RemoveEffectsWithTag`) |
| LeftShift · 4 | 4 | `GA_Dash` (`Dash`) | 시간이 걸리는 어빌리티(`waitDelay`), 이동 속도 ×3, 무적 태그가 피해 이펙트를 막음, 공격 막기(`BlockTag`) |

적: **Grunt**(빨강 캡슐) — 다가와 때리고, 맞으면 가시(`GA_Thorns`, 피격 트리거 + `targetEffect` — 데이터만)로 때린 쪽에 고정 피해.
**Caster**(보라 원뿔) — 거리를 두고 화염탄. 웨이브가 오를수록 수가 늘고 체력 · 공격력이 오릅니다. 플레이어는 쓰러지면 잠시 뒤 가운데에 다시 섭니다.
HP 바 · 피해 숫자는 어빌리티 시스템이 같은 오브젝트의 `HPBarBaseComponent` · `DamageUIComponent` 로 띄웁니다.

## 파일

- `AbilityArenaGame` — 카탈로그(클래스 등록 → `Resource/game/abilityarena/data/abilities.xml`)와 아레나를 들고 게임 서비스로 겁니다.
- `ArenaWorld` — 게임 규칙(유닛 · 웨이브 · AI · 투사체 · 입력 · 카메라). 어빌리티가 대상 찾기 · 투사체 발사를 부탁하는 창구입니다.
- `ArenaAbilities` — 코드로 둔 어빌리티 셋(근접 · 투사체 · 대시). **숫자는 모두 데이터**(`<Param>`)라 같은 클래스를 플레이어와 적이 다른 값으로 씁니다.

밸런스(피해 · 쿨다운 · 비용 · 체력)는 전부 `abilities.xml` 에 있습니다 — 코드를 고치지 않고 바꿉니다.
