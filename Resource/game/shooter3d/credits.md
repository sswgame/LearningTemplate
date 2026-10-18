# Shooter3D — 에셋 출처

| 파일 | 원본 | 라이선스 |
|------|------|----------|
| `textures_raw/crosshair.png` | Kenney "Starter Kit FPS" `sprites/crosshair.png` | CC0 1.0 |
| `textures_raw/hitmarker.png` | Kenney "Starter Kit FPS" `sprites/hit.png` | CC0 1.0 |
| `models_raw/kaykit_dungeon/*.glb` · `textures_raw/kaykit_dungeon.png` | KayKit "Dungeon Remastered 1.0" (<https://github.com/KayKit-Game-Assets/KayKit-Dungeon-Remastered-1.0>, 커밋 `b0ca9bd96a8072ab36a3a5464f00ed1e06a16d07`) — 벽 다섯(`wall` · `wall_half` · `wall_corner` · `wall_doorway` · `wall_broken`) · 바닥 둘 · 기둥 둘(`pillar` · `column`) · 엄폐물(`barrier*` 넷 · `crates_stacked` · `box_large` · `box_small` · `barrel_large` · `barrel_small` · `keg`), 큐브 벽 · 엄폐물(`Wall_*` · `Cover_*`) 자리용 · 아틀라스 `Assets/texture/dungeon_texture.png`. 경로 `addons/kaykit_dungeon_remastered/Assets/gltf/<이름>.gltf.glb`. `Assets/LICENSE.txt` 원문 "License: (Creative Commons Zero, CC0)". 만든 이 Kay Lousberg | CC0 1.0 |
| `models_raw/*.glb` · `textures_raw/blaster_colormap.png` | Kenney "Blaster Kit" (<https://kenney.nl/assets/blaster-kit>) — 총 셋(`blaster_a` · `blaster_d` · `blaster_h`) · 나무 상자 | CC0 1.0 |
| `models_raw/kaykit/*.glb` · `textures_raw/kaykit_knight.png` · `textures_raw/kaykit_rogue.png` · `textures_raw/kaykit_skeleton.png` | KayKit "Character Pack: Adventurers 1.0" (<https://github.com/KayKit-Game-Assets/KayKit-Character-Pack-Adventures-1.0>) — 캐릭터 `knight` · `rogue`(41 관절 리그 · 클립 76 — 무기 · 방패는 본에 붙은 `parts/*.mesh`), 그리고 "Character Pack: Skeletons 1.0" (<https://github.com/KayKit-Game-Assets/KayKit-Character-Pack-Skeletons-1.0>) — `skeleton_minion` · `skeleton_warrior` · `skeleton_rogue`(클립 95) · 무기 `skeleton_axe` · `skeleton_blade` · `skeleton_shield_small_a`. 만든 이 Kay Lousberg (www.kaylousberg.com) | CC0 1.0 |
| `sounds/*.ogg` | Kenney "Impact Sounds" (<https://kenney.nl/assets/impact-sounds>) — `footstep_concrete_000` · `impactMetal_light_001` · `impactMetal_medium_000` · `impactPlank_medium_000` | CC0 1.0 |

- 원본: <https://github.com/KenneyNL/Starter-Kit-FPS> (커밋 `185fd2326d74a5cf858cffc616f87cf9696f9cc0`). 에셋(2D 스프라이트 · 3D 모델 · 소리)은
  [CC0](https://creativecommons.org/publicdomain/zero/1.0/), 코드는 MIT 입니다. 우리는 스프라이트 둘만 옮겼고 코드는 쓰지 않았습니다.
- 원본은 `models_raw/` · `textures_raw/` 에 두고, 엔진이 읽는 `models/*.mesh` · `textures/*.dds` 는 `App --import-models` ·
  `App --import-textures` 가 만든다(스탬프는 각 폴더의 `import.stamp`). 파일 이름은 원본의 camelCase · `-` 를 소문자 `_` 로 바꾼 것이다.
  모델은 모두 한 장의 팔레트 텍스처(`blaster_colormap`)에서 색을 읽는다.
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl).
- KayKit 캐릭터는 스킨드 모델이라 `App --import-models` 가 `models/kaykit/<이름>.mesh` 와 옆 폴더 `models/kaykit/<이름>/`(스켈레톤 `<이름>.skeleton.json` ·
  본에 붙은 무기 · 투구 · 망토 `parts/*.mesh` · 클립 `clips/*.animclip`)를 만든다. 파일 이름은 원본의 `kaykit_` 접두어를 뗀 것이다. 원본 GLB 에 든 아틀라스
  텍스처(`knight_texture` · `rogue_texture` · `skeleton_texture`)는 `textures_raw/kaykit_knight.png` · `kaykit_rogue.png` · `kaykit_skeleton.png` 로 꺼냈다(바이트 그대로). CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kay Lousberg.
- KayKit Dungeon 모델은 `models/kaykit_dungeon/<이름>.mesh` 로 임포트된다(원본의 `.gltf.glb` 를 `.glb` 로 줄인 이름). 색은 `textures/kaykit_dungeon.dds` 에서
  읽는다. 맵(`arena.scene.xml`)은 아직 벽 · 엄폐물을 큐브로 그린다 — 쓰려면 `_meshID` 를 이 메시로 바꾸고 그 텍스처를 건다.
