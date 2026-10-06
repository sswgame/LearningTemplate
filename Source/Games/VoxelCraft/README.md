# VoxelCraft — 복셀 샌드박스 시험 게임

`GF_Voxel` 키트(블록 카탈로그 · 청크 월드 · 지형 생성 · 격자 광선 · 메싱 · 몸 충돌 · 핫바)를 실제로 쓰는 마인크래프트 장르입니다. 1인칭 시점은
기반의 `FirstPersonLook` 입니다(슈터 키트에 있던 것을 기반으로 옮겨, 이 게임은 키트 하나만 링크한다).

## 빌드 · 실행

```powershell
cmake --preset Ninja-Debug-VoxelCraft
cmake --build --preset Ninja-Debug-VoxelCraft
cd build/Ninja-Debug-VoxelCraft/Bin
./App.exe -dx12
./App.exe -dx12 -gv_voxelAutoPlay=1     # 걷고 뛰고 부수고 놓기도 AI
./App.exe -dx12 -scenario=game/voxelcraft/automation/control.scenario.xml   # 조종 시나리오 — 종료 코드 0 이 통과
./App.exe -dx12 -EnableEditor "-gv_editorStartupScene=game/voxelcraft/maps/island.scene.xml"
```

## 조작

| 키 | 하는 일 |
|----|---------|
| WASD · 마우스 | 걷기 · 시점(Esc 로 마우스 잠금 풀기 · 다시 잠그기) |
| Space · LeftShift | 점프(물에서는 위로 헤엄) · 달리기 |
| 왼쪽 버튼(누르고 있기) | 바라보는 블록 부수기 — 블록의 `hardness` 초가 걸린다. 풀은 흙, 돌은 조약돌이 된다 |
| 오른쪽 버튼 | 바라보는 면 앞 칸에 고른 블록 놓기(몸 안에는 놓지 않는다) |
| 1 – 9 · 휠 | 핫바 칸 고르기 |

월드는 128 × 64 × 128 블록(16 × 16 청크 8 × 8)이고 씨앗이 고정이라 매번 같은 섬입니다. 블록이 바뀐 청크(경계면 이웃 청크도)는 한 프레임에 네 개씩,
몸에서 가까운 것부터 다시 짓습니다. 물은 반투명 메시로 따로 그립니다.

## 구조 — 씬 · 프리팹 · 컴포넌트

섬은 씬 하나(`Resource/game/voxelcraft/maps/island.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)입니다. 레시피는 `Source/Games/README.md` 입니다.
씬에 놓인 것은 해 · 플레이어 · 블록 표시 · 디렉터 넷이고, 지형은 씨앗으로 절차 생성합니다.

| 무엇 | 어디 |
|------|------|
| 블록 월드 · 지형 짓기 · 꾸미기(광석 · 눈) · 블록 바꾸기 · 청크 다시 짓기 지시 · 로그 | `VoxelDirectorComponent`(씬에 하나) — 월드(`VoxelWorld`)를 든다 |
| 청크 하나의 불투명 · 물 메시 | 프리팹 `prefabs/chunk.prefab.xml` 의 `VoxelChunkComponent` — 디렉터가 청크마다(8 × 8) 세우고, 메시는 컴포넌트가 절차로 짓는다 |
| 몸 · 걷기 · 헤엄 · 부수기 · 놓기 · 핫바 | `VoxelPlayerComponent` — 플레이어 오브젝트(카메라와 같은 오브젝트). 입력을 읽지 않고 같은 오브젝트 `PawnComponent` 의 의도만 읽는다(버튼 `Voxel.Jump · Sprint · Break · Place · Slot1..9`, 아날로그 `Voxel.HotbarScroll` — 휠) |
| 자동 플레이 | `VoxelAutoPlayControllerComponent` — AI 조종자. 스위치(`gv_voxelAutoPlay` · 툴바)가 바뀌면 디렉터가 틱 뒤에 플레이어 폰을 이것 또는 플레이어 조종자에게 쥐어 준다 |
| 1인칭 시점 | GameFramework `FirstPersonCameraComponent`(같은 오브젝트 — 시점은 그 오브젝트 `PawnComponent` 의 조종 회전) |
| 시선(`Voxel.Look`) · 마우스 잠금(`ToggleMouseLock` — Esc) | GameFramework `PlayerControllerComponent`(조종 시스템이 세운다 — 플레이어 폰 `_autoPossess Player0`) |
| 바라보는 블록 표시(반투명 큐브 · 부수는 동안 네 단계로 진해짐 · 에디터 게임 뷰 테두리) | `VoxelHighlightComponent`(`BlockHighlight` 오브젝트) |
| 모습 | `materials/blocks.material`(블록 아틀라스) · `water.material`(유리와 같은 블렌드 + 아틀라스). 블록 표시는 엔진 유리 머티리얼의 단계별 인스턴스 |

**틱.** 디렉터는 `TickGroup::PrePhysics` 에서 블록이 바뀐 청크(경계면 이웃 청크도) 중 몸에서 가까운 것 네 개에 다시 짓기를 맡기고 표시를 지웁니다. 청크는 기본
그룹(`DuringPhysics`)에서 월드를 **읽기만** 하며 나란히 메싱하고, 엔진 메시로 옮기는 것은 틱 뒤 게임 스레드에서 합니다(새 메시를 만들어 건다 — 그리는 중인 정점
버퍼를 덮어쓰지 않는다). 플레이어도 같은 그룹에서 월드를 읽기만 하고(몸 충돌 · 광선), 블록 바꾸기 · 효과음은 쌓아 두었다가 틱 뒤에 디렉터에 건넵니다 — 월드는
틱 뒤에만 바뀐다. 블록 표시는 `PostUpdate` 에서 플레이어의 겨눔을 읽습니다.

**핫 리로드 · 상태 저장.** 블록(부수고 놓은 것 포함 — 같은 블록이 이어지는 구간으로 적는다)과 플레이어의 몸 자리 · 핫바 · 부순/놓은 수는 디렉터 · 플레이어의
`writeState` 로 상태 스냅샷의 컴포넌트 섹션에 실려 넘어갑니다(`ComponentStateStore`). 상태를 쓰기 전에 게임 인스턴스(생성자의 `registerDirector` 한 줄 — `GameInstanceBase`)가 둘을 싣고 디렉터가 세운
청크 오브젝트를 걷으며, 다시 만든 디렉터는 지형을 지은 뒤 블록을 되살리고 청크를 다시 세워 모두 다시 짓습니다.

## 파일 · 에셋

- `VoxelCraftGame` — 블록 카탈로그(`Resource/game/voxelcraft/data/blocks.xml`)를 게임 서비스로 걸고, 첫 씬을 열고, 상태 저장 전에 청크를 걷습니다.
- `VoxelDirectorComponent` · `VoxelChunkComponent` · `VoxelPlayerComponent` · `VoxelAutoPlayControllerComponent` · `VoxelHighlightComponent` — 위 표.
- `Resource/game/voxelcraft/automation/control.scenario.xml` — 가상 키 · 마우스로 걷기 · 핫바 · 시선 · 부수기, 자동 플레이를 켜고 끄면 빙의가 오가는지(탐침 `VoxelCraft.*`).
- `Resource/game/voxelcraft/maps/island.scene.xml` · `prefabs/chunk.prefab.xml` — 엔진 직렬화기가 쓴 파일입니다.
- `textures_raw/blocks.png` → `textures/blocks.dds` — Kenney Voxel Pack 타일(128 px) 4 × 4 아틀라스(`App --import-textures`, 출처는 `credits.md`).
- `sounds/` — 부수기 · 놓기 · 착지 효과음.
