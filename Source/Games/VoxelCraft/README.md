# VoxelCraft — 복셀 샌드박스 테스트 게임

## 이 게임으로 무엇을 배우나

마인크래프트 장르의 게임입니다. `GF_Voxel` 키트의 블록 카탈로그, 청크 월드, 지형 생성, 격자 광선, 메싱, 몸 충돌, 핫바를 실제로 씁니다(키트는 이것 하나만 링크하고 1인칭 시점은 기반의 `FirstPersonCameraComponent`).

- 프리팹 스폰이 아니라 컴포넌트가 **절차로 메시를 만드는** 방법(청크마다 `VoxelChunkComponent`)
- 병렬 틱에서 월드를 읽기만 하고 블록 바꾸기는 틱 뒤에 모아 하는 방법
- 1인칭 몸이 폰의 의도로만 걷고 부수고 놓는 구조
- 블록이 바뀐 청크만 다시 만드는 방법

## 빌드와 실행

```powershell
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug
cd build/Ninja-Debug/Bin
./App.exe -game=VoxelCraft -dx12
./App.exe -game=VoxelCraft -dx12 -gv_voxelAutoPlay=1     # 걷고 뛰고 부수고 놓는 것도 AI 조종자가
./App.exe -game=VoxelCraft -dx12 -scenario=game/voxelcraft/automation/control.scenario.xml
./App.exe -game=VoxelCraft -dx12 -EnableEditor "-gv_editorStartupScene=game/voxelcraft/maps/island.scene.xml"
```

`control.scenario.xml` 은 가상 키와 마우스로 1초 걷고(W) 핫바 셋째 슬롯을 고르고(3) 마우스를 100픽셀 움직여 시선을 돌리고 아래를 보며 왼쪽 버튼을 4초 눌러 블록을 부순 뒤, 자동 플레이를 켜고 끄면서 플레이어 폰의 빙의가 AI 조종자와 플레이어 조종자 사이를 오가는지 봅니다(탐침 `VoxelCraft.*`).
`ai.scenario.xml` 은 자동 플레이 조종자(`VoxelAutoPlay`)가 폰을 쥐고(`Pawn.Controller`) 한 주기 동안 걷기 → 부수기 → 걷기 → 놓기를 거치는지(`AI.State`) 봅니다.

## 조작

| 키 | 하는 일 |
|----|---------|
| WASD, 마우스 | 걷기와 시점(Esc 로 마우스 잠금 풀기와 다시 잠그기) |
| Space, LeftShift | 점프(물에서는 위로 헤엄), 달리기 |
| 왼쪽 버튼(누르고 있기) | 바라보는 블록 부수기. 블록의 `hardness` 초가 걸립니다. 풀은 흙, 돌은 조약돌이 됩니다 |
| 오른쪽 버튼 | 바라보는 면 앞에 고른 블록 놓기(몸 안에는 놓지 않습니다) |
| 1–9, 휠 | 핫바 슬롯 고르기 |

키 배치는 `data/voxel.input.xml` 입니다. 월드는 128 × 64 × 128 블록(16 × 16 청크가 8 × 8)이고 씨앗이 고정이라 매번 같은 섬입니다. 블록이 바뀐 청크(경계면의 이웃 청크 포함)는 한 프레임에 네 개씩 몸에서 가까운 것부터 다시 만들고, 물은 반투명 메시로 따로 그립니다.

## 구조

섬은 씬 하나(`Resource/game/voxelcraft/maps/island.scene.xml`)이고 씬에는 해, 플레이어, 블록 표시, 디렉터 넷만 있으며 지형은 씨앗으로 절차 생성합니다.

`VoxelDirectorComponent` 가 블록 월드(`VoxelWorld`), 지형 생성과 꾸미기(광석, 눈), 블록 바꾸기, 청크 다시 만들기 지시를 맡고 청크 하나의 불투명 · 물 메시는 프리팹 `prefabs/chunk.prefab.xml` 의 `VoxelChunkComponent` 가 절차로 만듭니다.
몸(걷기, 헤엄, 부수기, 놓기, 핫바)은 `VoxelPlayerComponent` 가 같은 오브젝트 `PawnComponent` 의 의도만 읽어 처리하고 자동 플레이는 `VoxelAutoPlayControllerComponent`(AI 조종자), 시점은 `FirstPersonCameraComponent`(폰의 조종 회전), 시선(`Voxel.Look`)과 마우스 잠금은 `PlayerControllerComponent`(플레이어 폰의 `_autoPossess Player0`)입니다.
바라보는 블록 표시 `VoxelHighlightComponent` 는 부수는 동안 네 단계로 진해지는 반투명 큐브입니다. 플레이어 폰의 버튼은 `Voxel.Jump`, `Voxel.Sprint`, `Voxel.Break`, `Voxel.Place`, `Voxel.Slot1`–`Slot9` 이고 아날로그 `Voxel.HotbarScroll` 은 휠입니다. 자동 플레이 스위치(`-gv_voxelAutoPlay`, 툴바)가 바뀌면 디렉터가 틱 뒤에 플레이어 폰을 AI 조종자나 플레이어 조종자에게 넘깁니다.

**틱.** 디렉터는 `PrePhysics` 에서 블록이 바뀐 청크 중 몸에서 가까운 네 개에 다시 만들기를 맡기고 표시를 지웁니다. 청크는 기본 그룹(`DuringPhysics`)에서 월드를 읽기만 하며 나란히 메싱하고 엔진 메시로 옮기는 일은 틱 뒤 게임 스레드에서 합니다(그리는 중인 정점 버퍼를 덮어쓰지 않으려고 새 메시를 만들어 건다).
플레이어도 같은 그룹에서 월드를 읽기만 하고(몸 충돌, 광선) 블록 바꾸기와 효과음은 쌓아 두었다가 틱 뒤에 디렉터에 넘기므로 월드는 틱 뒤에만 바뀝니다. 블록 표시는 `PostUpdate` 에서 플레이어의 겨눔을 읽습니다.

**핫 리로드와 상태 저장.** 부수고 놓은 것을 포함한 블록(같은 블록이 이어지는 구간으로 적음)과 플레이어의 위치, 핫바, 부순 수와 놓은 수가 디렉터와 플레이어의 `writeState` 로 실립니다. 다시 만든 디렉터는 지형을 만든 뒤 블록을 복원하고 청크를 다시 만들어 모두 메싱합니다.

## 데이터

- `Resource/game/voxelcraft/data/blocks.xml` 이 블록 카탈로그이고 게임 인스턴스가 게임 서비스로 등록합니다.
- `textures_raw/blocks.png` 는 Kenney Voxel Pack 타일(128px)로 만든 4 × 4 아틀라스이고 `App --import-textures` 가 `textures/blocks.dds` 로 만듭니다(출처 `credits.md`).
- `sounds/` 의 부수기, 놓기, 착지 효과음을 사운드 이벤트(`audio/voxelcraft.audioevents.xml` 의 `Land`, `BlockBreak`, `BlockPlace`)가 2D 로 냅니다.

## 더 볼 곳

- [Shooter3D](../Shooter3D/README.md) — 같은 1인칭 카메라와 마우스 잠금을 쓰는 게임
- [Games](../README.md) — 씬, 프리팹, 디렉터 구조
