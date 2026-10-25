# 임포트 규칙의 겹침 막기

> 상태: 측정대기 · 선행: 없음 · 영역: editor · 옛번호: C-44 · 진행: 3/3

**남은 확인(통합에서 확인):** 매니페스트 id `import-rule-overlap-test`(코드 · 테스트는 들어갔고 빌드 · 테스트 확인만 남았다). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

`Config/Editor/ModelImportConfig.json` 과 `TextureImportConfig.json` 의 규칙이 "넓은 와일드카드 + 첫 매칭"이라, 새 팩의 파일에 다른 팩의 규칙(배치 오프셋 등)이 적용돼도 경고가 없었다.
credits 표(`CheckResourceCredits`)에 먼저 옮긴 형식을 임포트 규칙에도 옮겼다.

## 1. 결정

- 패턴은 경로 마디 단위다(`AssetImportPathFilter`): `/` 없는 패턴은 파일 이름에만 맞고, 있으면 마디별로 맞춘다. `*` · `?` 는 폴더를 넘지 않고 마디 `**` 만 넘는다. `include_paths` · `exclude_paths` 도 부분 문자열이 아니라 폴더 마디의 연속이다.
- 규칙 선택은 `AssetImportRuleUtil::select` 하나다. 조건 있는 규칙이 둘 이상 맞으면 순서로 고르지 않고 `AssetImportRuleMatch::_error` 를 채운다(`[[nodiscard]]` 결과형이라 호출부가 오류를 무시할 수 없다). 목록의 순서는 결과에 영향을 주지 않는다.
- 조건 없는 규칙은 나머지를 받는 규칙이다. 겹침 검사에서 빼고, 맨 끝에 하나만 둘 수 있다(중간에 있으면 두 설정 모두 로드 오류). 우선순위 필드는 두지 않고 겹침은 `exclude_patterns` 로 풀어 쓴다.
- 호출부(`ModelRawImporter` · `TextureRawImporter`)는 겹친 원본의 해시를 0(읽지 못함)으로 두고 임포트를 같은 오류로 멈춘다. 일괄 임포트의 문제 목록에 오른다.
- 저장소 데이터는 규칙 배정이 바뀌지 않게 다시 썼다(옛 방식과 새 방식으로 모델 155 · 텍스처 1071 원본의 배정을 견주어 같음을 확인했다). 모델 `KitLayoutOffset` 은 `kaykit_space` 를 제외하고, 텍스처 `UI_Textures` 는 `*_n.*` 류를 제외한다(옛 첫 매칭에서 `Normal_Maps` 가 이기던 UI 화살표 19 개의 배정을 그대로 두었다).
- I3(산출물의 결정성): 텍스처는 기존 테스트가 있고, 모델 `.mesh` 는 같은 원본을 두 번 임포트해 바이트를 견주는 테스트를 더했다.

## 2. 남은 것

- 통합에서 매니페스트 `import-rule-overlap-test` 를 돌린다. 저장소 전수 테스트(`RepositoryRaw*MatchAtMostOneRule`, 기존 `RepositoryRaw*Match*`)가 실패하면 데이터를 다시 본다.
- 후보: UI 화살표(`ui/**/arrow_*_n.png`)는 이름이 `_n` 으로 끝날 뿐 노멀 맵이 아니다. 지금은 옛 배정(BC5)을 그대로 두었다. UI 로 옮기면 DDS 와 스탬프를 다시 만들어야 한다.
