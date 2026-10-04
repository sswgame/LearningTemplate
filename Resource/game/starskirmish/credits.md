# StarSkirmish — 에셋 출처

| 폴더 | 원본 | 라이선스 |
|------|------|----------|
| `models_raw/*.glb` | Kenney "Space Kit" (<https://kenney.nl/assets/space-kit>) — 우주선 다섯 · 격납고 셋 · 발전기 · 안테나 · 구조물 · 착륙장 · 포탑 둘 · 결정 · 분화구 | CC0 1.0 |
| `sounds/*.ogg` | Kenney "Interface Sounds" (<https://kenney.nl/assets/interface-sounds>) — `confirmation_003` · `error_003` · `select_004` | CC0 1.0 |
| `sounds/impact_metal_heavy_000.ogg` | Kenney "Impact Sounds" (<https://kenney.nl/assets/impact-sounds>) — `impactMetal_heavy_000` | CC0 1.0 |

- 원본은 `models_raw/` 에 두고, 엔진이 읽는 `models/*.mesh` 는 `App --import-models` 가 만든다(스탬프는 `models_raw/import.stamp`).
  파일 이름은 원본의 camelCase · `-` 를 소문자 `_` 로 바꾼 것이다.
- 원본 glb(UniGLTF 출력)를 두 군데 고쳐 넣었다. 씬 루트로 자식 노드를 적어 glTF 2.0 을 어기므로(cgltf_validate 가 거부) 씬 노드를 진짜 루트
  (`tmpParent`)로, 모델 노드에 든 배치용 오프셋 (2, 0, 1.5) 의 X · Z 를 0 으로 — 모델 가운데가 원점에 오게. 메시는 그대로다.
- 텍스처가 없다 — 색은 키트 머티리얼 색이고 임포터가 정점 색으로 굽는다. 게임은 주인 색을 곱한다.
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl).
