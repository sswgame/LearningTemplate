# CMake Debug Args (VS Code 확장)

CMake 프로젝트의 **명령줄 인자와 전역 변수**를 사이드바에서 골라, CMake Tools 의 디버그 · 실행에 넘깁니다.
어느 CMake 프로젝트에서나 돌고, "무엇을 넘길 수 있는가" 는 **프로필**(JSON)이 소스에서 읽을 규칙으로 정합니다.
프로필이 없으면 사용자 인자 · 환경 변수 · 프리셋 · 실행 · 디버그만 됩니다.

이 저장소(SW 엔진)는 `Profiles/SwEngine.json` 을 씁니다 — `SW_GLOBAL_VARIABLE` 계열(`-gv_*`)과 `ArgumentList.xxx` 의 인자,
RHI 백엔드(쿠킹 표)를 읽고, `SW_SHIPPING_BUILD` · `SW_ACTIVE_GAME` 으로 쓸 수 있는지를 판정합니다. C++ 코드는 건드리지 않습니다.

## 설치 · 시험

Node.js 는 필요 없습니다(VS Code 안의 Node 로 시험하고, VSIX 는 파이썬이 씁니다).

```powershell
py -3 Tools/cmake-debug-args-gui/Scripts/ExtensionTool.py test                 # 단위 시험
py -3 Tools/cmake-debug-args-gui/Scripts/ExtensionTool.py test --integration   # + 격리된 VS Code 에서 통합 시험(CMake Tools 필요)
py -3 Tools/cmake-debug-args-gui/Scripts/ExtensionTool.py package              # dist/cmake-debug-args-gui-<판>.vsix
py -3 Tools/cmake-debug-args-gui/Scripts/ExtensionTool.py install              # 지금 VS Code 에 설치 (Reload Window)
```

옛 판(`yourname.cmake-debug-args-gui` 2.x)이 설치돼 있으면 지우고 설치합니다(확장 id 가 `sw-engine.cmake-debug-args-gui` 로 바뀌었다).

## 쓰는 법

활동 막대의 **CMake Debug Args** → **Launch Arguments**.

| 자리 | 하는 일 |
|------|---------|
| 머리글 | 프로필 · CMake Tools 구성 프리셋 · 프로필 `status`(예: `Dev` · `게임 Empty`) · 실행 대상. 명령줄 미리보기(복사는 셸에 붙여 넣을 수 있게 따옴표 포함), 넣지 못한 값의 이유 |
| 디버그 | 설정 쓰기를 끝낸 뒤 `cmake.debugTarget` |
| 실행 | `cmake.launchTargetPath`(필요하면 빌드)로 대상을 받아 **셸 없이** 띄운다 — 아래 "함정" |
| 전역 변수 탭 | 모듈(과 변형 폴더) 묶음. 체크 = 넘김, 값을 고치면 켜진다. 이름을 누르면 정의한 소스 줄을 연다. ↺ = 기본값 |
| 인자 탭 | 배타 묶음(예: RHI 백엔드)은 고르기 한 칸, 플래그는 체크만, 값 인자는 값 칸 |
| 사용자 · 환경 | 카탈로그에 없는 인자(여러 개를 붙여 넣으면 나누고, 아는 것은 제 칸으로 옮긴다) · 실행 환경 변수 |
| 프리셋 | 지금 선택 전체를 이름 붙여 저장 · 불러오기 · 덮어쓰기 · 지우기 |

검색 · "켜진 것만" · "태그 붙은 것 숨기기"(예: SW 의 `test` 변수)로 거릅니다. 다른 게임의 변수처럼 지금 빌드에 없는 것은 숨기고,
빌드 문맥 규칙에 걸린 것은 이유를 붙입니다(Shipping 에서 등록되지 않는 테스트 변수 · `-EnableEditor` 없이 쓰는 에디터 변수).

### 어디에 쓰는가

- **켜진 것**: `cmake.debugConfig.args` · `cmake.debugConfig.environment`(작업 영역 설정 — 단일 폴더면 `.vscode/settings.json`).
  VS Code 설정 API 로 쓰므로 주석 · 다른 키 · `debugConfig` 의 다른 칸이 그대로 남고, 넘길 것이 없으면 그 칸을 지운다.
  **이 저장소는 `.vscode/settings.json` 을 추적하므로 켜 둔 인자가 git 변경으로 보인다** — 커밋 전에 "모두 끄기" 하거나 그 줄을 빼고 커밋한다.
- **꺼 둔 값 · 사용자 인자 순서 · 프리셋**: `.vscode/cmakeDebugArgsGui.json`(PC 마다, `.gitignore` 됨). 읽지 못하면 덮어쓰지 않고
  `.bad-<시각>.json` 으로 옮겨 두고 알린다.
- 설정을 손으로 고치거나 git pull 로 바뀌면 그 명령줄을 정본으로 읽어 들인다(모르는 글은 사용자 인자로 남는다).

## 프로필

설정 `cmakeDebugArgs.catalog` 에 객체로 적거나, 작업 폴더 기준 JSON 파일 경로로 적습니다. 이 저장소:

```jsonc
"cmakeDebugArgs.catalog": "Tools/cmake-debug-args-gui/Profiles/SwEngine.json"
```

칸의 정의는 `Profiles/ProfileSchema.json`(편집기에서 자동 완성 · 검사), 전체 예는 `Profiles/SwEngine.json`, 엔진과 무관한 작은 예는
`Test/Integration/Fixture/profile.json` 입니다. JSON 키는 snake_case 입니다.

| 칸 | 뜻 |
|----|----|
| `global_variable.files` · `macros` · `argument_index` | 이 glob 의 소스에서 이 매크로 호출을 찾고, 인자 위치(타입 · 이름 · 기본값 · 설명)로 읽는다. 매크로마다 `tag`(배지 · 규칙용) |
| `global_variable.type_map` | C++ 타입 글 → 편집기 종류(`bool` · `int` · `float` · `string` · `enum`). 없는 타입은 같은 이름의 `enum class` 정의를 찾아 고르기로 |
| `global_variable.name_prefix` | 카탈로그가 모르는 키라도 이 접두사면 전역 변수로 읽어 들인다(늦게 등록되는 모듈 변수) |
| `global_variable.default_value_macros` | 기본값이 매크로면 글 또는 JSON 파일의 키로 푼다 |
| `argument.files` · `macro` · `argument_index` | 인자 등록 매크로(이름 · 기본값 · 철자들). 기본값 리터럴의 타입이 값 타입이고 bool 이면 플래그 |
| `argument.exclusive_groups` | 서로 배타인 플래그 묶음. 이름 목록(`arguments`)이나, 표 매크로 자리에 JSON 표의 줄을 펼친다(`table_macro` · `file` · `rows_key` …) |
| `command_line` | `prefix`(기본 `-`) · `separator`(기본 `=`). `-` 로만 된 접두사는 읽을 때 `-` 개수를 가리지 않는다 |
| `module` | `root` 아래 첫 폴더가 묶음. `variant_folders` 는 하나씩만 빌드되는 폴더(게임 · 플러그인)와 그것을 고르는 CMake 캐시 변수 |
| `status` | 머리글에 보일 캐시 값 |
| `rules` | 첫 번째로 맞는 규칙이 변수에 알림을 붙인다: `when`(캐시 변수 참 · 같음) · `match`(`tag` · `module`) · `unless_argument` · `level` · `message` |

캐시 값은 CMake Tools 가 고른 빌드 폴더의 `CMakeCache.txt` 를 먼저, 구성 전이면 프리셋의 `cacheVariables` 를 읽습니다.
프로필 · 정본 파일이 바뀌면 그 파일만 다시 읽습니다(감시하는 glob 도 프로필에서 온다).

## 함정

- **CMake Tools 의 ▷ 실행 + Windows PowerShell 은 점(.)이 든 인자를 쪼갠다.** CMake Tools 는 인자를 따옴표 없이 터미널에 보내고
  PowerShell 5.1 은 `-gv_screenshot=out.ppm` 을 `-gv_screenshot=out` `.ppm` 으로 나눈다. 그런 인자가 있으면 머리글이 알린다 —
  이 확장의 **실행**(셸 없는 작업)이나 **디버그**(인자가 배열로 간다)를 쓴다.
- 이 스크립트를 VS Code 터미널에서 돌리면 `ELECTRON_RUN_AS_NODE` · `VSCODE_*` 가 물려 와 띄운 VS Code 가 Node 로 돈다(종료 코드 9).
  `ExtensionTool.py` 는 그 변수를 빼고 띄운다.
- 웹뷰는 `confirm()` 을 띄우지 못한다 — 지우기 확인은 확장 쪽 모달로 한다.

## 구조

```
package.json                 확장 매니페스트(명령 · 뷰 · 설정 cmakeDebugArgs.catalog)
Source/
  Extension.js               진입점 — CMake 작업 폴더를 골라 잇는다
  LaunchArgumentController.js  카탈로그 · 선택 · 설정 · 상태 파일을 맞춘다(의도 → 선택 → 미뤄 쓰기, 밖의 변경 읽기)
  LaunchArgumentViewProvider.js 웹뷰 공급자(CSP nonce, 카탈로그는 바뀔 때만 보낸다)
  CatalogProfile.js          프로필 읽기 · 기본값 · glob            ┐
  CatalogScanner.js          프로필대로 소스를 읽어 카탈로그        │ vscode 를 모르는 순수 모듈 —
  CppTextUtil.js             주석 지우기 · 매크로 인자 · 리터럴     │ Test/*.js 가 VS Code 의 Node 로 시험한다
  LaunchArgumentUtil.js      선택 ↔ 명령줄 · 값 검사 · 규칙 판정   │
  SelectionStore.js          .vscode/cmakeDebugArgsGui.json        ┘
  BuildContextProvider.js    CMake Tools API(getApi v5) — 프리셋 · 빌드 폴더 · 캐시 값
  DebugConfigWriter.js       cmake.debugConfig 읽기 · 쓰기(설정 API)
  TargetRunner.js            디버그(cmake.debugTarget) · 셸 없는 실행(ProcessExecution)
Webview/                     Index.html · Style.css · Main.js (테마 변수만 쓴다)
Profiles/                    ProfileSchema.json · SwEngine.json
Test/                        단위 시험 · Integration/(확장 호스트 시험 + 엔진과 무관한 Fixture)
Scripts/ExtensionTool.py     test · package · install
```

## 코드 규칙

저장소 규칙([AGENTS.md](../../AGENTS.md))을 JavaScript 에 옮겨 씁니다: 파일 `PascalCase.js`, 클래스 `PascalCase`, 함수 `camelCase`,
멤버 `_camelCase`, bool 은 `b` 접두사(`_bInitialized`), 컨테이너는 `list` · `map` · `unique` 접두사 + 단수(`_listPreset`, `_mapRow`),
상수 `kPascalCase`, 모듈 안 도우미는 `camelCaseInternal`, 동사 표(`initialize` · `get`/`find` · `make`/`create` · `compute`), 비교는
`=== false` 처럼 드러내 적기, 한 줄 `if` 는 중괄호 없이 · 반복문은 늘 중괄호. 선언 위 `/** @brief */` 는 한국어 "~합니다" 체,
본문 `//` 는 "~다" 체, 로그 글은 영어입니다.
