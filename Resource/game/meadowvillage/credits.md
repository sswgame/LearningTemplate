# MeadowVillage — 에셋 출처

| 파일 | 원본 | 라이선스 |
|------|------|----------|
| `models_raw/*.glb` | Kenney "Nature Kit" 2.1 (<https://kenney.nl/assets/nature-kit>) — 땅 타일 넷(`ground_grass` · `ground_path_tile` · `ground_river_tile` · `crops_dirt_single`). 묶음 <https://kenney.nl/media/pages/assets/nature-kit/37ac38a37b-1677698939/kenney_nature-kit.zip> (SHA-256 `fa7974a0d342bfe63c38664ba9f8ec1a4aab8ea25f099bdc56870e33588c4d9d`) 의 `Models/GLTF format/<원본 이름>.glb` | CC0 1.0 |

- 라이선스 근거: 묶음의 `License.txt` 원문 "License: (Creative Commons Zero, CC0)".
- 씬은 아직 엔진 기본 큐브 · 머티리얼만 쓴다(`prefabs/tile.prefab.xml`). 위 모델은 그 타일 자리 표시를 바꿀 원본이다. 씬 · 데이터 · 입력 맵은 저장소에서 새로 썼다.
- 원본은 `models_raw/` 에 두고, 엔진이 읽는 `models/*.mesh` 는 `App --import-models` 가 만든다(스탬프는 `models_raw/import.stamp`).
  쓰려면 `MeshComponent::_meshID` 를 `game/meadowvillage/models/<이름>.mesh` 로 바꾼다. 파일 이름은 원본의 camelCase 를 소문자 `_` 로 바꾼 것이다.
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl).
