# ThemeParkTycoon — 에셋 출처

| 폴더 | 원본 | 라이선스 |
|------|------|----------|
| `models_raw/*.glb` · `textures_raw/coaster_colormap.png` | Kenney "Coaster Kit" (<https://kenney.nl/assets/coaster-kit>) — 강철 레일 · 열차 · 승강장 · 기둥 · 매점 · 길 · 입구 · 나무 · 벤치 · 휴지통 · 꽃 | CC0 1.0 |
| `sounds/*.ogg` | Kenney "Interface Sounds" (<https://kenney.nl/assets/interface-sounds>) — `confirmation_001` · `error_001` · `select_001` | CC0 1.0 |

- 원본은 `models_raw/` · `textures_raw/` 에 그대로 두고, 엔진이 읽는 `models/*.mesh` · `textures/*.dds` 는 `App --import-models` ·
  `App --import-textures` 가 만든다(스탬프는 각 폴더의 `import.stamp`). 파일 이름은 원본의 `-` 를 `_` 로 바꾼 것이다.
- 모델은 모두 한 장의 팔레트 텍스처(`coaster_colormap`)에서 색을 읽는다 — 게임은 `PrimitiveLook::_texturePath` 로 그것을 건다.
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl).
