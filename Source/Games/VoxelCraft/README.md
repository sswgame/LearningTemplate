# VoxelCraft — 복셀 샌드박스 시험 게임

`GF_Voxel` 키트(블록 카탈로그 · 청크 월드 · 지형 생성 · 격자 광선 · 메싱 · 몸 충돌 · 핫바)를 실제로 쓰는 마인크래프트 장르입니다. 1인칭 시점은
기반의 `FirstPersonLook` 입니다(슈터 키트에 있던 것을 기반으로 옮겨, 이 게임은 키트 하나만 링크한다).

## 빌드 · 실행

```powershell
cmake --preset Ninja-Debug -DSW_ACTIVE_GAME=VoxelCraft
cmake --build --preset Ninja-Debug
cd build/Ninja-Debug/Bin
./App.exe -dx12
./App.exe -dx12 -gv_voxelAutoPlay=1     # 걷고 뛰고 부수고 놓기도 AI
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

## 파일 · 에셋

- `VoxelCraftGame` — 블록 카탈로그(`Resource/game/voxelcraft/data/blocks.xml`)를 읽고 월드를 둡니다.
- `VoxelCraftWorld` — 지형 꾸미기(광석 · 눈), 청크 메시 → 엔진 메시, 몸 · 시점, 부수기 · 놓기, 핫바, 블록 표시.
- `textures_raw/blocks.png` → `textures/blocks.dds` — Kenney Voxel Pack 타일(128 px) 4 × 4 아틀라스(`App --import-textures`, 출처는 `credits.md`).
- `sounds/` — 부수기 · 놓기 · 착지 효과음.
