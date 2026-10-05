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
| `TextGatherer.*` | 글 수집 — 코드 스캐너(`SW_LOCTEXT` · `SW_LOCFORMAT`) · 리플렉션 XML(`Meta = "Localizable"`) · 하드코딩 의심 경고 · 원문 표 합치기 |
| `TranslationMemory.*` | 번역 메모리(`tm/<culture>.tm.json`) — 같은 원문 · 정규화 · 편집 거리 근사 일치 |
| `PortableObjectFile.*` | gettext PO 읽기 · 쓰기(msgctxt 키 · `#.` · `#:` · `#` 번역가 메모 · `#, fuzzy` · `#|` 옛 원문) |

수집 · 교환 명령의 본문은 `DevTools/LocalizationTools.*` 다(대화 에셋 · 게임 설정을 함께 보므로 Localization(티어 2)이 아니라 개발 도구(티어 7)에 있다).

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

## 도구 — 수집 · 교환 (`App` 헤드리스, Dev · 소스 트리 필요)

```powershell
cd build/Ninja-Debug/Bin
./App.exe --gather-text                     # 엔진 + 활성 게임 프로젝트: 코드 · 데이터 → 원문 표, 번역 표 해시 · 메모리 채우기 · 검사
./App.exe --check-text                      # 쓰지 않고 표가 최신인지만(종료 코드) — CI
./App.exe --gather-text -loc-project=game/shooter3d/data/localization/shooter3d.locproject.json
./App.exe --gather-text -loc-project=all    # 엔진 + 모든 게임 팩
./App.exe --export-po                       # 문화권마다 <프로젝트 폴더>/po/<culture>.po
./App.exe -import-po=<번역가가 돌려준 .po>    # 머리의 X-Localization-Project 로 프로젝트를 고른다(-loc-project 로 지정 가능)
```

- **코드**: `SW_LOCTEXT( "Namespace", "Key", "Source" )` 의 세 인자는 문자열 리터럴(이어 붙이기 · 이스케이프 · 날 문자열 가능). 리터럴이 아니면 `파일:줄` 오류.
  주석 · 문자열 · `#define` 줄 안의 것은 건너뛴다. 같은 키에 원문이 둘이면 오류.
- **리플렉션 데이터**: `PROPERTY( Meta = "Localizable" )`(최대 길이는 `Meta = "Localizable, MaxLength=24"`) 문자열 프로퍼티의 값이 **키이거나 글 그대로**다 —
  어느 표에 그 키가 있으면 참조, 없으면 글 자체가 키가 된다(gettext 와 같다. 글을 고치면 새 키가 되고 옛 번역은 번역 메모리가 근사 일치로 넘긴다).
  실행에서는 `LocalizationManager::getStringByText( value, value )` 로 찾는다(대화가 그렇게 한다). 표시 없는 문자열 프로퍼티에 낱말 둘 이상의 문장이 있으면
  하드코딩 의심 경고 — 사람이 읽는 글이 아니면 `Meta = "NotLocalizable"`.
- **손으로 읽는 XML**(아이템 카탈로그 · 설정 스키마): 프로젝트의 `assetRules` — `{ "files": "items.xml", "elements": [ "Item" ], "attribute": "name", "kind": "text", "context": "Item name" }`,
  `kind: "key"` 는 키 참조(어느 표에 있어야 한다 — 없으면 오류). 대화(`.dialogue.json`)의 화자 · 대사 · 선택지는 늘 모은다.
- **합치기**: 첫 원문 표(`stringTables[0]`)에 더해짐 · 바뀜 · 지워짐. 지우는 것은 수집기가 넣었던 줄(`origins` 가 있는 줄)뿐이다.
- **번역 표**: 해시 없는 번역에 지금 원문 해시를 찍고(손으로 쓴 번역을 받아들인다), 지금 번역을 번역 메모리에 쌓고, 원문이 사라진 키의 번역을 빼고, 없는 번역은 메모리의
  같은 원문(그대로) · 비슷한 원문(검토 표시)으로, 낡은 번역은 같은 원문일 때만 채운다. 자리표시자 이름 차이 · 최대 길이 초과 · 구문 오류를 보고한다.
- **PO**: 가져온 번역의 해시는 그 msgid(번역가가 본 원문)의 해시다 — 그 사이 원문이 바뀌었으면 낡은 번역으로 남는다. `#, fuzzy` 는 검토 표시, `# ` 줄은 번역가 메모(왕복 유지).
- `TextGathererTest.RepositoryProjectsAreUpToDate` 가 저장소의 프로젝트가 최신인지 본다 — 글을 고치면 `--gather-text` 결과를 같이 커밋한다.

## 실행

- 기동: `EngineDefaultAssets` 단계가 문화권 표와 엔진 프로젝트를 올리고 `-lang=` 을 적용한다. 게임은 `GameInstanceBase` 가 `GameStrings::initialize`
  (→ `LocalizationManager::initialize`)로 자기 프로젝트를 올린다(앞의 게임 프로젝트는 내린다). 플레이어 설정 `language.text` 가 그 뒤에 다시 적용된다.
- 조회 사슬: 지금 문화권 → 부모(`ko_kr` → `ko`, 표의 `parent`) → 폴백 문화권 → 부모 → 올린 프로젝트들의 원문 문화권. `getLookupChain()`.
- `getString( key )` 는 어디에도 없으면 빠진 키로 한 번 경고하고 기록한다(`getMissingKeys`). `getStringByText` 는 키가 아닐 수 있는 글(대사 원문)용이라 기록하지 않는다.
- 포맷: `formatText( pattern, TextArgumentList().addText( "name", n ).addInteger( "count", c ) )` · `getFormattedString( key, args )` · `SW_LOCFORMAT`.
- 의사 문화권 `qps-ploc` · `qps-plocm` 은 Dev 빌드에서 고를 수 있는 언어에 들어 있다 — 명령줄 `-lang=qps-ploc` 또는 설정 메뉴. 괄호 없이 보이는 글은 하드코딩이다.
- 글꼴: `getFontFallback( culture )` — 문화권(없으면 부모 · 폴백)의 글꼴 가족 목록. 글꼴을 고르고 그리는 것은 UI 쪽이다.
- 다시 읽기: `reloadChangedFile( path )` 는 그 파일이 든 프로젝트를 다시 읽고(실패하면 예전 글을 지킨다) 글 판을 올린 뒤 언어 변경 콜백을 같은 언어로 부른다.
  에디터 핫 리로드는 `.strings.json` · `.translation.json` · `.locproject.json` 을 에셋 캐시 "StringTable"(`Resource/LocalizationReloadCache`)로 보내 이 길을 탄다.
  에디터 Data Table 패널(Localization 탭)은 프로젝트(엔진 · 게임)를 골라 원문 · 문화권 번역을 한 표로 고치고, 저장하면 같은 길로 다시 읽힌다.

## 주의

- 낱개 표(`setString` · `loadLanguageJson`)는 원문 확인 없이 프로젝트 위에 덮인다 — 시험 · 도구용이다. 게임 데이터는 프로젝트로 올린다.
- 번역 표의 `culture` 는 프로젝트의 `cultures` 철자(정본화 후)와 같아야 한다 — 다르면 그 표를 올리지 않는다.
- `selectordinal` · 화폐 · 시간대 · 서수는 없다. 날짜는 받은 값을 그대로 쓴다(시간대 변환 없음).
