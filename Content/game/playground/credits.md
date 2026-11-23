# Playground — 에셋 출처

| 파일 | 원본 | 라이선스 |
|------|------|----------|
| `sounds/click.ogg` | Kenney "Interface Sounds" `Audio/click_002.ogg` — 공용 `common/sounds/kenney_interface_sounds/click_002.ogg` 와 같은 바이트(출처 · 해시는 `common/credits.md`) | CC0 1.0 |
| `models_raw/testhead.gltf` → `models/testhead.*` | 이 저장소에서 합성(구 셋 + 모프 타깃 일곱 · Talk 가중치 클립 · Idle 머리 회전) | 이 저장소 |
| `voice/testline.wav` → `voice/testline.visemes.xml` | 이 저장소에서 합성(120 Hz 배음 + 두 포먼트 봉우리 모음 넷 · 높은 대역 잡음, 16 kHz 모노) | 이 저장소 |
| 환경 쇼케이스 식생(`maps/envshowcase.scene.xml` 의 꽃 · 덤불 · 나무 · 바위 레이어) | Quaternius "Stylized Nature MegaKit" — 공용 원본 `common/models_raw/quaternius/nature/`(출처는 `common/credits.md`). 식생 구간 머티리얼은 `common/materials/quaternius/`(foliage.hlsl — 바람 · 페이드). 예전 Kenney Nature Kit 나무 · 덤불 · 꽃 · 그루터기 · 통나무는 지웠다 | CC0 1.0 |
| `heightfields_raw/valley*.png` · `heightfields/valley.layerweights` | `Scripts/dev/MakeTerrainShowcase.py` 가 만든 절차 생성 데이터 | 이 저장소 |
| `rigs_raw/*.glb` · `rigs_raw/*.rig.xml` | `Scripts/dev/MakeSyntheticRig.py --representatives` 가 만든 애니메이션 비용 측정용 합성 리그 | 이 저장소 |
| `models_raw/avatarsample_d.vrm` · `textures_raw/_shared/vrm_character_textures/*.png`(임포트가 꺼낸 내장 이미지, 이름은 내용 키) | VRoid Project "AvatarSample_D"(VRoid Studio 알파판 샘플, VRM 0.x) — <https://opengameart.org/content/vroid-studio-cc0-models> 에서 받음. 원 출처 · 조건: <https://vroid.pixiv.help/hc/en-us/articles/4402614652569-Do-VRoid-Studio-s-sample-models-come-with-conditions-of-use>. 텍스처 원본은 모델 임포트가 VRM 에 든 이미지를 그대로 꺼낸 것 | CC0 1.0 |
| `models_raw/crate_medium.glb` | Kenney "Blaster Kit"(묶음 출처는 `common/credits.md`) — 나무 상자(shooter3d 의 것과 같은 파일, 파괴 쇼케이스) | CC0 1.0 |
| `models_raw/brick_wall.gltf` · `models_raw/barrel.gltf` | 이 저장소에서 만든 상자 · 원기둥(파괴 쇼케이스) | 이 저장소 |
| `textures_raw/postprocess/*.png` → `textures/postprocess/*.dds` | 일회성 파이썬 스크립트로 만든 후처리 시나리오 텍스처(원본 PNG 가 정본 — 스크립트는 지웠다)(컬러 그레이딩 LUT 띠 · 블룸 먼지 마스크) | 이 저장소 |
| `textures/twod/*.dds` | 이 저장소에서 코드로 그린 2D 데모 픽셀 아트(스프라이트 · 타일 · 노멀 맵, 커밋 `1fb09535c`) | 이 저장소 |
| `wildwest/models_raw/*_*.glb` | Kenney "Nature Kit" 2.1 (<https://kenney.nl/assets/nature-kit>) — 선인장 둘(`cactus_short` · `cactus_tall`) · 울타리 둘(`fence_corner` · `fence_gate`) — 바위 · 곧은 울타리는 공용 Quaternius 모델로 바꿨다. 묶음 <https://kenney.nl/media/pages/assets/nature-kit/37ac38a37b-1677698939/kenney_nature-kit.zip> (SHA-256 `fa7974a0d342bfe63c38664ba9f8ec1a4aab8ea25f099bdc56870e33588c4d9d`) 의 `Models/GLTF format/<원본 이름>.glb` | CC0 1.0 |
| `wildwest/models_raw/horse.glb` | Quaternius "Farm Animals" (<https://opengameart.org/content/lowpoly-animated-farm-animal-pack>)의 `FBX/Horse.fbx` 를 FBX2glTF 0.13.1 로 glTF 변환 — 클립 `Idle` · `Walk` · `WalkSlow` · `Run` · `Jump` · `Death` | CC0 1.0 |
| `harvestvalley/models_raw/crops_dirt_*.glb` | Kenney "Nature Kit" 2.1 — 밭 흙(`crops_dirt_single` · `crops_dirt_row`, 밭 `FarmSoil` 자리). 묶음 <https://kenney.nl/media/pages/assets/nature-kit/37ac38a37b-1677698939/kenney_nature-kit.zip> (SHA-256 `fa7974a0d342bfe63c38664ba9f8ec1a4aab8ea25f099bdc56870e33588c4d9d`), `License.txt` 원문 "License: (Creative Commons Zero, CC0)" | CC0 1.0 |
| `harvestvalley/models_raw/crops_corn_*.glb` · `harvestvalley/models_raw/crops_leafs_*.glb` · `harvestvalley/models_raw/crop_*.glb` · `harvestvalley/models_raw/tent_*.glb` · `harvestvalley/models_raw/log.glb` · `harvestvalley/models_raw/stump_*.glb` | Kenney "Nature Kit" (<https://kenney.nl/assets/nature-kit>) — 작물 단계(옥수수 · 잎) · 다 자란 작물(순무 · 당근 · 호박) · 천막 · 장작 · 그루터기 | CC0 1.0 |
| `harvestvalley/sounds/*.ogg` | Kenney "Interface Sounds" (<https://kenney.nl/assets/interface-sounds>) — `confirmation_001` · `drop_002` · `pluck_001` · `select_002` | CC0 1.0 |
| `meadowvillage/models_raw/*.glb` | Kenney "Nature Kit" 2.1 (<https://kenney.nl/assets/nature-kit>) — 땅 타일 넷(`ground_grass` · `ground_path_tile` · `ground_river_tile` · `crops_dirt_single`). 묶음 <https://kenney.nl/media/pages/assets/nature-kit/37ac38a37b-1677698939/kenney_nature-kit.zip> (SHA-256 `fa7974a0d342bfe63c38664ba9f8ec1a4aab8ea25f099bdc56870e33588c4d9d`) 의 `Models/GLTF format/<원본 이름>.glb`, `License.txt` 원문 "License: (Creative Commons Zero, CC0)" | CC0 1.0 |
| `meadowvillage/prefabs/tree` · `grass` · `sprout`(마을 오브젝트 · 생물 모습) | Quaternius "Stylized Nature MegaKit" — 공용 원본 `common/models_raw/quaternius/nature/` 의 CommonTree_1 · Grass_Common_Tall · Plant_1(출처는 `common/credits.md`) | CC0 1.0 |
| `voxelcraft/textures_raw/blocks.png` | Kenney "Voxel Pack" (<https://kenney.nl/assets/voxel-pack>) — 128 px 타일 열여섯을 4 × 4 아틀라스로 모은 것(칸 순서와 블록의 짝은 `data/blocks.xml`) | CC0 1.0 |
| `voxelcraft/sounds/*.ogg` | Kenney "Impact Sounds" (<https://kenney.nl/assets/impact-sounds>) — `footstep_grass_000` · `impactPlank_medium_001` · `impactSoft_medium_000`(파일 이름은 소문자 `_` 로) | CC0 1.0 |
| `starskirmish/models_raw/kaykit_space/*` · `starskirmish/textures_raw/kaykit_space.png` | KayKit "Space Base Bits 1.0" (<https://github.com/KayKit-Game-Assets/KayKit-Space-Base-Bits-1.0>, 커밋 `6dfbcac9927d06283752c4defd4882cfe0d29666`) — 지형 다섯(`terrain_low` · `terrain_tall` · `terrain_slope` · `terrain_slope_inner_corner` · `terrain_slope_outer_corner`) · 바위 넷(`rock_a` · `rock_b` · `rocks_a` · `rocks_b`) — 씬에 견본으로만 둔다 · 아틀라스 `spacebits_texture.png`. 경로 `addons/kaykit_space_base_bits/Assets/gltf/`. `Assets/LICENSE.txt` 원문 "License: (Creative Commons Zero, CC0)". 만든 이 Kay Lousberg | CC0 1.0 |
| 절벽 바위(`starskirmish/prefabs/cliff_rock_*.prefab.xml`) | Quaternius "Stylized Nature MegaKit" — 공용 원본 `common/models_raw/quaternius/nature/` 의 Rock_Medium_1 · 2 · 3(출처는 `common/credits.md`) | CC0 1.0 |
| `starskirmish/models_raw/*.glb` | Kenney "Space Kit" (<https://kenney.nl/assets/space-kit>) — 우주선 다섯 · 격납고 셋 · 발전기 · 안테나 · 구조물 · 착륙장 · 포탑 둘 · 결정 · 분화구 | CC0 1.0 |
| `starskirmish/sounds/confirmation_003.ogg` · `starskirmish/sounds/error_003.ogg` · `starskirmish/sounds/select_004.ogg` | Kenney "Interface Sounds" (<https://kenney.nl/assets/interface-sounds>) — `confirmation_003` · `error_003` · `select_004` | CC0 1.0 |
| `starskirmish/sounds/impact_metal_heavy_000.ogg` | Kenney "Impact Sounds" (<https://kenney.nl/assets/impact-sounds>) — `impactMetal_heavy_000` | CC0 1.0 |
| `themepark/models_raw/*.glb` · `themepark/textures_raw/coaster_colormap.png` | Kenney "Coaster Kit" (<https://kenney.nl/assets/coaster-kit>) — 강철 레일 · 열차 · 승강장 · 기둥 · 매점 · 길 · 입구 · 휴지통. 모델은 모두 한 장의 팔레트 텍스처(`coaster_colormap`)에서 색을 읽는다(`themepark/materials/palette.material`) | CC0 1.0 |
| `themepark/sounds/*.ogg` | Kenney "Interface Sounds" (<https://kenney.nl/assets/interface-sounds>) — `confirmation_001` · `error_001` · `select_001` | CC0 1.0 |
| `abilityarena/models_raw/*.glb` · `abilityarena/textures_raw/dungeon_colormap.png` | Kenney "Mini Dungeon" (<https://kenney.nl/assets/mini-dungeon>) — 사람 · 오크 · 바닥 · 벽 · 기둥 · 함정(모두 한 장의 팔레트 텍스처에서 색을 읽는다, 파일 이름은 소문자 `_` 로) | CC0 1.0 |
| `abilityarena/sounds/impact_punch_*.ogg` | Kenney "Impact Sounds" (<https://kenney.nl/assets/impact-sounds>) — `impactPunch_medium_000` · `impactPunch_heavy_000` | CC0 1.0 |
| `abilityarena/sounds/error_004.ogg` · `abilityarena/sounds/maximize_001.ogg` | Kenney "Interface Sounds" (<https://kenney.nl/assets/interface-sounds>) | CC0 1.0 |

- `AudioServiceTest` 가 OGG 디코드를 실제 파일로 확인하는 테스트 자료입니다.
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl).
- 테스트 머리 · 음성은 얼굴 애니메이션 테스트(`FacialAnimationTest`) 자료입니다 — KayKit 캐릭터에는 모프 타깃이 없습니다.
- 서부 평원(`maps/prairie.scene.xml`)의 선인장 · 울타리 · 말은 `wildwest/models_raw/` 의 모델이고(Farm Animals 라이선스 근거: 묶음의 `License.txt` 원문 "License: (Creative Commons Zero, CC0)"),
  땅 · 목장 · 기수는 아직 엔진 기본 도형과 색만 바꾼 머티리얼이다. 말 클립 이름에는 임포트가 `armature_` 접두어를 붙인다.
- 액션 어드벤처 미니게임(`velmora/`)과 그 확장 팩(`Test/Mods/velmora_tidecaves`)은 바깥 에셋을 쓰지 않는다 — 땅은 엔진 기본 도형에 색만 바꾼 머티리얼이고 나머지는 디버그 그리기다.
- 허브 농장의 Nature Kit 모델(`harvestvalley/models_raw/`)은 내려받은 그대로다 — UniGLTF 출력이라 씬 루트로 자식 노드를 적어 glTF 2.0 을 어기지만 임포터가 맨 위 조상(`tmpParent`)으로 바꿔 읽고 경고한다. 텍스처가 없고 색은 임포터가 정점 색으로 굽는다.
- RTS 매치의 Kenney Space Kit 원본 glb(`starskirmish/models_raw/`)는 내려받은 그대로다(UniGLTF 출력 — 위 농장 모델과 같은 경고). 모델 노드마다 키트 배치용 오프셋 (2, 0, 1.5) 이 들어 있어, 임포트 규칙
  (`Config/Editor/ModelImportConfig.json` 의 `StarSkirmish_KitLayoutOffset`, `translation: [-2, 0, -1.5]`)이 그것을 지워 모델 원점(바닥 가운데)이 엔진 원점에 온다. 경계 상자 중심(`recenter: xz`)은 발전기 · 결정 · 포탑처럼
  모양이 치우친 모델을 최대 6 cm 옮기므로 쓰지 않는다. 텍스처가 없고 색은 키트 머티리얼 색을 정점 색으로 굽는다(게임은 주인 색을 곱한다).
- KayKit Space Base(`starskirmish/models_raw/kaykit_space/`)는 `.gltf` + `.bin` 이고 색은 아틀라스(`starskirmish/textures_raw/kaykit_space.png`, 원본 `spacebits_texture.png` 바이트 그대로)에서 읽는다. 파일 이름을 소문자로 바꾸느라
  `.gltf` 안의 버퍼 `uri` 만 새 `.bin` 이름으로 고쳤다. 키트 배치 오프셋이 없으므로 임포트 규칙 `StarSkirmish_KayKitSpaceBase` 가 먼저 맞아 옮기지 않는다.
- 테마파크 가장자리 나무 · 꽃 덤불(`themepark/maps/park.scene.xml` 의 PropScatter)은 공용 Quaternius "Stylized Nature MegaKit" 의 구간 모델이다(출처는 `common/credits.md`).
