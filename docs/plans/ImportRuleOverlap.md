# 임포트 규칙의 겹침 막기

> 상태: 구현대기 · 선행: 없음 · 영역: editor · 옛번호: C-44 · 진행: 0/3

`Config/Editor/ModelImportConfig.json` 과 `TextureImportConfig.json` 의 규칙은 "넓은 와일드카드 + 첫 매칭"이라, 새 팩의 파일에 다른 팩의 규칙(배치 오프셋 등)이 적용돼도 경고가 없다(`res3` 도입에서 확인, 새 줄을 표 앞에 두어 피했다).
credits 표는 이미 고쳤다 — `CheckResourceCredits` 의 패턴은 경로 마디 단위(`*` 는 폴더를 넘지 않고 `**` 만 넘는다)이고 한 파일에 줄이 둘 이상 맞으면 오류이며, 출처 목록(`*.sources.json`)의 해시 · 목록 밖 파일도 그 게이트가 본다.
이 문서는 임포트 규칙에 같은 형식을 옮기는 일을 나눈다.

## 1. 사실

- 모델: `ModelImportConfig::findMatchingRule` 은 규칙을 순서대로 보고 처음 맞는 것을 돌려준다(`AssetImportPathFilter::matchesPath` — `*` 가 폴더를 넘는 `StringUtil::matchesWildcard`).
  순서가 뜻을 가진 곳이 있다: `StarSkirmish_KayKitSpaceBase` 가 `StarSkirmish_KitLayoutOffset` 보다 먼저 맞아야 한다(뒤 규칙의 `models_raw/*` 가 앞 규칙의 `models_raw/kaykit_space/*` 를 포함한다).
- 텍스처: `TextureImportConfig::findMatchingRule` 도 첫 매칭이고, 마지막 `Fallback_Default` 는 캐치올이다(맨 끝이면 가리는 것이 없다 — `findShadowingRuleIndex`). 캐치올이 "맞지 않은 나머지"를 받는 것은 의도된 쓰임이라
  겹침 검사에서 빼고 따로 다뤄야 한다. 그 밖의 규칙끼리(예: `*splash*` · `*_n.*` · 폴더 규칙)는 서로 겹칠 수 있는지 `Config/Editor/TextureImportConfig.json` 의 12 개 규칙으로 따져야 하고,
  `TestTexturePipeline` 이 일부 우선순위(`hero_preview_n` 이 폴백으로 가는 제외 등)를 고정한다.
- 호출부: 모델은 `ModelImporter.cpp`(해시 계산 · 임포트 두 곳), 텍스처는 `TextureImporter.cpp` 한 곳과 테스트가 부른다.

## 2. 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| I1 | 모델 규칙: 매칭을 경로 마디 단위로(`*` 는 폴더를 넘지 않고 `**` 만 넘는다), 한 원본에 규칙이 둘 이상 맞으면 오류(`findMatchingRule` 이 `Result` 를 돌려준다 — 호출부 둘이 오류를 전한다). 겹치던 규칙은 `exclude_patterns` 로 다시 쓴다(`KitLayoutOffset` 이 `kaykit_space` 를 제외) | `Source/Editor/Common/Asset/`, `Config/Editor/ModelImportConfig.json`, `Test/EditorTest/Common/Asset/` | 없음 | `EditorTest` 의 모델 임포트 테스트 + 저장소의 모든 모델 원본이 규칙 하나에만 맞는지 보는 테스트 | 필요 없음 |
| I2 | 텍스처 규칙: 같은 형식을 적용한다(캐치올은 겹침 검사에서 뺀다). 겹치는 규칙쌍이 나오면 우선순위 필드 없이 `exclude_*` 로 풀어 쓴다. 위 1절의 우선순위 테스트를 새 데이터에 맞춘다 | `Source/Editor/Common/Asset/`, `Config/Editor/TextureImportConfig.json`, `Test/EditorTest/Common/Asset/` | I1 | `EditorTest` 텍스처 파이프라인 테스트 | 필요 없음 |
| I3 | 출처 목록의 해시가 맞으려면 산출물이 결정적이어야 한다. 인코더가 만드는 산출물(텍스처 DDS 변환 등)의 비결정 바이트를 점검한다 — 같은 원본을 두 번 구워 바이트를 견준다 | `Source/Editor/Common/Asset/` | I1 | App 빌드와 실행(같은 원본 두 번 임포트) | 필요 없음 |

I1 의 첫 구현 단위가 작지 않은 까닭: 매칭기 반환형이 바뀌어 호출부 · 테스트 · 데이터가 함께 움직이고, 빌드와 `EditorTest` 로 확인해야 한다.

## 3. 사용자 결정

없습니다. I2 에서 두 규칙이 모두 뜻을 가진 파일이 나오면 어느 규칙을 이기게 할지 데이터로 고르는데, 현재 테스트가 고정한 쪽을 그대로 따른다.
