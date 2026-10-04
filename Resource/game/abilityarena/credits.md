# AbilityArena — 에셋 출처

| 폴더 | 원본 | 라이선스 |
|------|------|----------|
| `models_raw/*.glb` · `textures_raw/dungeon_colormap.png` | Kenney "Mini Dungeon" (<https://kenney.nl/assets/mini-dungeon>) — 사람 · 오크 · 바닥 · 벽 · 기둥 · 깃발 · 상자 · 물약 · 동전 · 바위 · 함정 | CC0 1.0 |
| `sounds/impact_punch_*.ogg` | Kenney "Impact Sounds" (<https://kenney.nl/assets/impact-sounds>) — `impactPunch_medium_000` · `impactPunch_heavy_000` | CC0 1.0 |
| `sounds/error_004.ogg` · `sounds/maximize_001.ogg` | Kenney "Interface Sounds" (<https://kenney.nl/assets/interface-sounds>) | CC0 1.0 |

- 원본은 `models_raw/` · `textures_raw/` 에 그대로 두고, 엔진이 읽는 `models/*.mesh` · `textures/*.dds` 는 `App --import-models` ·
  `App --import-textures` 가 만든다(스탬프는 각 폴더의 `import.stamp`). 파일 이름은 원본의 camelCase · `-` 를 소문자 `_` 로 바꾼 것이다.
- 모델은 모두 한 장의 팔레트 텍스처(`dungeon_colormap`)에서 색을 읽는다 — 게임은 머티리얼 인스턴스의 `albedoMap` 으로 그것을 건다.
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl).
