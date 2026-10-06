# CMake Debug Args — 실행 인자를 클릭으로 고르는 VS Code 확장

게임 · 에디터를 띄울 때마다 `-dx12 -EnableEditor -gv_viewMode=2 …` 같은 인자를 손으로 적고 있었다면, 이 확장이 그 일을 대신합니다.

- 엔진 소스에 정의된 **전역 변수(`-gv_*`)와 커맨드라인 인자를 목록으로 보여 줍니다.** 설명 · 기본값 · 타입이 함께 나옵니다.
- 체크하고 값을 고르면 VS Code 의 **CMake Tools 디버그(F5) · 실행에 그대로 넘어갑니다.**
- 자주 쓰는 조합은 **프리셋**으로 저장해 두었다가 한 번에 불러옵니다.
- 지금 빌드(Debug · Shipping, 활성 게임)에서 **쓸 수 없는 변수는 이유를 알려 줍니다.**

> 이 확장은 C++ 코드를 읽기만 하고 바꾸지 않습니다. 엔진에 새 전역 변수를 추가하면 저장하는 즉시 목록에 나타납니다.

---

## 1. 설치 (처음 한 번)

준비물: VS Code, **CMake Tools** 확장(`ms-vscode.cmake-tools`), Python 3. Node.js 는 필요 없습니다.

저장소 루트에서 아래를 실행한 뒤, VS Code 에서 `Ctrl+Shift+P` → **Developer: Reload Window**.

```powershell
py -3 Tools/cmake-debug-args-gui/Scripts/ExtensionTool.py install
```

왼쪽 활동 막대에 **CMake Debug Args** 아이콘(벌레 모양)이 생기면 설치된 것입니다.

> 예전 판(`yourname.cmake-debug-args-gui` 2.x)을 쓰고 있었다면 확장 목록에서 먼저 제거하세요. 확장 id 가 `sw-engine.cmake-debug-args-gui` 로 바뀌었습니다.

## 2. 3분 안에 써 보기

1. CMake Tools 상태 표시줄에서 **구성 프리셋**(예: `Ninja-Debug`)과 **실행 대상**(예: `App`)을 평소처럼 고릅니다.
2. 활동 막대의 **CMake Debug Args** 를 엽니다.
3. **인자** 탭 → `RHI 백엔드` 에서 `DirectX12 (-dx12)` 를 고르고, `-EnableEditor` 를 체크합니다.
4. **전역 변수** 탭 → 검색창에 `viewMode` → `gv_viewMode` 의 값을 `2` 로 바꿉니다(값을 고치면 자동으로 체크됩니다).
5. 위쪽 **명령줄** 상자에 `-dx12 -EnableEditor -gv_viewMode=2` 가 보이는지 확인합니다.
6. **디버그** 버튼(또는 평소처럼 CMake Tools 의 디버그)을 누르면 이 인자로 App 이 뜹니다.

끝입니다. 끄고 싶으면 체크를 풀거나 **모두 끄기** 를 누릅니다.

## 3. 화면 둘러보기

### 위쪽 — 요약

| 무엇 | 설명 |
|------|------|
| 상태 알약 | 프로필 이름 · 구성 프리셋 · `Dev`/`Shipping` · `게임 Empty` · 실행 대상. 빌드를 바꾸면 따라 바뀝니다 |
| 명령줄 | 지금 넘길 인자 전부. **복사** 는 PowerShell 에 그대로 붙여 넣을 수 있게 따옴표까지 넣어 복사합니다 |
| 빨간 줄 | 값이 틀려서 **명령줄에서 뺀** 항목과 그 이유(예: `-W: 정수여야 합니다`) |
| 노란 줄 | 점(`.`)이 든 인자가 있을 때의 주의 — 아래 [자주 묻는 것](#6-자주-묻는-것--문제-해결) 참고 |
| **디버그** | CMake Tools 디버그를 시작합니다(중단점이 걸립니다) |
| **실행** | 디버거 없이 실행합니다. 필요하면 먼저 빌드합니다 |
| **모두 끄기** | 전부 끕니다. 입력해 둔 값은 기억합니다 |

### 탭

| 탭 | 무엇을 하나 |
|----|-------------|
| **전역 변수** | `-gv_*` 변수. 기본은 엔진 · 앱 · 에디터 · 게임 등 모듈별로 묶여 있습니다 |
| **인자** | 엔진 커맨드라인 인자(`-W`, `-EnableEditor`, `-cook-shaders` …). RHI 백엔드처럼 하나만 고르는 것은 드롭다운입니다 |
| **사용자 · 환경** | 목록에 없는 인자를 직접 적거나, 실행할 때 줄 **환경 변수**를 넣습니다 |
| **프리셋** | 지금 고른 것 전체를 이름 붙여 저장 · 불러오기 · 덮어쓰기 · 지우기 |

### 찾기 · 분류 · 정렬 (목록 탭 위쪽)

| 도구 | 하는 일 |
|------|---------|
| 검색 | 이름 · 설명 · 철자에 그 글이 든 항목만 보입니다 |
| 켜진 것만 | 지금 넘기는 항목만 보입니다 |
| **정렬** | `이름 ↑` · `이름 ↓` · `켜진 것 먼저` · `소스 순서`(정의된 파일 · 줄 순서). 묶음 안에서 정렬됩니다 |
| **분류** 칩 | 매크로 종류별로 보이기 · 숨기기 — 이 저장소는 `일반`(`SW_GLOBAL_VARIABLE`) · `테스트 (Shipping 제외)`(`SW_TEST_GLOBAL_VARIABLE`) · `테스트 (Shipping 포함)`(`SW_TEST_GLOBAL_VARIABLE_SHIPPED`). 칩 옆 숫자는 그 분류의 변수 수 |
| **묶기** | `모듈별`(엔진 · 에디터 · 게임 …) · `분류별`(위 분류) · `묶지 않음`(한 목록 — 이름 정렬과 함께 쓰면 가나다 · ABC 순 전체 목록) |

켜 둔 항목은 분류 칩으로 숨겨도 계속 보입니다(넘기고 있는 것이 안 보이면 헷갈리므로). 고른 정렬 · 분류 · 묶기는 다음에 열어도 그대로입니다.

### 항목 한 줄 읽는 법

```
☑ gv_viewMode   [int32]                 ← 체크 = 넘긴다 · 타입
  [ 2          ]  기본 0  ↺             ← 값 · 소스의 기본값 · 기본값으로 되돌리기
  씬 보기 방식 (0 Lit / 1 Unlit / 2 Wireframe)   ← 소스에 적힌 설명
  Shipping 에서는 등록되지 않는 테스트용 변수입니다  ← (있을 때만) 지금 빌드에서의 주의
```

- **이름을 클릭**하면 그 변수를 정의한 소스 줄이 열립니다.
- 값 칸을 고치면 자동으로 체크됩니다. bool 은 처음 켤 때 기본값의 반대가 들어갑니다.
- `test` · `test-shipped` 배지는 벤치 · 자동화용 변수라는 뜻입니다(마우스를 올리면 분류 이름). 위쪽 **분류** 칩으로 감추거나 **묶기: 분류별** 로 모아 볼 수 있습니다.
- 줄이 그어진 이름은 지금 빌드에서 쓸 수 없는 변수입니다(예: Shipping 에서 빠지는 테스트 변수). 다른 게임 전용 변수는 아예 숨겨집니다.

## 4. 자주 쓰는 예

| 하고 싶은 것 | 고르는 법 |
|--------------|-----------|
| 에디터를 DX12 로 | 인자: RHI 백엔드 `-dx12`, `-EnableEditor` 체크 |
| 특정 씬으로 에디터 시작 | 위 + 전역 변수 `gv_editorStartupScene` = `game/empty/maps/editortest.scene.xml` |
| 와이어프레임으로 보기 | 전역 변수 `gv_viewMode` = `2` |
| 벤치 큐브 8개, 결정적으로 | `gv_benchMeshes` = `8`, `gv_benchAnimate` = `0` |
| 40 프레임 뒤 자동 종료 | `gv_profileFrames` = `40` |
| 스크린샷 찍기 | `gv_screenshot` = `out.ppm` (이 경우 **실행 · 디버그 버튼**을 쓰세요 — 6절) |
| 다른 명령줄을 통째로 가져오기 | 사용자 · 환경 탭에 `-dx11 -gv_viewMode=1` 처럼 한 줄로 붙여 넣기 → 아는 인자는 알아서 제 칸으로 들어갑니다 |

마음에 드는 조합은 **프리셋** 탭에서 이름을 붙여 저장하세요(예: `벤치 8 · 와이어`).

## 5. 무엇이 어디에 저장되나

| 내용 | 저장 위치 | 공유 |
|------|-----------|------|
| **켜 둔** 인자 · 환경 변수 | `.vscode/settings.json` 의 `cmake.debugConfig` (CMake Tools 가 읽는 곳) | git 에 추적됨 |
| 꺼 둔 값 · 사용자 인자 · 프리셋 | `.vscode/cmakeDebugArgsGui.json` | PC 마다 따로(git 무시) |

- `settings.json` 의 주석과 다른 설정은 건드리지 않습니다. 넘길 것이 없으면 `cmake.debugConfig` 키를 지웁니다.
- **커밋 전에 "모두 끄기" 를 누르세요.** 켜 둔 인자가 `settings.json` 변경으로 보여 실수로 커밋될 수 있습니다.
- `settings.json` 을 손으로 고치거나 git pull 로 바뀌면 화면이 그 내용을 따라갑니다(모르는 인자는 사용자 인자로 남습니다).

## 6. 자주 묻는 것 · 문제 해결

**목록이 비어 있어요.**
설정 `cmakeDebugArgs.catalog` 가 프로필을 가리키는지 보세요. 이 저장소에서는 이미 들어 있습니다:
`"cmakeDebugArgs.catalog": "Tools/cmake-debug-args-gui/Profiles/SwEngine.json"`.
그래도 비면 출력 창(`Ctrl+Shift+U`)에서 **CMake Debug Args** 채널을 보세요. 읽지 못한 항목과 이유가 적혀 있습니다.

**새로 추가한 전역 변수가 안 보여요.**
소스를 저장하면 자동으로 다시 읽습니다. 그래도 안 보이면 패널 제목 줄의 새로고침(Rescan Source)을 누르세요.
다른 게임 폴더(`Source/Games/<게임>`)의 변수는 그 게임이 활성일 때만 보입니다.

**`gv_screenshot=out.ppm` 이 이상하게 들어가요.** *(노란 경고가 뜰 때)*
Windows PowerShell 은 `-` 로 시작하고 점(`.`)이 든 인자를 `-gv_screenshot=out` 과 `.ppm` 으로 **쪼갭니다.**
CMake Tools 상태 표시줄의 ▷(실행)가 이렇게 깨집니다. 이 패널의 **실행** · **디버그** 버튼은 셸을 거치지 않아 안전합니다.

**"빌드 문맥 모름" 이라고 나와요.**
CMake Tools 에서 아직 구성(configure)을 하지 않은 빌드입니다. 구성하면 Debug/Shipping · 활성 게임을 알아내 판정이 붙습니다.

**값이 빨갛게 표시돼요.**
타입에 맞지 않는 값입니다(정수 칸에 글자 등). 그 항목은 명령줄에서 빠져 있으니 고치면 다시 들어갑니다.

**프리셋 · 기억해 둔 값을 모두 지우고 싶어요.**
`Ctrl+Shift+P` → **CMake Debug Args: Delete Saved State**. 지금 켜진 인자는 남습니다.

---

## 7. 다른 프로젝트에서 쓰기 — 프로필

확장 코드는 이 엔진을 모릅니다. "소스에서 무엇을 읽을지" 는 **프로필**(JSON) 하나가 정하므로, 다른 CMake 프로젝트에서도 프로필만 쓰면 됩니다.
프로필이 없어도 사용자 인자 · 환경 변수 · 프리셋 · 실행 · 디버그는 그대로 쓸 수 있습니다.

설정 `cmakeDebugArgs.catalog` 에 프로필 파일 경로(작업 폴더 기준)나 객체를 적습니다.
프로필 파일 첫 줄에 `"$schema": "<경로>/ProfileSchema.json"` 을 넣으면 편집기가 칸을 자동 완성 · 검사해 줍니다.

참고할 예:
- [`Profiles/SwEngine.json`](Profiles/SwEngine.json) — 이 저장소용(전체 기능)
- [`Test/Integration/Fixture/profile.json`](Test/Integration/Fixture/profile.json) — 엔진과 무관한 작은 프로젝트용(매크로 이름 · 인자 순서 · `--` 접두사가 다르다)
- [`Profiles/ProfileSchema.json`](Profiles/ProfileSchema.json) — 모든 칸의 정의

| 칸 | 뜻 |
|----|----|
| `global_variable.files` · `macros` · `argument_index` | 이 파일들(glob)에서 이 매크로 호출을 찾고, 몇 번째 인자가 타입 · 이름 · 기본값 · 설명인지. 매크로마다 `tag`(분류 키 — 칩 · 묶기 · 배지 · 규칙에 쓴다)와 `label`(화면에 보일 분류 이름) |
| `global_variable.type_map` | C++ 타입 → 편집기 종류(`bool` · `int` · `float` · `string` · `enum`). 표에 없는 타입은 같은 이름의 `enum class` 를 찾아 드롭다운으로 |
| `global_variable.name_prefix` | 목록에 없는 키라도 이 접두사(`gv_`)로 시작하면 전역 변수로 읽습니다 |
| `global_variable.default_value_macros` | 기본값이 매크로일 때 그 값을 글이나 JSON 파일의 키로 풉니다 |
| `argument.files` · `macro` · `argument_index` | 인자 등록 매크로(이름 · 기본값 · 철자). 기본값이 `true/false` 면 플래그, 숫자면 숫자 칸 |
| `argument.exclusive_groups` | 하나만 고르는 플래그 묶음(드롭다운). 인자 이름을 나열하거나 JSON 표에서 펼칩니다 |
| `command_line` | 명령줄 모양 — `prefix`(기본 `-`) · `separator`(기본 `=`) |
| `module` | 목록을 묶는 기준 폴더(`root`)와, 하나씩만 빌드되는 폴더(`variant_folders` — 게임 · 플러그인)와 그것을 고르는 CMake 캐시 변수 |
| `status` | 위쪽 상태 알약에 보일 CMake 캐시 값 |
| `rules` | 조건(`when` 캐시 변수 · `match` 태그 · 모듈 · `unless_argument`)이 맞으면 변수에 알림(`info` · `warning` · `unavailable`)을 붙입니다 |

CMake 캐시 값은 CMake Tools 가 고른 빌드 폴더의 `CMakeCache.txt` 를 먼저, 아직 구성 전이면 프리셋의 `cacheVariables` 를 봅니다.

## 8. 확장을 고치는 사람에게

```powershell
py -3 Tools/cmake-debug-args-gui/Scripts/ExtensionTool.py test                 # 단위 시험
py -3 Tools/cmake-debug-args-gui/Scripts/ExtensionTool.py test --integration   # + 격리된 VS Code 에서 통합 시험(CMake Tools 필요)
py -3 Tools/cmake-debug-args-gui/Scripts/ExtensionTool.py package              # dist/cmake-debug-args-gui-<판>.vsix
py -3 Tools/cmake-debug-args-gui/Scripts/ExtensionTool.py install              # 지금 VS Code 에 설치
```

- 시험은 VS Code 안에 든 Node 로 돌고, VSIX 는 파이썬이 직접 만듭니다. 판을 올리면 `package.json` 의 `version` 을 바꿉니다.
- VS Code 터미널에서 VS Code 를 띄우면 물려받은 `ELECTRON_RUN_AS_NODE` 때문에 창 대신 Node 로 돕니다(종료 코드 9). `ExtensionTool.py` 는 그 변수를 빼고 띄웁니다.
- 통합 시험 픽스처의 C++ · CMake 파일은 `*.in` 으로 둡니다. 저장소의 커밋 훅이 엔진 코드로 보고 검사하지 않게 하려는 것이고, 시험이 복사할 때 `.in` 을 뗍니다.
- 웹뷰에서는 `confirm()` 이 뜨지 않으므로 지우기 확인은 확장 쪽 모달로 합니다.

### 구조

```
package.json                    확장 매니페스트(명령 · 뷰 · 설정 cmakeDebugArgs.catalog)
Source/
  Extension.js                  진입점 — CMake 작업 폴더를 골라 잇는다
  LaunchArgumentController.js   카탈로그 · 선택 · 설정 · 상태 파일을 맞춘다(화면 의도 → 선택 → 미뤄 쓰기, 밖의 변경 읽기)
  LaunchArgumentViewProvider.js 웹뷰 공급자(CSP nonce, 카탈로그는 바뀔 때만 보낸다)
  CatalogProfile.js             프로필 읽기 · 기본값 · glob            ┐
  CatalogScanner.js             프로필대로 소스를 읽어 카탈로그        │ vscode 를 모르는 순수 모듈 —
  CppTextUtil.js                주석 지우기 · 매크로 인자 · 리터럴     │ Test/*.js 가 시험한다
  LaunchArgumentUtil.js         선택 ↔ 명령줄 · 값 검사 · 규칙 판정   │
  SelectionStore.js             .vscode/cmakeDebugArgsGui.json        ┘
  BuildContextProvider.js       CMake Tools API — 프리셋 · 빌드 폴더 · 캐시 값
  DebugConfigWriter.js          cmake.debugConfig 읽기 · 쓰기(설정 API, 주석 보존)
  TargetRunner.js               디버그(cmake.debugTarget) · 셸 없는 실행
Webview/                        Index.html · Style.css · Main.js (VS Code 테마 색만 쓴다)
Profiles/                       ProfileSchema.json · SwEngine.json
Test/                           단위 시험 · Integration/(확장 호스트 시험 + 엔진과 무관한 Fixture)
Scripts/ExtensionTool.py        test · package · install
```

### 코드 규칙

저장소 규칙([AGENTS.md](../../AGENTS.md))을 JavaScript 에 옮겨 씁니다: 파일 `PascalCase.js`, 클래스 `PascalCase`, 함수 `camelCase`,
멤버 `_camelCase`, bool 은 `b` 접두사(`_bInitialized`), 컨테이너는 `list` · `map` · `unique` 접두사 + 단수(`_listPreset`, `_mapRow`),
상수 `kPascalCase`, 모듈 안 도우미는 `camelCaseInternal`, 동사 표(`initialize` · `get`/`find` · `make`/`create` · `compute`), 비교는
`=== false` 처럼 드러내 적기, 한 줄 `if` 는 중괄호 없이 · 반복문은 늘 중괄호. 선언 위 `/** @brief */` 는 한국어 "~합니다" 체,
본문 `//` 는 "~다" 체, 로그 글은 영어입니다.
