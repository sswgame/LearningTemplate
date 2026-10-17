# NileCity — 에셋 출처

| 폴더 | 원본 | 라이선스 |
|------|------|----------|
| `models_raw/statue_*.glb` · `models_raw/crops_*.glb` | Kenney "Nature Kit" 2.1 (<https://kenney.nl/assets/nature-kit>) — 조각상 넷(`statue_obelisk` · `statue_head` · `statue_column` · `statue_block`) · 밭(`crops_dirt_row` · `crops_wheat_stage_a` · `crops_wheat_stage_b`). 색 상자(`prefabs/blockbuilding.prefab.xml`)로 그리는 밭 · 조각상 자리용. 묶음 <https://kenney.nl/media/pages/assets/nature-kit/37ac38a37b-1677698939/kenney_nature-kit.zip> (SHA-256 `fa7974a0d342bfe63c38664ba9f8ec1a4aab8ea25f099bdc56870e33588c4d9d`), `License.txt` 원문 "License: (Creative Commons Zero, CC0)" | CC0 1.0 |
| `models_raw/*.glb` · `textures_raw/suburban_colormap.png` | Kenney "City Kit (Suburban)" (<https://kenney.nl/assets/city-kit-suburban>) — 집 여덟 종 · 낮은 울타리 · 보도 · 화분 · 나무 | CC0 1.0 |
| `sounds/*.ogg` | Kenney "Interface Sounds" (<https://kenney.nl/assets/interface-sounds>) — `confirmation_002` · `drop_003` · `error_002` · `select_003` | CC0 1.0 |

- 원본은 `models_raw/` · `textures_raw/` 에 그대로 두고, 엔진이 읽는 `models/*.mesh` · `textures/*.dds` 는 `App --import-models` ·
  `App --import-textures` 가 만든다(스탬프는 각 폴더의 `import.stamp`). 파일 이름은 원본의 `-` 를 `_` 로 바꾼 것이다.
- 모델은 모두 한 장의 팔레트 텍스처(`suburban_colormap`)에서 색을 읽는다 — 게임은 `PrimitiveLook::_texturePath` 로 그것을 걸고,
  서비스 건물은 색을 곱해 종류를 보인다. Nature Kit 모델(조각상 · 밭)은 텍스처 없이 키트 머티리얼 색을 정점 색으로 굽는다.
  게임 코드는 아직 밭 · 조각상을 색 상자로 그린다 — 건물 표(`NileBuildingComponent::hasModel`)에 이 메시를 이어야 쓰인다.
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl).
