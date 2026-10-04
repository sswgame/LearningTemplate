# Shooter3D — 에셋 출처

| 파일 | 원본 | 라이선스 |
|------|------|----------|
| `textures/crosshair.dds` | Kenney "Starter Kit FPS" `sprites/crosshair.png` | CC0 1.0 |
| `textures/hitmarker.dds` | Kenney "Starter Kit FPS" `sprites/hit.png` | CC0 1.0 |
| `models_raw/*.glb` · `textures_raw/blaster_colormap.png` | Kenney "Blaster Kit" (<https://kenney.nl/assets/blaster-kit>) — 총 셋(`blaster_a` · `blaster_d` · `blaster_h`) · 나무 상자 · 과녁 | CC0 1.0 |
| `sounds/*.ogg` | Kenney "Impact Sounds" (<https://kenney.nl/assets/impact-sounds>) — `footstep_concrete_000` · `impactMetal_light_001` · `impactMetal_medium_000` · `impactPlank_medium_000` | CC0 1.0 |

- 원본: <https://github.com/KenneyNL/Starter-Kit-FPS> (커밋 `185fd2326d74a5cf858cffc616f87cf9696f9cc0`). 에셋(2D 스프라이트 · 3D 모델 · 소리)은
  [CC0](https://creativecommons.org/publicdomain/zero/1.0/), 코드는 MIT 입니다. 우리는 스프라이트 둘만 옮겼고 코드는 쓰지 않았습니다.
- 엔진은 실행 중에 DDS 만 읽으므로 `Scripts/generate/GenerateGameTextures.py --kenney-fps <클론>` 이 PNG 를 RGBA8 DDS 로 옮깁니다.
- 모델 원본은 `models_raw/` · `textures_raw/` 에 두고, 엔진이 읽는 `models/*.mesh` · `textures/blaster_colormap.dds` 는 `App --import-models` ·
  `App --import-textures` 가 만든다(스탬프는 각 폴더의 `import.stamp`). 파일 이름은 원본의 camelCase · `-` 를 소문자 `_` 로 바꾼 것이다.
  모델은 모두 한 장의 팔레트 텍스처(`blaster_colormap`)에서 색을 읽는다.
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl).
