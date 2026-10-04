# Shooter3D — 1인칭 슈터 시험 게임

기반의 무기 규칙(`GameFramework/Combat` — 무기 카탈로그 · 무기 상태, 히트스캔은 `RayMath` · 1인칭 시점은 `FirstPersonLook`)를 실제로 쓰는 아레나 슈터입니다. 나무 상자 더미가 놓인 아레나로 드론(떠다니는 과녁)이 웨이브로
몰려옵니다. 손에 든 총 · 상자 · 과녁은 Kenney Blaster Kit 모델(`Resource/game/shooter3d/credits.md`)이고 벽 · 바닥은 내장 도형입니다.

## 빌드 · 실행

```powershell
cmake --preset Ninja-Debug-Shooter3D
cmake --build --preset Ninja-Debug-Shooter3D
cd build/Ninja-Debug-Shooter3D/Bin
./App.exe -dx12
./App.exe -dx12 -gv_shooterAutoPlay=1   # 조준 · 사격 · 이동도 AI(가까우면 산탄총, 멀면 소총)
./App.exe -dx12 -EnableEditor "-gv_editorStartupScene=game/shooter3d/maps/arena.scene.xml"
```

## 조작

| 키 | 하는 일 |
|----|---------|
| WASD · 마우스 | 이동 · 시점(마우스는 창 가운데에 잠긴다 — Esc 로 풀고 다시 잠근다) |
| 왼쪽 버튼 | 쏘기(소총은 누르고 있으면 연사, 산탄총 · 권총은 누를 때마다) |
| R | 재장전(빈 탄창은 저절로) |
| 1 · 2 · 3 · 휠 | 소총 · 산탄총 · 권총 |
| Space · LeftShift | 점프 · 달리기 |

드론(과녁, 늘 플레이어 쪽을 본다)은 다가와 부딪히며 체력을 깎습니다. 맞으면 잠깐 붉게 번쩍이고 HP 바(`HealthBarComponent`)가 줄어듭니다. 체력은 맞지 않고 4 초가 지나면 다시
차고, 바닥나면 웨이브 1 부터 다시 시작합니다. 웨이브마다 드론 수 · 체력 · 속도가 오르고 탄이 조금 채워집니다. 탄도선은 디버그 선이라 에디터 게임 뷰에서만 보입니다.

## 구조 — 씬 · 프리팹 · 컴포넌트

아레나는 씬 하나(`Resource/game/shooter3d/maps/arena.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)입니다. 레시피는 `Source/Games/README.md` 입니다.

| 무엇 | 어디 |
|------|------|
| 바닥 · 벽 넷 · 엄폐물 열(나무 상자 더미는 엄폐물의 자식) · 해 · 드론 스폰 자리 여덟 · 플레이어 · 디렉터 | 씬(엔티티) — 에디터에서 옮긴다 |
| 벽 · 엄폐물의 충돌 상자 | `ShooterBlockerComponent`(오브젝트 자리 ± 반 크기) — 디렉터가 플레이 시작에 모은다. 옮기면 충돌도 따라온다 |
| 드론 · 탄착/터짐 구 | 프리팹 `prefabs/drone.prefab.xml` · `effect.prefab.xml` — 드론은 웨이브마다 스폰, 효과 구는 풀(디렉터가 미리 세워 숨겨 두고 꺼내 쓴다) |
| 웨이브 · 쓰러뜨린 수 · 효과 풀 · 로그 | `ShooterDirectorComponent`(씬에 하나 — 언리얼 GameMode/GameState 자리) |
| 이동 · 점프 · 무기 셋 · 히트스캔 · 체력 · 조준선 · 탄도선 | `ShooterPlayerComponent` — 플레이어 오브젝트(카메라 · 손에 든 총 · 조준선 스프라이트와 같은 오브젝트) |
| 1인칭 시점 · 마우스 잠금 · 손에 든 총 자리 | GameFramework `FirstPersonCameraComponent`(같은 오브젝트) — 손에 든 총(`ViewWeapon`) · 조준선은 카메라의 자식이라 시점을 따라간다 |
| 드론 하나 | `ShooterDroneComponent` — 플레이어 눈 쪽으로 오며 이웃과 떨어지고 상자를 돌아간다, 맞으면 번쩍 · HP 바 |
| 모습 | 팔레트 머티리얼 `materials/palette.material`(상자 · 총 · 드론), 바닥 · 벽 `floor.material` · `wall.material`. 드론의 맞은 색 · 효과 색은 디렉터가 만든 머티리얼 인스턴스 |

**틱.** 디렉터는 `TickGroup::PrePhysics` 에서 쓰러진 드론을 걷고(쓰러뜨린 수 · 터짐 효과 · 효과음) 이번 프레임의 드론 자리 · 플레이어 눈 자리를 적습니다.
1인칭 카메라도 `PrePhysics` 에서 마우스로 시점을 돌립니다(자기 오브젝트). 플레이어 · 드론은 기본 그룹(`DuringPhysics`)에서 그것을 **읽기만** 하고(목록은
`data()` 로) 자기 오브젝트에만 씁니다. 다른 오브젝트에 쓰는 일 — 드론에 피해, 플레이어에 피해, 탄착 효과 꺼내기, 효과음, 손에 든 총 모델 바꾸기, 탄도선(디버그
선 큐) — 은 쌓아 두고 `executeOrDeferPostTick` 으로 틱 뒤 게임 스레드에서 합니다. 플레이어가 쓰러지면 그 자리에서 디렉터가 드론을 걷고 웨이브 1 부터 다시 기다립니다.

**핫 리로드 · 상태 저장.** 판의 진행(웨이브 · 쓰러뜨린 수)은 디렉터의 `writeState` 로 상태 스냅샷의 컴포넌트 섹션에 실려 넘어갑니다(`ComponentStateStore`).
상태를 쓰기 전에 게임(`onBeforeStateSerialize`)이 진행을 싣고 디렉터가 세운 드론 · 효과 풀을 걷으며, 다시 만든 디렉터는 웨이브 대기 뒤 **같은 웨이브**를 새로 세운다
(드론은 새 판). 무기 카탈로그는 게임 서비스이고 플레이어가 플레이 시작에 무기를 다시 든다.

## 파일 · 에셋

- `Shooter3DGame` — 무기 카탈로그(`Resource/game/shooter3d/data/weapons.xml`)를 게임 서비스로 걸고, 첫 씬을 열고, 상태 저장 전에 디렉터가 세운 것을 걷습니다.
- `ShooterDirectorComponent` · `ShooterPlayerComponent` · `ShooterDroneComponent` · `ShooterEffectComponent` · `ShooterBlockerComponent` — 위 표.
- `Resource/game/shooter3d/maps/arena.scene.xml` · `prefabs/` — 엔진 직렬화기가 쓴 파일입니다(손으로 고치면 씬 · 프리팹 형식을 깨기 쉽다 — 에디터로).
- `textures_raw/crosshair.png` · `hitmarker.png` — Kenney "Starter Kit FPS" 의 스프라이트(CC0), `App --import-textures` 가 DDS 로 굽는다.
  출처는 `Resource/game/shooter3d/credits.md`.

연사 간격 · 피해 · 퍼짐 · 반동 · 탄창은 전부 `weapons.xml` 에 있습니다. 이동 · 체력 · 드론 값은 컴포넌트 PROPERTY 입니다.
