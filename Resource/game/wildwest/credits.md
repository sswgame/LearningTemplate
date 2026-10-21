# WildWest — 에셋 출처

| 파일 | 원본 | 라이선스 |
|------|------|----------|
| `models_raw/*.glb` | Kenney "Nature Kit" 2.1 (<https://kenney.nl/assets/nature-kit>) — 선인장 둘(`cactus_short` · `cactus_tall`) · 바위 넷(`rock_large_a` · `rock_large_b` · `rock_small_a` · `rock_tall_a`) · 울타리 넷(`fence_simple` · `fence_corner` · `fence_gate` · `fence_planks`). 묶음 <https://kenney.nl/media/pages/assets/nature-kit/37ac38a37b-1677698939/kenney_nature-kit.zip> (SHA-256 `fa7974a0d342bfe63c38664ba9f8ec1a4aab8ea25f099bdc56870e33588c4d9d`) 의 `Models/GLTF format/<원본 이름>.glb` | CC0 1.0 |
| `models_raw/horse_placeholder.gltf` | 저장소에서 생성 — `Scripts/generate/GeneratePlaceholderHorse.py`(상자 스킨드 말, 걸음새 클립 넷 `Walk` · `Trot` · `Canter` · `Gallop`, 기수 소켓 뼈 `Saddle`) | 이 저장소 |

- 라이선스 근거: 묶음의 `License.txt` 원문 "License: (Creative Commons Zero, CC0)".
- 씬의 선인장(`Cactus_*`) · 바위(`Rock_*`) · 울타리(`Fence_*`)는 위 모델을 부른다(`Scripts/dev/MakeCc0SceneProps.py` 가 `Config/Editor/Cc0PropPlacement.json`
  대로 자리 표시 큐브를 바꿨다). 나머지(땅 · 목장 · 기수)는 아직 엔진 기본 도형과 색만 바꾼 머티리얼이다. 씬 · 데이터 · 입력 맵 · HUD 문서는 저장소에서 새로 썼다.
- 원본은 `models_raw/` 에 두고, 엔진이 읽는 `models/*.mesh` 는 `App --import-models` 가 만든다(스탬프는 `models_raw/import.stamp`).
  씬에서 쓰려면 `MeshComponent::_meshID` 를 `game/wildwest/models/<이름>.mesh` 로 바꾼다. 파일 이름은 원본의 camelCase 를 소문자 `_` 로 바꾼 것이다.
- 목장 건물(`Ranch`)과 기수는 아직 자리 표시다. 말은 CC0 로 걸음새 애니메이션을 갖춘 모델이 없어, 같은 뼈 이름 · 클립 이름을 가진 상자 말을 생성해 두었다
  (`horse_placeholder.gltf`). 진짜 모델이 들어오면 이름을 맞춰 바꾼다(계획 [WildWestHorseMount](../../../docs/plans/WildWestHorseMount.md)).
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl).
