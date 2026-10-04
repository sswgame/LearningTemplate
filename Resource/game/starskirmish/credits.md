# StarSkirmish — 에셋 출처

| 폴더 | 원본 | 라이선스 |
|------|------|----------|
| `models_raw/*.glb` | Kenney "Space Kit" (<https://kenney.nl/assets/space-kit>) — 우주선 다섯 · 격납고 셋 · 발전기 · 안테나 · 구조물 · 착륙장 · 포탑 둘 · 결정 · 분화구 | CC0 1.0 |
| `sounds/*.ogg` | Kenney "Interface Sounds" (<https://kenney.nl/assets/interface-sounds>) — `confirmation_003` · `error_003` · `select_004` | CC0 1.0 |
| `sounds/impact_metal_heavy_000.ogg` | Kenney "Impact Sounds" (<https://kenney.nl/assets/impact-sounds>) — `impactMetal_heavy_000` | CC0 1.0 |

- 원본은 `models_raw/` 에 두고, 엔진이 읽는 `models/*.mesh` 는 `App --import-models` 가 만든다(스탬프는 `models_raw/import.stamp`).
  파일 이름은 원본의 camelCase · `-` 를 소문자 `_` 로 바꾼 것이다.
- 원본 glb 는 내려받은 그대로다. UniGLTF 출력이라 씬 루트로 자식 노드를 적어 glTF 2.0 을 어기지만, 임포터가 그 항목을 맨 위 조상
  (`tmpParent`)으로 바꿔 읽고 경고한다.
- 모델 노드마다 키트 배치용 오프셋 (2, 0, 1.5) 이 들어 있어, 임포트 규칙(`Config/Editor/ModelImportConfig.json` 의 `StarSkirmish_KitLayoutOffset`,
  `translation: [-2, 0, -1.5]`)이 그것을 지워 모델 원점(바닥 가운데)이 엔진 원점에 온다. 경계 상자 중심(`recenter: xz`)은 발전기 · 결정 · 포탑처럼
  모양이 치우친 모델을 최대 6 cm 옮기므로 쓰지 않는다.
- 텍스처가 없다 — 색은 키트 머티리얼 색이고 임포터가 정점 색으로 굽는다. 게임은 주인 색을 곱한다.
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl).
