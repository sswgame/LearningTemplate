# Shooter3D — 1인칭 슈터 시험 게임

기반의 무기 규칙(`GameFramework/Combat` — 무기 카탈로그 · 무기 상태, 히트스캔은 `RayMath` · 1인칭 시점은 `FirstPersonLook`)를 실제로 쓰는 아레나 슈터입니다. 상자가 놓인 아레나로 드론이 웨이브로 몰려옵니다.

## 빌드 · 실행

```powershell
cmake --preset Ninja-Debug -DSW_ACTIVE_GAME=Shooter3D
cmake --build --preset Ninja-Debug
cd build/Ninja-Debug/Bin
./App.exe -dx12
./App.exe -dx12 -gv_shooterAutoPlay=1   # 조준 · 사격 · 이동도 AI(가까우면 산탄총, 멀면 소총)
```

## 조작

| 키 | 하는 일 |
|----|---------|
| WASD · 마우스 | 이동 · 시점(마우스는 창 가운데에 잠긴다 — Esc 로 풀고 다시 잠근다) |
| 왼쪽 버튼 | 쏘기(소총은 누르고 있으면 연사, 산탄총 · 권총은 누를 때마다) |
| R | 재장전(빈 탄창은 저절로) |
| 1 · 2 · 3 · 휠 | 소총 · 산탄총 · 권총 |
| Space · LeftShift | 점프 · 달리기 |

드론(빨간 구)은 다가와 부딪히며 체력을 깎습니다. 맞으면 잠깐 하얗게 번쩍이고 HP 바(`HealthBarComponent`)가 줄어듭니다. 체력은 맞지 않고 4 초가 지나면 다시
차고, 바닥나면 웨이브 1 부터 다시 시작합니다. 웨이브마다 드론 수 · 체력 · 속도가 오르고 탄이 조금 채워집니다. 탄도선은 디버그 선이라 에디터 게임 뷰에서만 보입니다.

## 파일 · 에셋

- `Shooter3DGame` — 무기 카탈로그(`Resource/game/shooter3d/data/weapons.xml`)를 읽고 아레나를 둡니다.
- `ShooterArena` — 이동 · 충돌(원 대 상자), 무기 셋, 히트스캔(상자 · 바닥 · 드론 중 가장 가까운 것), 드론 웨이브, 조준선 · 맞음 표시.
- `textures/crosshair.dds` · `hitmarker.dds` — Kenney "Starter Kit FPS" 의 스프라이트(CC0). 출처는 `Resource/game/shooter3d/credits.md`,
  다시 만드는 법은 `Scripts/generate/GenerateGameTextures.py`.

연사 간격 · 피해 · 퍼짐 · 반동 · 탄창은 전부 `weapons.xml` 에 있습니다.
