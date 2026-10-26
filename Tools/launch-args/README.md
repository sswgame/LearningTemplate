# Launch Args — 실행 인자를 클릭으로 고르는 VS Code 확장

## 이것은 무엇이고 왜 있나

게임이나 에디터를 띄울 때마다 `-dx12 -EnableEditor -gv_viewMode=Wireframe …` 같은 인자를 손으로 적는 일을 대신합니다.

- 엔진 소스에 정의된 **전역 변수(`-gv_*`)와 명령줄 인자를 목록으로 보여 줍니다**(설명, 기본값, 타입). 새 전역 변수는 소스를 저장하는 즉시 나타납니다(C++ 코드는 읽기만 한다).
- 체크하고 값을 고르면 VS Code의 **CMake Tools 디버그와 실행**, **`launch.json` 의 CodeLLDB 구성(F5)** 에 그대로 넘어갑니다. 자주 쓰는 조합은 **프리셋**으로 저장합니다.
- 지금 빌드(Debug나 Shipping, 활성 게임)에서 **쓸 수 없는 변수는 이유를 알려 줍니다.**

디버그와 실행 자체는 CMake Tools와 디버거가 하고 이 확장은 "어떤 인자로 띄울지"만 CMake Tools 설정(`cmake.debugConfig`)에 씁니다(이름 · 설정 키 `launchArgs.*` · `launch.json` 필드 `"launchArgs"` 는 `cmake.*` 와 겹치지 않게 지었다). 상태 표시줄의 **`인자 3`** 은 지금 넘기는 인자 수이고 누르면 패널이 열립니다.

## 따라 해 보기

### 설치(처음 한 번)

준비물은 VS Code, **CMake Tools**(`ms-vscode.cmake-tools`), Python 3 입니다(Node.js 불필요). 저장소 루트에서 실행한 뒤 VS Code 에서 **Developer: Reload Window** 를 고르면 활동 막대에 **Launch Args** 아이콘이, 상태 표시줄에 **`인자 없음`** 이 생깁니다. 옛 버전(`yourname.launch-args` 2.x)은 먼저 제거하세요(확장 id 가 `sw-engine.launch-args` 로 바뀜).

```powershell
py -3 Tools/launch-args/Scripts/ExtensionTool.py install
```

### 3분 안에 써 보기

1. CMake Tools 상태 표시줄에서 **구성 프리셋**(예: `Ninja-Debug`)과 **실행 대상**(예: `App`)을 고릅니다.
2. 활동 막대의 **Launch Args** 에서 **인자** 탭의 `RHI 백엔드` 를 `DirectX12 (-dx12)` 로 고르고 `-EnableEditor` 를 체크합니다.
3. **전역 변수** 탭에서 `viewMode` 를 검색해 `gv_viewMode` 를 `Wireframe` 으로 바꿉니다(값을 고치면 자동 체크).
4. 위쪽 **명령줄** 에 `-dx12 -EnableEditor -gv_viewMode=Wireframe` 가 보이면 **CMake Tools 로 디버그** 를 누릅니다(CMake Tools 의 디버그나 Run and Debug 의 "App (CodeLLDB · 사이드바 인자)" 도 된다).

인자를 빼려면 체크를 풀거나 **모두 빼기** 를 누릅니다. **체크된 항목만 명령줄에 넘어갑니다.** 값을 고치면 자동으로 체크되지만 체크는 "고쳤다"는 표시가 아닙니다(기본값 그대로라도 체크하면 넘어가고, 값을 고쳐 두고 체크를 풀면 넘어가지 않으며 값은 기억된다).

| 하고 싶은 것 | 고르는 법 |
|---|---|
| 특정 씬으로 에디터 시작 | `-dx12`, `-EnableEditor` + `gv_editorStartupScene` = `game/empty/maps/editortest.scene.xml` |
| 벤치 큐브 8개를 결정적으로 | `gv_benchMeshes` = `8`, `gv_benchAnimate` = `0` |
| 40 프레임 뒤 자동 종료 | `gv_profileFrames` = `40` |
| 스크린샷 찍기 | `gv_screenshot` = `out.ppm`. 실행은 패널 버튼으로(아래 "함정과 주의") |
| 다른 명령줄을 통째로 가져오기 | **사용자 · 환경** 탭에 한 줄로 붙여 넣기(아는 인자는 해당 항목으로 들어감) |

## 화면 둘러보기

위쪽에 상태 알약(프로필 · 구성 프리셋 · Dev/Shipping · 활성 게임 · 실행 대상), **명령줄**(**복사** 는 PowerShell 용 따옴표까지), 빨간 줄(값이 틀려 뺀 항목과 이유), 노란 줄(점 `.` 이 든 인자의 주의)과 버튼 **CMake Tools 로 디버그**(중단점이 걸림) · **실행 (셸 없이)**(디버거 없이, 필요하면 먼저 빌드) · **모두 빼기** 가 있습니다.
탭은 **전역 변수**(기본은 모듈별 그룹) · **인자**(엔진 명령줄 인자, 하나만 고르는 것은 드롭다운) · **사용자 · 환경**(목록에 없는 인자와 실행 때 줄 환경 변수) · **프리셋**(저장 · 불러오기 · 덮어쓰기 · 지우기)입니다.
검색 · **체크된 항목만** · **정렬** · **분류** 칩(정의한 매크로 종류별 — 이 저장소는 `SW_GLOBAL_VARIABLE` 일반, `SW_TEST_GLOBAL_VARIABLE` 테스트(Shipping 제외), `SW_TEST_GLOBAL_VARIABLE_SHIPPED` 테스트(Shipping 포함)) · **묶기** 로 찾습니다(체크된 항목은 분류 칩으로 숨겨도 보이고 정렬 · 분류 · 그룹 방식은 기억된다).

항목은 `☑ gv_viewMode [RenderViewMode]`(체크하면 넘김, 타입), 값 입력과 ↺(기본값으로), `기본값 Lit`(플래그는 "주지 않음"), 소스에 적힌 설명, 지금 빌드에서의 주의 순입니다. **이름을 클릭**하면 정의한 소스 줄이 열리고 bool 은 처음 켤 때 기본값의 반대가 들어갑니다.
`test` · `test-shipped` 배지는 벤치나 자동화용 변수, 줄이 그어진 이름은 지금 빌드에서 쓸 수 없는 변수입니다(다른 게임 전용 변수는 숨겨진다).

## 작동 원리

### `launch.json` 구성에서 쓰기

`launch.json` 에 직접 적은 디버그 구성은 원래 `args` 로만 뜹니다. 구성에 **`"launchArgs"`** 한 줄을 넣으면 F5 를 누르는 순간 패널의 인자와 환경 변수가 들어갑니다: `"replace"` 는 구성의 `args` 대신 패널의 인자만(패널에서 모두 고를 때), `"append"` 는 구성의 `args` 뒤에 붙입니다(구성이 백엔드와 에디터를 정하고 패널은 전역 변수만 더할 때).
이 저장소의 `.vscode/launch.json` 은 구성 하나를 고른 뒤 사이드바에서 인자를 정하도록 정리되어 있습니다(App · CMake 실행 대상 · 테스트(사이드바 인자 없음) · App 에 붙기, WSL 짝).

<!-- snippet: .vscode/launch.json 의 App 구성 — 5b U7 에서 대조 -->
```jsonc
{
    "name": "App (CodeLLDB · 사이드바 인자)",
    "type": "lldb",
    "request": "launch",
    "program": "${command:cmake.buildDirectory}/Bin/App.exe",   // 프리셋을 바꾸면 그 빌드를 띄운다
    "args": [],
    "launchArgs": "replace",
    "cwd": "${command:cmake.buildDirectory}/Bin"
}
```

- `launchArgs` 가 없는 구성은 건드리지 않습니다. CodeLLDB(`lldb`)뿐 아니라 MS C++ 디버거(`cppdbg`, `cppvsdbg`)에서도 같은 한 줄로 됩니다.
- `append` 구성에 `-dx12` 가 적혀 있으면 패널의 RHI 백엔드는 "지정 안 함"으로 둡니다(아니면 백엔드 인자가 둘).
- 패널의 **CMake Tools 로 디버그** 도 CodeLLDB 로 띄우려면 `"cmake.debugConfig": { "type": "lldb" }` 를 넣습니다(이 저장소 `.vscode/settings.json` 에 있음). CMake Tools 가 MS 디버거 형식으로 넘기는 환경 변수는 확장이 CodeLLDB 형식(`env`)으로 바꿉니다. 무엇이 들어갔는지는 출력 창 **Launch Args** 채널의 `injected [...]` 입니다.

### 무엇이 어디에 저장되나

체크된 인자와 환경 변수는 `.vscode/settings.json` 의 `cmake.debugConfig`(git 에 추적됨), 체크를 푼 항목의 값 · 사용자 인자 · 프리셋은 `.vscode/launchArgs.json`(PC마다 따로, git 무시)입니다. `settings.json` 의 주석과 다른 설정은 건드리지 않고 넘길 것이 없으면 `cmake.debugConfig` 키를 지웁니다.
`settings.json` 을 손으로 고치거나 git pull 로 바뀌면 화면이 따라가고 모르는 인자는 사용자 인자로 남습니다.

### 프로필 — 다른 프로젝트에서 쓰기

확장 코드는 이 엔진을 모릅니다. 소스에서 무엇을 읽을지는 **프로필**(JSON) 하나가 정하므로 다른 CMake 프로젝트에서도 프로필만 쓰면 됩니다(없어도 사용자 인자 · 환경 변수 · 프리셋 · 실행 · 디버그는 쓸 수 있다).
설정 `launchArgs.catalog` 에 프로필 파일 경로나 객체를 적습니다(이 저장소: `"launchArgs.catalog": "Tools/launch-args/Profiles/SwEngine.json"`). 프로필 첫 줄에 `"$schema": "<경로>/ProfileSchema.json"` 을 넣으면 편집기가 자동 완성하고 검사합니다.

- [`Profiles/SwEngine.json`](Profiles/SwEngine.json) — 이 저장소용, [`Test/Integration/Fixture/profile.json`](Test/Integration/Fixture/profile.json) — 엔진과 무관한 작은 프로젝트용(매크로 이름 · 인자 순서 · `--` 접두사가 다름)
- [`Profiles/ProfileSchema.json`](Profiles/ProfileSchema.json) — 모든 필드의 정의(`global_variable`(`files` · `macros` · `type_map` · `name_prefix` · `default_value_macros`), `argument`(`files` · `macro` · `exclusive_groups`), `command_line`, `module`, `status`, `rules`)

`macros` 항목의 `tag`(분류 키)와 `label`(분류 이름)을 분류 칩 · 그룹 · 배지 · 규칙이 씁니다. `type_map` 에 없는 타입은 같은 이름의 `enum class` 를 찾아 드롭다운으로 만들고 인자의 기본값이 `true`/`false` 면 플래그, 숫자면 숫자 입력입니다.
`module.variant_folders`(하나씩만 빌드되는 게임 · 플러그인 폴더)는 CMake 캐시 변수로 고릅니다. CMake 캐시 값은 CMake Tools 가 고른 빌드 폴더의 `CMakeCache.txt` 를 먼저 보고 configure 전이면 프리셋의 `cacheVariables` 를 봅니다.

## 확장하는 법 — 확장 자체를 고칠 때

```powershell
py -3 Tools/launch-args/Scripts/ExtensionTool.py test                 # 단위 테스트
py -3 Tools/launch-args/Scripts/ExtensionTool.py test --integration   # 격리된 VS Code에서 통합 테스트까지(CMake Tools 필요)
py -3 Tools/launch-args/Scripts/ExtensionTool.py package              # dist/launch-args-<버전>.vsix
py -3 Tools/launch-args/Scripts/ExtensionTool.py install              # 지금 VS Code에 설치
```

- 테스트는 VS Code 에 든 Node 로 돌고 VSIX 는 파이썬이 만듭니다. 버전을 올리면 `package.json` 의 `version` 을 바꿉니다.
- `Source/` 의 `CatalogProfile.js`, `CatalogScanner.js`, `CppTextUtil.js`, `LaunchArgumentUtil.js`, `SelectionStore.js` 는 `vscode` 를 모르는 순수 모듈이라 `Test/*.js` 가 단위 테스트하고 나머지는 통합 테스트(`Test/Integration/`)가 확장 호스트에서 확인합니다(진입점 `Extension.js`).
- 통합 테스트 픽스처의 C++ · CMake 파일은 `*.in` 으로 둡니다(커밋 훅이 엔진 코드로 검사하지 않게, 복사할 때 `.in` 을 뗀다). 웹뷰에서는 `confirm()` 이 뜨지 않으므로 지우기 확인은 확장 쪽 모달로 합니다.
- 코드 규칙은 [AGENTS.md](../../AGENTS.md) 를 JavaScript 에 옮깁니다(파일 · 클래스 `PascalCase`, 함수 `camelCase`, 멤버 `_camelCase`, 상수 `kPascalCase`, bool `b` 접두사, 한 줄 `if` 는 중괄호 없이 · 반복문은 항상 중괄호, `/** @brief */` 는 "~합니다" 체 · 본문 `//` 는 "~다" 체 · 로그는 영어).

## 함정과 주의

- **커밋 전에 "모두 빼기"를 누르세요.** 체크된 인자는 git이 추적하는 `.vscode/settings.json` 에 쓰이므로 실수로 커밋될 수 있습니다.
- **점(`.`)이 든 인자는 CMake Tools 상태 표시줄의 ▷(실행)로 띄우지 마세요.** Windows PowerShell 이 `-gv_screenshot=out.ppm` 을 `-gv_screenshot=out` 과 `.ppm` 으로 쪼갭니다. 패널의 **실행 (셸 없이)** 과 **CMake Tools 로 디버그** 는 셸을 거치지 않아 안전합니다.
- **VS Code 터미널에서 VS Code를 띄우면 창 대신 Node로 돕니다**(물려받은 `ELECTRON_RUN_AS_NODE`, 종료 코드 9). `ExtensionTool.py` 는 그 변수를 빼고 띄웁니다. 한국어 Windows 콘솔(cp949)에서 `test` 가 출력 중에 죽으면 `PYTHONIOENCODING=utf-8` 을 줍니다.
- 매크로나 인자 목록, 캐시 규칙을 바꾸면 `Profiles/SwEngine.json` 도 고칩니다.

## 문제 해결

- **목록이 비어 있어요.** 설정 `launchArgs.catalog` 가 프로필을 가리키는지 보고, 그래도 비면 출력 창(`Ctrl+Shift+U`)의 **Launch Args** 채널에 읽지 못한 항목과 이유가 있습니다.
- **새 전역 변수가 안 보여요.** 소스를 저장하면 자동으로 다시 읽고 안 보이면 패널 제목 줄의 새로고침(Rescan Source)을 누릅니다(다른 게임 폴더의 변수는 그 게임이 활성일 때만 보인다).
- **`launch.json` 구성으로 띄웠더니 패널 인자가 안 들어가요.** 그 구성에 `"launchArgs": "replace"` 나 `"append"` 가 있는지 확인합니다.
- **"빌드 문맥 모름".** CMake Tools 에서 아직 configure 하지 않은 빌드입니다(configure 하면 Debug/Shipping 과 활성 게임을 알아낸다).
- **값이 빨갛게 표시돼요.** 타입에 맞지 않는 값이며 그 항목은 명령줄에서 빠져 있다가 고치면 다시 들어갑니다.
- **프리셋과 기억해 둔 값을 모두 지우고 싶어요.** `Ctrl+Shift+P` 에서 **Launch Args: Delete Saved State**(지금 체크된 항목은 남습니다).
