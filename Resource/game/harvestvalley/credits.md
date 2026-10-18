# HarvestValley — 에셋 출처

| 폴더 | 원본 | 라이선스 |
|------|------|----------|
| `models_raw/cart.glb` · `textures_raw/town_colormap.png` | Kenney "Fantasy Town Kit" 2.0 (<https://kenney.nl/assets/fantasy-town-kit>) — 수레(`cart`, 출하 상자 `ShippingBin` 자리) · 팔레트 텍스처(`Models/GLB format/Textures/colormap.png`). 묶음 <https://kenney.nl/media/pages/assets/fantasy-town-kit/efe948d309-1754222374/kenney_fantasy-town-kit_2.0.zip> (SHA-256 `1a7530c09f4d2fa2cdee259876f089334f8b1f27fa86a0c4f54ef86cdd8676ef`), `License.txt` 원문 "License: (Creative Commons Zero, CC0)" | CC0 1.0 |
| `models_raw/crops_dirt_*.glb` | Kenney "Nature Kit" 2.1 — 밭 흙(`crops_dirt_single` · `crops_dirt_row`, 밭 `FarmSoil` 자리). 묶음 <https://kenney.nl/media/pages/assets/nature-kit/37ac38a37b-1677698939/kenney_nature-kit.zip> (SHA-256 `fa7974a0d342bfe63c38664ba9f8ec1a4aab8ea25f099bdc56870e33588c4d9d`), `License.txt` 원문 "License: (Creative Commons Zero, CC0)" | CC0 1.0 |
| `models_raw/crops_corn_*.glb` · `models_raw/crops_leafs_*.glb` · `models_raw/crop_*.glb` · `models_raw/tent_*.glb` · `models_raw/fence_*.glb` · `models_raw/tree_*.glb` · `models_raw/log.glb` · `models_raw/stump_*.glb` · `models_raw/plant_*.glb` · `models_raw/flower_*.glb` | Kenney "Nature Kit" (<https://kenney.nl/assets/nature-kit>) — 작물 단계(옥수수 · 잎) · 다 자란 작물(순무 · 당근 · 호박) · 천막 · 울타리 · 나무 · 장작 · 그루터기 · 덤불 · 꽃 | CC0 1.0 |
| `sounds/*.ogg` | Kenney "Interface Sounds" (<https://kenney.nl/assets/interface-sounds>) — `confirmation_001` · `drop_002` · `pluck_001` · `select_002` | CC0 1.0 |

- 원본은 `models_raw/` 에 두고, 엔진이 읽는 `models/*.mesh` 는 `App --import-models` 가 만든다(스탬프는 `models_raw/import.stamp`).
  파일 이름은 원본의 camelCase · `-` 를 소문자 `_` 로 바꾼 것이다.
- 원본 glb 는 내려받은 그대로다. UniGLTF 출력이라 씬 루트로 자식 노드를 적어 glTF 2.0 을 어기지만, 임포터가 그 항목을 맨 위 조상
  (`tmpParent`)으로 바꿔 읽고 경고한다.
- Nature Kit 모델은 텍스처가 없다 — 색은 키트 머티리얼 색이고 임포터가 정점 색으로 굽는다. 수레(`cart`)만 팔레트 텍스처(`town_colormap`)에서
  색을 읽는다. 씬은 아직 출하 상자 · 밭에 큐브를 쓴다 — 쓰려면 `_meshID` 를 `game/harvestvalley/models/<이름>.mesh` 로 바꾸고 수레에는 그 텍스처를 건다.
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl).
