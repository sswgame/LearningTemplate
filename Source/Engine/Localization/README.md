# Localization — 문자열 표 · 문화권 · 메시지 포맷 · 의사 로컬라이제이션

게임에 보이는 글을 키로 찾아 지금 문화권의 글로 바꾸고, 문화권에 맞춰 인자를 넣어 포맷합니다. 언리얼 Localization Dashboard(String Table ·
culture fallback · ICU 포맷 · 의사 로컬라이즈)와 유니티 Localization 패키지(String Table Collection · Smart String · Pseudo-Locale)의 같은 자리입니다.

| 파일 | 하는 일 |
|---|---|
| `LocalizationManager.*` | 엔진 서비스 — 문화권 표 · 프로젝트를 올리고(엔진 · 게임 범위), 조회 사슬로 찾고, 포맷 · 빠진 키 · 글 판(revision) · 다시 읽기 |
| `LocalizationDocuments.*` | 저작 파일 셋 — 원문 표 `SourceStringTable` · 번역 표 `TranslationTable` · 프로젝트 `LocalizationProject` · 원문 해시 |
| `CultureInfo.*` | 문화권 데이터(`*.cultures.json`) — 복수형 규칙 이름 · 숫자 기호 · 날짜 패턴 · 월 이름 · 쓰기 방향 · 글꼴 대체 목록, 복수형 규칙 등록부 |
| `TextFormatter.*` | ICU MessageFormat 부분 집합 — `{name}` · `plural` · `select` · `number` · `date` · `time` |
| `PseudoLocalizer.*` | 의사 로컬라이제이션(악센트 · 40 % 늘림 · 괄호, 거울 방식은 RLO) — 구문은 지키고 글자 조각만 바꾼다 |
| `LocText.*` | `SW_LOCTEXT( "Namespace", "Key", "Source" )` · `SW_LOCFORMAT( …, arguments )` — 코드의 글 |
| `StringTable.*` | 문화권 하나의 실행 표(키 해시 → 글). 글은 추가 전용 저장소에 있어 돌려준 포인터가 영구히 유효하다 |

## 데이터

```
Resource/engine/localization/
    engine.cultures.json        문화권 표(EngineDefaultAssets::_cultureTable)
    engine.locproject.json      엔진 프로젝트(EngineDefaultAssets::_localizationProject) — 설정 메뉴 글
    engine.strings.json         원문 표(정본)
    ko.translation.json         문화권 번역 표
<팩>/data/localization/<게임>.locproject.json   게임 프로젝트(GameSettings::_localizationProject)
```

- **프로젝트** `{ "name", "sourceCulture", "cultures": [...], "stringTables": [...], "codeRoots": [...], "assetRoots": [...] }` — 표 이름은 프로젝트 옆 상대,
  번역 표는 문화권마다 `<culture>.translation.json`. 폴더를 훑지 않고 이름으로 읽으므로 팩 안에서도 같다.
- **원문 표** `{ "culture": "en", "entries": { "Menu.Start": { "source": "Start", "context": "…", "comment": "…", "maxLength": 12, "origins": [ … ] } } }` —
  `origins` 가 비어 있으면 손으로 넣은 줄(수집기가 지우지 않는다).
- **번역 표** `{ "culture": "ko", "entries": { "Menu.Start": { "text": "시작", "sourceHash": "16 진수 16 자리", "review": true, "translatorComment": "…" } } }` —
  `sourceHash` 가 지금 원문의 해시와 다르면 **낡은(stale)** 번역이고, `review` 는 검토 대기다. 둘 다 화면에 나오지 않고 사슬의 다음으로 떨어진다.
  해시가 없는 줄은 확인하지 않는다(손으로 쓴 표).
- **문화권 표** — `parent` 에서 적지 않은 칸을 물려받는다. `pluralRule` 은 등록부 이름(`none` · `english` · `french` · `russian` · `polish` · `czech` ·
  `arabic`), `digits` 는 `latn` · `arab`, `pseudo` 는 `accented` · `mirrored`. 모르는 칸 · 이름 · 없는 부모 · 순환 · 정본이 아닌 코드(`ko-KR`)는 로드 오류.
- 모든 키 · 칸 이름은 대소문자를 구분해 저장하지만 실행 조회는 `hashed_string` 해시라 대소문자를 무시한다 — 대소문자만 다른 두 키를 두지 말 것.

## 실행

- 기동: `EngineDefaultAssets` 단계가 문화권 표와 엔진 프로젝트를 올리고 `-lang=` 을 적용한다. 게임은 `GameInstanceBase` 가 `GameStrings::initialize`
  (→ `LocalizationManager::initialize`)로 자기 프로젝트를 올린다(앞의 게임 프로젝트는 내린다). 플레이어 설정 `language.text` 가 그 뒤에 다시 적용된다.
- 조회 사슬: 지금 문화권 → 부모(`ko_kr` → `ko`, 표의 `parent`) → 폴백 문화권 → 부모 → 올린 프로젝트들의 원문 문화권. `getLookupChain()`.
- `getString( key )` 는 어디에도 없으면 빠진 키로 한 번 경고하고 기록한다(`getMissingKeys`). `getStringByText` 는 키가 아닐 수 있는 글(대사 원문)용이라 기록하지 않는다.
- 포맷: `formatText( pattern, TextArgumentList().addText( "name", n ).addInteger( "count", c ) )` · `getFormattedString( key, args )` · `SW_LOCFORMAT`.
- 의사 문화권 `qps-ploc` · `qps-plocm` 은 Dev 빌드에서 고를 수 있는 언어에 들어 있다 — 명령줄 `-lang=qps-ploc` 또는 설정 메뉴. 괄호 없이 보이는 글은 하드코딩이다.
- 글꼴: `getFontFallback( culture )` — 문화권(없으면 부모 · 폴백)의 글꼴 가족 목록. 글꼴을 고르고 그리는 것은 UI 쪽이다.
- 다시 읽기: `reloadChangedFile( path )` 는 그 파일이 든 프로젝트를 다시 읽고(실패하면 예전 글을 지킨다) 글 판을 올린 뒤 언어 변경 콜백을 같은 언어로 부른다.

## 주의

- 낱개 표(`setString` · `loadLanguageJson`)는 원문 확인 없이 프로젝트 위에 덮인다 — 시험 · 도구용이다. 게임 데이터는 프로젝트로 올린다.
- 번역 표의 `culture` 는 프로젝트의 `cultures` 철자(정본화 후)와 같아야 한다 — 다르면 그 표를 올리지 않는다.
- `selectordinal` · 화폐 · 시간대 · 서수는 없다. 날짜는 받은 값을 그대로 쓴다(시간대 변환 없음).
