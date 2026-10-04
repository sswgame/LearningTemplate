# HarvestValley — 에셋 출처

| 폴더 | 원본 | 라이선스 |
|------|------|----------|
| `models_raw/*.glb` | Kenney "Nature Kit" (<https://kenney.nl/assets/nature-kit>) — 작물 단계(옥수수 · 잎) · 다 자란 작물(순무 · 당근 · 호박) · 천막 · 울타리 · 나무 · 장작 · 그루터기 · 덤불 · 꽃 | CC0 1.0 |
| `sounds/*.ogg` | Kenney "Interface Sounds" (<https://kenney.nl/assets/interface-sounds>) — `confirmation_001` · `drop_002` · `pluck_001` · `select_002` | CC0 1.0 |

- 원본은 `models_raw/` 에 두고, 엔진이 읽는 `models/*.mesh` 는 `App --import-models` 가 만든다(스탬프는 `models_raw/import.stamp`).
  파일 이름은 원본의 camelCase · `-` 를 소문자 `_` 로 바꾼 것이다.
- 원본 glb(UniGLTF 출력)는 씬 루트로 자식 노드를 적어 glTF 2.0 을 어기므로(cgltf_validate 가 거부) 씬 노드만 진짜 루트(`tmpParent`)로 고쳐 넣었다.
  메시 · 트랜스폼은 그대로다.
- 텍스처가 없다 — 색은 키트 머티리얼 색이고 임포터가 정점 색으로 굽는다.
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl).
