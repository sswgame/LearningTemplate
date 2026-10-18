# WildWest — 서부 평원 승마 테스트 게임

## 이 게임으로 무엇을 배우나

레드 데드 리뎀션 2 · 젤다 BotW 식 오픈월드 승마를 작게 옮긴 게임입니다. 서부 풍 평원을 걷다가 곁의 말에 상호작용 버튼으로 올라타고, 걸음새(서기 · 걷기 · 속보 · 구보 · 질주)와 스태미나로 몰고, 내립니다.
탑승 단계 `Mount` · `Dismount` 와 탐침 `Pawn.Seat` 을 실제 게임에서 돌리는 자리이기도 합니다.

- 기반 탈것(`MountInteractionComponent` · `MountMovementComponent` · `VehicleSeatComponent` · `VehicleExitComponent` · `MountUtil`)으로 타고 내리기 — 운전석에 앉으면 빙의가 말로 넘어간다
- 서부극 키트(`GF_OpenWorldWestern`)의 말 상태(`WesternHorse`)를 탈것 이동에 잇기(`WesternHorseMountComponent`) — 스태미나가 바닥나면 질주를 막는다
- 주인을 따라오는 말 AI(`HorseFollowAIController`)가 탈 때 빙의를 내주고 내리면 되찾기, 휘파람으로 부르기
- 씬 데이터만으로 조종자와 폰을 잇기(`_possessAtStart`, `_autoPossess`)

## 빌드와 실행

```powershell
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug
cd build/Ninja-Debug/Bin
./App.exe -game=WildWest -dx12
./App.exe -game=WildWest -dx12 -gv_wildWestAutoPlay=1    # 기수가 가장 가까운 말에 타고 시작
./App.exe -game=WildWest -dx12 -scenario=game/wildwest/automation/mount.scenario.xml
```

시나리오 `automation/mount.scenario.xml` 은 Bessie 에 타고(`Mount`), W + Shift 로 2 초 질주하고(걸음새 4 · 속도 · 스태미나 · 위치 단언), 멈춘 뒤 내립니다(`Dismount`).
타기 전후로 `Pawn.Seat` 과 `Pawn.Controller` 로 좌석과 빙의가 오가는 것을 봅니다.

## 조작

| 키 | 액션 | 하는 일 |
|----|------|---------|
| WASD · 방향키 | `West.Move` | 걷기, 탔으면 말 몰기(입력 크기가 걸음새를 정한다) |
| E | `West.Interact` | 걸을 때는 타는 거리 안의 말에 타기, 탔을 때는 내리기 |
| Shift(누르는 동안) | `West.Gallop` | 질주(스태미나가 버틸 때만) |
| F | `West.Whistle` | 휘파람 — 주인을 따르는 말이 어디서든 온다 |

키 배치는 `data/wildwest.input.xml` 에 있습니다. 기수 폰과 말 폰이 같은 액션 이름을 의도 버튼으로 쓰므로 E 하나가 걸을 때는 타기, 탔을 때는 내리기가 됩니다.

## 구조

씬은 `Resource/game/wildwest/maps/prairie.scene.xml` 하나입니다.

- **기수**(`Player`)는 캐릭터 컨트롤러(뿌리)와 `CharacterPawnMovementComponent` 로 걷고, `MountInteractionComponent` 가 E 로 가까운 빈 좌석에 태웁니다. 바닥은 정적 강체 상자입니다.
- **말**(`Bessie` · `Shadow`)은 캐릭터 컨트롤러 없이 뿌리를 직접 옮기는 탈것입니다. 폰, 탈것 이동, 운전석, 내리기 버튼, 서부극 탈것 연결, 말 상태(`WildWestHorseComponent` — 품종은 `data/western.xml` 의 `<Horse id>`)가 붙어 있습니다.
- **말 AI**(`BessieAI` · `ShadowAI`)는 시작할 때 말을 쥐고(`_possessAtStart`) 기수를 주인으로 따라갑니다. 기수가 타면 빙의를 내주고, 내리면 다시 쥡니다.
- **목장**(`Ranch`)의 `WildWestDirectorComponent` 가 서부극 카탈로그를 읽고(`_westernDataPath`), 카메라 디렉터(`CameraDirectorComponent` + 프리셋 `data/prairie.cameras.xml`)의 초점을 기수에 두고(`setFocusPoint`), 휘파람을 말 AI 에 전하고, 같은 오브젝트의 HUD 에 한 줄을 씁니다.
  HUD 는 게임의 `WildWestHUDViewModel`(탑승 상태 글 `_rideStatus` · 말 스태미나 `_stamina` · `_staminaRatio`)을 `HUDControllerComponent` 에 걸어 씁니다(`ui/hud.ui.xml`).
- 말과 기수는 큐브와 캡슐을 쌓은 자리 표시입니다. 말 모델과 걸음새 애니메이션은 아직 없습니다.

## 더 볼 곳

- [Automation](../../Engine/Automation/README.md) — 시나리오 단계와 탐침
