# VoxelCraft — 에셋 출처

| 폴더 | 원본 | 라이선스 |
|------|------|----------|
| `textures_raw/blocks.png` | Kenney "Voxel Pack" (<https://kenney.nl/assets/voxel-pack>) — 128 px 타일 열여섯을 4 × 4 아틀라스로 모은 것(풀 윗면 · 풀 옆면 · 흙 · 돌 · 모래 · 물 · 기반암 · 통나무 옆 · 통나무 단면 · 잎 · 판자 · 조약돌 · 벽돌 · 눈 · 석탄 · 철) | CC0 1.0 |
| `sounds/*.ogg` | Kenney "Impact Sounds" (<https://kenney.nl/assets/impact-sounds>) — `footstep_grass_000` · `impactPlank_medium_001` · `impactSoft_medium_000` | CC0 1.0 |

- 칸 순서(왼쪽 위부터 줄마다 0, 1, 2 …)와 블록의 짝은 `data/blocks.xml` 이 정한다. 엔진이 읽는 `textures/blocks.dds` 는 `App --import-textures`
  가 만든다(스탬프는 `textures_raw/import.stamp`, 팔레트 규칙 — 밉 없이 BC7. 밉이 이웃 칸 색을 섞는다).
- 파일 이름은 원본의 camelCase 를 소문자 `_` 로 바꾼 것이다.
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl).
