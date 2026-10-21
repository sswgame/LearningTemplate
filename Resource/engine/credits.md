# 엔진 팩 — 에셋 출처

| 파일 | 원본 | 라이선스 |
|------|------|----------|
| `fonts/kenney_future.ttf` · `fonts/kenney_future_narrow.ttf` | Kenney "Kenney Fonts" (<https://kenney.nl/assets/kenney-fonts>) — `Kenney Future.ttf` · `Kenney Future Narrow.ttf` | CC0 1.0 |
| `textures_raw/terrain/terrain_detail.png` | `Scripts/dev/MakeTerrainShowcase.py` 가 만든 절차 생성 데이터 | 이 저장소 |
| `textures_raw/prototype/grid_dark.png` · `textures_raw/prototype/grid_light.png` · `textures_raw/prototype/grid_orange.png` | Kenney "Prototype Textures" 1.0 (<https://kenney.nl/assets/prototype-textures>) — `PNG/Dark/texture_02.png` · `PNG/Light/texture_02.png` · `PNG/Orange/texture_02.png` 를 1024 → 512 로 줄임(Lanczos) | CC0 1.0 |
| `textures_raw/perlin.png` | `Scripts/dev/MakeNoiseTexture.py` 가 만든 이음매 없는 Perlin 잡음(채널마다 다른 씨앗) | 이 저장소 |
| `textures_raw/random/grass.jpg` | ambientCG "Grass001" (<https://ambientcg.com/a/Grass001>) — `Grass001_1K-JPG_Color.jpg` 를 1024 → 512 로 줄임(Lanczos, JPEG 품질 92) | CC0 1.0 |
| `textures_raw/sky/*.hdr` | Poly Haven HDRI 2K (<https://polyhaven.com/hdris>) — `autumn_field_puresky`(맑은 낮) · `overcast_soil_puresky`(흐림) · `belfast_sunset_puresky`(석양) · `moonless_golf`(야간)의 `<이름>_2k.hdr` 에서 이름의 `_2k` 만 뺌(바이트는 그대로) | CC0 1.0 |
| `textures_raw/surfaces/*/*.jpg` | Poly Haven 재질 1K JPG (<https://polyhaven.com/textures>) — `aerial_grass_rock` · `brown_mud_leaves_01` · `sand_01` · `coast_sand_rocks_02` · `snow_02` · `ganges_river_pebbles` · `weathered_brown_planks` · `red_brick` · `metal_plate` · `asphalt_02` 의 `_diff_1k` · `_nor_gl_1k` · `_rough_1k` 를 `_albedo` · `_normal` · `_rough` 로 이름만 바꿈(바이트는 그대로, AO · 변위 맵은 넣지 않음) | CC0 1.0 |
| `textures_raw/ui/kenney_ui/*/*.png` | Kenney "UI Pack" 2.0 (<https://kenney.nl/assets/ui-pack>) — `PNG/<색>/Default/*.png`, 색 폴더 이름은 소문자(Double 크기 · 벡터 · 글꼴 · 소리는 넣지 않음) | CC0 1.0 |
| `textures_raw/ui/kenney_cursor/*/*.png` | Kenney "Cursor Pack" 1.1 (<https://kenney.nl/assets/cursor-pack>) — `PNG/Basic/Default/*.png` · `PNG/Outline/Default/*.png` | CC0 1.0 |
| `textures_raw/ui/kenney_input_prompts/*/*.png` | Kenney "Input Prompts" 1.5A (<https://kenney.nl/assets/input-prompts>) — `Keyboard & Mouse` · `Xbox Series` · `PlayStation Series` 의 `Default/*.png` 가운데 핵심 키(글자 · 숫자 · F 키 · 조작 키 · 마우스, Xbox · PS5 의 버튼 · 방향 · 트리거 · 스틱 — 목록은 `textures.sources.json`), 폴더 이름은 소문자 `_` | CC0 1.0 |
| `textures_raw/particles/kenney_particle/*.png` | Kenney "Particle Pack" 1.1 (<https://kenney.nl/assets/particle-pack>) — `PNG (Transparent)/*.png` 가운데 계열마다 대표 40장(목록은 `textures.sources.json`, Rotated · 검은 배경판 · Unity 패키지는 넣지 않음) | CC0 1.0 |
| `textures_raw/pixel/kenney_roguelike/*.png` | Kenney "Roguelike/RPG Pack" 1.0 (<https://kenney.nl/assets/roguelike-rpg-pack>) — `Spritesheet/roguelikeSheet_transparent.png` 를 `roguelike_sheet.png` 로(바이트는 그대로, 16 px 칸 · 사이 1 px) | CC0 1.0 |
| `textures_raw/pixel/kenney_roguelike_characters/*.png` | Kenney "Roguelike Characters" 2.0 (<https://kenney.nl/assets/roguelike-characters>) — `Spritesheet/roguelikeChar_transparent.png` 를 `roguelike_characters.png` 로(바이트는 그대로) | CC0 1.0 |
| `textures_raw/pixel/zelda_like/*.png` | ArMM1998 "Zelda-like tilesets and sprites" (<https://opengameart.org/content/zelda-like-tilesets-and-sprites>) — `gfx_3.zip` 의 `Overworld` · `Inner` · `cave` · `objects` · `character` · `NPC_test` · `log` 를 소문자 이름으로(`NPC_test` → `npc`, 바이트는 그대로, `font.png` 는 넣지 않음) | CC0 1.0 |
| `textures_raw/pixel/puny_world/*.png` | Shade "16x16 Puny World Tileset" (<https://opengameart.org/content/16x16-puny-world-tileset>) — `punyworld-overworld-tileset.png` 를 `puny_world.png` 로(바이트는 그대로) | CC0 1.0 |
| `textures_raw/pixel/puny_characters/*.png` | Shade "Puny Characters" (<https://opengameart.org/content/puny-characters>) — `puny-characters.zip` 의 `Character-Base` · `Warrior-Blue` · `Soldier-Red` · `Mage-Cyan` · `Archer-Green` · `Slime` 을 소문자 `_` 이름으로(바이트는 그대로, 나머지 색 바꿈 · 오크 묶음은 넣지 않음) | CC0 1.0 |
| `textures/missing.dds` | `Scripts/generate/GenerateSpriteTextures.py` 가 만든 누락 표시 체커(마젠타 · 검정) | 이 저장소 |
| `textures/loading.dds` | `Scripts/generate/GenerateSpriteTextures.py` 가 만든 로드 중 표시(고른 중간 회색) | 이 저장소 |
| `textures/test/quadrants.dds` | `Scripts/generate/GenerateSpriteTextures.py` 가 만든 네 칸 테스트 텍스처 | 이 저장소 |
| `textures/test/checker.dds` · `textures/test/pixelstripes.dds` | 이 저장소에서 만든 테스트 패턴(체커 · 줄무늬 — 커밋 `6361d2d54` · `bb857b284`) | 이 저장소 |

- 내려받은 묶음의 `License.txt` 원문으로 CC0 인지 확인했다("License: (Creative Commons Zero, CC0)"). 파일 이름은 원본의 공백을 소문자 `_` 로 바꾼 것이다(바이트는 그대로).
- 저장소에는 CC0 라틴 글꼴만 둔다. 한글 · CJK · 아랍 문자는 OS 시스템 글꼴로 대체한다(런타임 UI 결정 R1).
- ambientCG 는 라이선스 페이지 원문으로 CC0 인지 확인했다("Creative Commons CC0 1.0 Universal License").
- Poly Haven 은 라이선스 페이지(<https://polyhaven.com/license>) 원문으로 CC0 인지 확인했다("Our assets are all licensed as CC0" · "You do not need to give credit or attribution when using them" · "You can redistribute them").
- Kenney UI Pack · Cursor Pack · Input Prompts 는 `License.txt` 의 "License: (Creative Commons Zero, CC0)", Particle Pack 은 "License (Creative Commons Zero, CC0)" 원문과 CC0 1.0 주소(`creativecommons.org/publicdomain/zero/1.0/`)로 확인했다.
- 픽셀 아트(`pixel/`)는 Kenney 두 팩이 `License.txt` 원문("License (Creative Commons Zero, CC0)" · "License: (Creative Commons Zero, CC0)"), OpenGameArt 세 팩이 공식 페이지의 라이선스 칸("License(s): CC0" — CC0 하나만, CC BY 병기 없음)으로 확인했다. 16 px 세트만 들였다(496 아이콘 팩 · CC BY 팩은 넣지 않는다).
- `sky/` · `surfaces/` · `ui/kenney_*` · `particles/` · `pixel/` 원본의 정본 목록은 `textures.sources.json`(원본 URL · SHA-256 · 라이선스 인용, 칸은 공용 팩 `audio.sources.json` 과 같다)이다. 린트 `CheckResourceCredits` 도 해시와 목록 밖 파일을 대조한다. `Scripts/dev/MakeCc0Import.py fetch` 가 그 목록대로 내려받아 해시와 라이선스 인용을 대조한 뒤 다시 쓰고, `verify` 가 저장소 파일을 목록과 대조한다.
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl), ambientCG (ambientcg.com), Poly Haven (polyhaven.com), ArMM1998 · Shade (opengameart.org).
