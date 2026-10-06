# VoxelCraft — 복셀 샌드박스 테스트 게임

## 이 게임으로 무엇을 배우나

마인크래프트 장르의 게임입니다. `GF_Voxel` 키트의 블록 카탈로그, 청크 월드, 지형 생성, 격자 광선, 메싱, 몸 충돌, 핫바를 실제로 씁니다.

- 프리팹 스폰이 아니라 컴포넌트가 **절차로 메시를 만드는** 방법(청크마다 `VoxelChunkComponent`)
- 병렬 틱에서 월드를 읽기만 하고, 블록 바꾸기는 틱 뒤에 모아 하는 방법
- 1인칭 몸이 폰의 의도로만 걷고 부수고 놓는 구조
- 블록이 바뀐 청크만 다시 만드는 방법

1인칭 시점은 기반의 `FirstPersonCameraComponent` 이고, 이 게임은 키트 하나만 링크합니다.

## 빌드와 실행

```powershell
cmake --preset Ninja-Debug-VoxelCraft
cmake --build --preset Ninja-Debug-VoxelCraft
cd build/Ninja-Debug-VoxelCraft/Bin
./App.exe -dx12
./App.exe -dx12 -gv_voxelAutoPlay=1     # 걷고 뛰고 부수고 놓는 것도 AI 조종자가
./App.exe -dx12 -scenario=game/voxelcraft/automation/control.scenario.xml
./App.exe -dx12 -EnableEditor "-gv_editorStartupScene=game/voxelcraft/maps/island.scene.xml"
```

`control.scenario.xml` 은 가상 키와 마우스로 1초 걷고(W), 핫바 셋째 슬롯을 고르고(3), 마우스를 100픽셀 움직여 시선을 돌리고, 아래를 보며 왼쪽 버튼을 4초 눌러 블록을 부숩니다.
그다음 자동 플레이를 켜고 끄면서 플레이어 폰의 빙의가 AI 조종자와 플레이어 조종자 사이를 오가는지 봅니다. 탐침은 `VoxelCraft.*`(걸은 거리, 핫바 슬롯, 조종 요, 부순 수, 조종자 종류)입니다.

## 조작

| 키 | 하는 일 |
|----|---------|
| WASD, 마우스 | 걷기와 시점(Esc 로 마우스 잠금 풀기와 다시 잠그기) |
| Space, LeftShift | 점프(물에서는 위로 헤엄), 달리기 |
| 왼쪽 버튼(누르고 있기) | 바라보는 블록 부수기. 블록의 `hardness` 초가 걸립니다. 풀은 흙, 돌은 조약돌이 됩니다 |
| 오른쪽 버튼 | 바라보는 면 앞에 고른 블록 놓기(몸 안에는 놓지 않습니다) |
| 1–9, 휠 | 핫바 슬롯 고르기 |

키 배치는 `data/voxel.input.xml` 에 있습니다.
월드는 128 × 64 × 128 블록(16 × 16 청크가 8 × 8)이고 씨앗이 고정이라 매번 같은 섬입니다. 블록이 바뀐 청크(경계면의 이웃 청크 포함)는 한 프레임에 네 개씩, 몸에서 가까운 것부터 다시 만듭니다.
물은 반투명 메시로 따로 그립니다.

## 구조

섬은 씬 하나(`Resource/game/voxelcraft/maps/island.scene.xml`)입니다. 씬에는 해, 플레이어, 블록 표시, 디렉터 네 개만 있고, 지형은 씨앗으로 절차 생성합니다.

| 무엇 | 어디 |
|------|------|
| 블록 월드, 지형 생성과 꾸미기(광석, 눈), 블록 바꾸기, 청크 다시 만들기 지시, 로그 | `VoxelDirectorComponent`. 월드(`VoxelWorld`)를 가집니다 |
| 청크 하나의 불투명 메시와 물 메시 | 프리팹 `prefabs/chunk.prefab.xml` 의 `VoxelChunkComponent`. 디렉터가 청크마다 만들고, 메시는 컴포넌트가 절차로 만듭니다 |
| 몸, 걷기, 헤엄, 부수기, 놓기, 핫바 | `VoxelPlayerComponent`. 같은 오브젝트 `PawnComponent` 의 의도만 읽습니다 |
| 자동 플레이 | `VoxelAutoPlayControllerComponent`(AI 조종자) |
| 1인칭 시점 | GameFramework `FirstPersonCameraComponent`. 시점은 폰의 조종 회전입니다 |
| 시선(`Voxel.Look`)과 마우스 잠금(`ToggleMouseLock`, Esc) | GameFramework `PlayerControllerComponent`(플레이어 폰의 `_autoPossess Player0`) |
| 바라보는 블록 표시 | `VoxelHighlightComponent`. 반투명 큐브이고 부수는 동안 네 단계로 진해집니다 |
| 머티리얼 | `materials/blocks.material`(블록 아틀라스), `water.material`. 블록 표시는 엔진 유리 머티리얼의 단계별 인스턴스 |

플레이어 폰의 버튼은 `Voxel.Jump`, `Voxel.Sprint`, `Voxel.Break`, `Voxel.Place`, `Voxel.Slot1` 부터 `Voxel.Slot9` 이고, 아날로그 `Voxel.HotbarScroll` 은 휠입니다.
자동 플레이 스위치(`-gv_voxelAutoPlay`, 툴바)가 바뀌면 디렉터가 틱 뒤에 플레이어 폰을 AI 조종자나 플레이어 조종자에게 넘깁니다.

**틱.** 디렉터는 `PrePhysics` 에서 블록이 바뀐 청크 중 몸에서 가까운 네 개에 다시 만들기를 맡기고 표시를 지웁니다.
청크는 기본 그룹(`DuringPhysics`)에서 월드를 읽기만 하며 나란히 메싱하고, 엔진 메시로 옮기는 일은 틱 뒤 게임 스레드에서 합니다.
옮길 때는 새 메시를 만들어 겁니다. 그리는 중인 정점 버퍼를 덮어쓰지 않기 위해서입니다.
플레이어도 같은 그룹에서 월드를 읽기만 하고(몸 충돌, 광선), 블록 바꾸기와 효과음은 쌓아 두었다가 틱 뒤에 디렉터에 넘깁니다. 그래서 월드는 틱 뒤에만 바뀝니다.
블록 표시는 `PostUpdate` 에서 플레이어의 겨눔을 읽습니다.

**핫 리로드와 상태 저장.** 부수고 놓은 것을 포함한 블록(같은 블록이 이어지는 구간으로 적습니다)과 플레이어의 위치, 핫바, 부순 수와 놓은 수가 디렉터와 플레이어의 `writeState` 로 실립니다.
다시 만든 디렉터는 지형을 만든 뒤 블록을 복원하고, 청크를 다시 만들어 모두 메싱합니다.

## 데이터

- `Resource/game/voxelcraft/data/blocks.xml` 이 블록 카탈로그이고, 게임 인스턴스가 게임 서비스로 등록합니다.
- `textures_raw/blocks.png` 는 Kenney Voxel Pack 타일(128px)로 만든 4 × 4 아틀라스이고, `App --import-textures` 가 `textures/blocks.dds` 로 만듭니다. 출처는 `credits.md` 입니다.
- `sounds/` 에 부수기, 놓기, 착지 효과음이 있습니다.

## 더 볼 곳

- [Shooter3D](../Shooter3D/README.md) — 같은 1인칭 카메라와 마우스 잠금을 쓰는 게임
- [GameFramework](../../GameFramework/README.md) — 폰, 조종자, 1인칭 카메라
- [Games](../README.md) — 씬, 프리팹, 디렉터 구조
