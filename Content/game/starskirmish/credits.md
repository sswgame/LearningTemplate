# StarSkirmish — 에셋 출처

| 폴더 | 원본 | 라이선스 |
|------|------|----------|
| `models_raw/kaykit_space/*` · `textures_raw/kaykit_space.png` | KayKit "Space Base Bits 1.0" (<https://github.com/KayKit-Game-Assets/KayKit-Space-Base-Bits-1.0>, 커밋 `6dfbcac9927d06283752c4defd4882cfe0d29666`) — 지형 다섯(`terrain_low` · `terrain_tall` · `terrain_slope` · `terrain_slope_inner_corner` · `terrain_slope_outer_corner`) · 바위 넷(`rock_a` · `rock_b` · `rocks_a` · `rocks_b`), 큐브 절벽(`prefabs/cliff.prefab.xml`) 자리용 · 아틀라스 `spacebits_texture.png`. 경로 `addons/kaykit_space_base_bits/Assets/gltf/`. `Assets/LICENSE.txt` 원문 "License: (Creative Commons Zero, CC0)". 만든 이 Kay Lousberg | CC0 1.0 |
| `models_raw/*.glb` | Kenney "Space Kit" (<https://kenney.nl/assets/space-kit>) — 우주선 다섯 · 격납고 셋 · 발전기 · 안테나 · 구조물 · 착륙장 · 포탑 둘 · 결정 · 분화구 | CC0 1.0 |
| `sounds/confirmation_003.ogg` · `sounds/error_003.ogg` · `sounds/select_004.ogg` | Kenney "Interface Sounds" (<https://kenney.nl/assets/interface-sounds>) — `confirmation_003` · `error_003` · `select_004` | CC0 1.0 |
| `sounds/impact_metal_heavy_000.ogg` | Kenney "Impact Sounds" (<https://kenney.nl/assets/impact-sounds>) — `impactMetal_heavy_000` | CC0 1.0 |

- 원본은 `models_raw/` 에 두고, 엔진이 읽는 `models/*.mesh` 는 `App --import-models` 가 만든다(스탬프는 `models_raw/import.stamp`).
  파일 이름은 원본의 camelCase · `-` 를 소문자 `_` 로 바꾼 것이다.
- 원본 glb 는 내려받은 그대로다. UniGLTF 출력이라 씬 루트로 자식 노드를 적어 glTF 2.0 을 어기지만, 임포터가 그 항목을 맨 위 조상
  (`tmpParent`)으로 바꿔 읽고 경고한다.
- 모델 노드마다 키트 배치용 오프셋 (2, 0, 1.5) 이 들어 있어, 임포트 규칙(`Config/Editor/ModelImportConfig.json` 의 `StarSkirmish_KitLayoutOffset`,
  `translation: [-2, 0, -1.5]`)이 그것을 지워 모델 원점(바닥 가운데)이 엔진 원점에 온다. 경계 상자 중심(`recenter: xz`)은 발전기 · 결정 · 포탑처럼
  모양이 치우친 모델을 최대 6 cm 옮기므로 쓰지 않는다.
- Space Kit 모델은 텍스처가 없다 — 색은 키트 머티리얼 색이고 임포터가 정점 색으로 굽는다. 게임은 주인 색을 곱한다.
- KayKit Space Base 는 `.gltf` + `.bin` 이고 색은 아틀라스(`textures_raw/kaykit_space.png`, 원본 `spacebits_texture.png` 바이트 그대로)에서 읽는다.
  파일 이름을 소문자로 바꾸느라 `.gltf` 안의 버퍼 `uri` 만 새 `.bin` 이름으로 고쳤다. 키트 배치 오프셋이 없으므로 임포트 규칙
  `StarSkirmish_KayKitSpaceBase` 가 `StarSkirmish_KitLayoutOffset` 보다 먼저 맞아 옮기지 않는다. 게임 코드는 아직 절벽을 큐브로 그린다.
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl).
