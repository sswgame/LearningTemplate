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
- 저장소 데이터는 규칙 배정이 바뀌지 않게 다시 썼다(옛 방식과 새 방식으로 모델 155 · 텍스처 1071 원본의 배정을 견주어 같음을 확인했다). 모델 `KitLayoutOffset` 은 `kaykit_space` 를 제외하고, 텍스처 `Normal_Maps` 는 UI 폴더(`ui/` · `gui/` · `icons/`)를 제외한다(아래 UI `_n` 이미지 19 개만 `UI_Textures` 로 바뀐다 — 이름이 `_n` 으로 끝날 뿐 노멀 맵이 아니었다).
- I3(산출물의 결정성): 텍스처는 기존 테스트가 있고, 모델 `.mesh` 는 같은 원본을 두 번 임포트해 바이트를 견주는 테스트를 더했다.

## 2. 남은 것

- 통합에서 매니페스트 `import-rule-overlap-test` 를 돌린다. 저장소 전수 테스트(`RepositoryRaw*MatchAtMostOneRule`, 기존 `RepositoryRaw*Match*`)가 실패하면 데이터를 다시 본다.
- 통합에서 UI `_n` 이미지 19 개의 DDS 와 스탬프를 다시 만든다(규칙 배정이 `Normal_Maps`(BC5) 에서 `UI_Textures`(B8G8R8A8) 로 바뀌었다). 목록은 `Resource/engine/textures_raw/ui/` 아래 `kenney_ui/{blue,green,grey,red,yellow}/arrow_{basic,decorative}_n.png` 10 개, `kenney_cursor/{basic,outline}/{arrow,hand_point,hand_small_point,navigation}_n.png` 8 개, `kenney_input_prompts/keyboard_mouse/keyboard_n.png` 1 개다. 명령은 `App --import-textures` 한 번이고(`Resource/engine/textures/ui/…` 의 해당 `.dds` 와 `Resource/engine/textures_raw/import.stamp` 만 바뀌어야 한다), 끝나면 `RepositoryRawTexturesMatchTheirDDS` 가 깨끗해야 한다.

## 3. 같은 모양 정리(임포트 밖 패턴)

- `Scripts/common/AssetValidation.py` 는 `fnmatch` 라 `*` 가 폴더를 넘었다. `Scripts/common/PathPattern.py`(임포트 규칙과 같은 마디 매칭의 Python 쪽)로 바꾸고 `Config/Editor/AssetValidationRules.json` 의 패턴을 `**` 로 다시 썼다. 35 개 규칙의 포함 · 제외 · 대상 파일 집합이 옛 방식과 같음을 저장소 전수(17 654 파일)로 견줬고, `{pack}/*.input.xml` 만 하위 폴더(`data/`)에 닿지 못해 `{pack}/**/*.input.xml` 로 고쳤다. 대소문자는 이제 가리지 않는다.
- `PackConfig` 제외 패턴(`StringUtil::matchesWildcard`)은 `/` 없는 파일 이름 패턴뿐이고 첫 매칭 선택이 없어 같은 결함이 아니다(슬래시 패턴이 생기면 `*` 가 폴더를 넘는다는 점은 헤더에 적혀 있다).
- `ConfigCatalog` 의 `pathPattern` 은 `fnmatch`(`*` 가 `/` 를 넘는다)에 첫 줄 선택이다. 문서 색인이라 같은 파일이 두 쪽(`GameSettings` · `PackEngineSettings`)에 오르는 것이 의도이고 `Source/*.module.json` 이 하위 폴더에 닿는 것에 기대므로 두었다. 바꾸려면 `**` 로 다시 쓰고 `ConfigReference.json` 의 `path` 를 읽는 에디터 쪽을 함께 본다.
