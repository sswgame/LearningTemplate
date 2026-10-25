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
- C++ 마디 매처는 하나다: `AssetImportPathFilter` 안에 있던 구현을 Core `StringUtil::matchesPathPattern` 으로 옮겨 `AssetImportPathFilter` 와 `PackCooker`(`PackConfig` 제외 패턴)가 같은 코드를 쓴다. `PackConfig.json` 제외 패턴 11 개는 모두 `/` 없는 파일 이름 패턴이고, 옛 방식(파일 이름 또는 전체 경로에 `*` 가 `/` 를 넘는 매칭)과 마디 매칭이 `git ls-files` 23 631 파일 × 11 패턴에서 하나도 다르지 않다(그래서 패턴은 그대로). 이제 `PackConfig` 에 슬래시 패턴을 써도 임포트 규칙과 같은 뜻이다. `matchesWildcard` 는 마디 안 글자 매처로 `matchesPathPattern` 이 쓰고(테스트 두 곳도 직접 쓴다) 남겨 두었다. 새 테스트는 `StringUtilTest.MatchesPathPattern` 이다.
- `ConfigCatalog` 의 `pathPattern` · `kListResourceSettingPattern` 과 `ConfigReference.py` 의 대조도 `PathPattern` 으로 바꿨다. 의도된 동작은 패턴을 `**` 로 다시 써 유지했다: `Source/**/*.module.json`(하위 폴더 모듈 85 개), `Resource/game/*/models/**/*.facial.xml`(`avatarsample_d/` 하위), 설정 파일 이름 패턴의 `Resource/**/…`, 폴더 이름이 `settings` 인 곳을 잇는 `Resource/**/settings/*.xml` 한 줄을 더했다(옛 `Resource/*settings*.xml` 이 폴더 이름에도 맞았던 것). 옛 `fnmatch` 와 새 매칭을 저장소 `Config` · `Resource` · `Source` 21 900 파일로 견줘 표 40 줄 각각의 닿는 파일 집합, 첫 매칭으로 고른 줄, 설정 파일 이름 패턴의 합집합(49 개)이 모두 같음을 확인했다. 같은 파일이 두 페이지(`GameSettings` · `PackEngineSettings`)에 오르는 것은 두 줄이 같은 패턴이라 그대로다.
- 에디터 `ProjectSettingsCatalog` 는 `ConfigReference.json` 의 `path` 중 `Config/` · `Resource/` 아래 `.json` 만 읽고, 와일드카드는 마지막 구성요소만 확장했다(폴더 `*` 는 건너뜀). 확장자로만 모아 이름 조건을 보지 않던 것을 `AssetImportPathFilter` 로 거르게 했다(새 매칭 코드 없음). 지금 데이터(`Config/Game/*.json` · `Config/Server/*.json`)에서는 결과가 같고 `**` 가 든 경로는 JSON 이 아니어서 읽지 않는다. 컴파일 확인은 통합에서 한다.
- 저장소 전수 검사 `TestAssetValidation.py` 가 느렸던 원인은 `PathPattern` 이 호출마다 패턴과 경로를 다시 나누던 것(경로 × 패턴 수백만 번)이다. 패턴 해석과 경로 마디 나누기를 캐시해 32 초(프로파일러 35 초)에서 5 초로 줄었고, 옛 구현과 30 패턴 × 7 800 경로에서 결과가 같다. CTest `TIMEOUT` 은 그대로 둔다.
- 데이터 패턴 전수 조사(Scripts 의 `glob` · `rglob` · `fnmatch` · 자체 매처, C++ 의 `matchesWildcard` 호출처): `CheckResourceCredits.py` 만 `credits.md` 표의 패턴을 자체 마디 매처(대소문자 구분, `/` 없는 패턴은 최상위 파일만)로 읽었다. `PathPattern` 으로 바꿨고, 저장소 `Resource` 17 654 파일 × 표 패턴 382 510 쌍에서 맞음 결과가 옛 매처와 하나도 다르지 않다. 나머지는 코드에 박힌 고정 패턴(`RemoveWorktree` 의 쿠킹 스탬프 `fnmatch` 는 `*` 가 `/` 를 넘는 것이 의도, `CheckProductNames` · `LintGate` 예외 키 · `glob("*.h")` 류)이라 두었다. C++ 은 `PackCooker`(위 `PackConfig` 줄)와 `AssetImportPathFilter` 뿐이다.
- 대소문자만 다른 두 파일은 한 이름으로 겹친다(`PathPattern` · `ResourcePaths::normalizePath` 모두 대소문자 무시). `CheckResourceCredits` 게이트가 `Resource` 파일 목록에서 소문자로 같은 경로 쌍을 위반으로 거절한다(저장소 `Resource` · `Config` · `Source` 21 900 파일에 쌍 없음). 대소문자 구분 없는 파일 시스템에서는 두 파일을 시나리오로 만들 수 없어, 쌍 검사를 경로 목록을 받는 순수 함수 `findCaseOnlyDuplicatesInternal` 로 빼고 `Test/PythonTest/TestPathPattern.py` 에서 경로 목록으로 시험한다.
