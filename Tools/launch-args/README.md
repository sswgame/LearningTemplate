# Launch Args — 실행 인자를 클릭으로 고르는 VS Code 확장

## 이것은 무엇이고 왜 있나

게임이나 에디터를 띄울 때마다 `-dx12 -EnableEditor -gv_viewMode=2 …` 같은 인자를 손으로 적고 있었다면, 이 확장이 그 일을 대신합니다.

- 엔진 소스에 정의된 **전역 변수(`-gv_*`)와 명령줄 인자를 목록으로 보여 줍니다.** 설명, 기본값, 타입이 함께 나옵니다.
- 체크하고 값을 고르면 VS Code의 **CMake Tools 디버그와 실행**, 그리고 **`launch.json` 의 CodeLLDB 구성(F5)** 에 그대로 넘어갑니다.
- 자주 쓰는 조합은 **프리셋**으로 저장해 두었다가 한 번에 불러옵니다.
- 지금 빌드(Debug나 Shipping, 활성 게임)에서 **쓸 수 없는 변수는 이유를 알려 줍니다.**

이 확장은 C++ 코드를 읽기만 하고 바꾸지 않습니다. 엔진에 새 전역 변수를 추가하면 소스를 저장하는 즉시 목록에 나타납니다.

**CMake Tools와의 관계.** 디버그와 실행 자체는 CMake Tools와 디버거(CodeLLDB 등)가 합니다. Launch Args는 "어떤 인자로 띄울지"만 정해서 CMake Tools 설정(`cmake.debugConfig`)에 써 줍니다.
그래서 이름, 설정 키(`launchArgs.*`), `launch.json` 필드(`"launchArgs"`)를 CMake Tools의 `cmake.*` 와 겹치지 않게 지었습니다.
상태 표시줄의 **`인자 3`** 항목은 지금 넘기는 인자 수입니다. 마우스를 올리면 명령줄 전체가 보이고, 누르면 패널이 열립니다.

## 따라 해 보기

### 설치(처음 한 번)

준비물은 VS Code, **CMake Tools** 확장(`ms-vscode.cmake-tools`), Python 3입니다. Node.js는 필요 없습니다.
저장소 루트에서 아래를 실행한 뒤, VS Code에서 `Ctrl+Shift+P` 를 누르고 **Developer: Reload Window** 를 고릅니다.

```powershell
py -3 Tools/launch-args/Scripts/ExtensionTool.py install
```

왼쪽 활동 막대에 **Launch Args** 아이콘이, 아래 상태 표시줄에 **`인자 없음`** 항목이 생기면 설치된 것입니다.
예전 버전(`yourname.launch-args` 2.x)을 쓰고 있었다면 확장 목록에서 먼저 제거하세요. 확장 id가 `sw-engine.launch-args` 로 바뀌었습니다.

### 3분 안에 써 보기

1. CMake Tools 상태 표시줄에서 **구성 프리셋**(예: `Ninja-Debug`)과 **실행 대상**(예: `App`)을 평소처럼 고릅니다.
2. 활동 막대의 **Launch Args** 를 엽니다.
3. **인자** 탭의 `RHI 백엔드` 에서 `DirectX12 (-dx12)` 를 고르고, `-EnableEditor` 를 체크합니다.
4. **전역 변수** 탭의 검색창에 `viewMode` 를 치고, `gv_viewMode` 의 값을 `2` 로 바꿉니다. 값을 고치면 자동으로 체크됩니다.
5. 위쪽 **명령줄** 상자에 `-dx12 -EnableEditor -gv_viewMode=2` 가 보이는지 확인합니다.
6. **CMake Tools 로 디버그** 버튼을 누르면 이 인자로 App이 뜹니다. 평소처럼 CMake Tools의 디버그나, Run and Debug의 "App (CodeLLDB · 사이드바 인자)" 구성을 써도 됩니다.

인자를 빼고 싶으면 체크를 풀거나 **모두 빼기** 를 누릅니다.

**체크된 항목만 명령줄에 넘어갑니다.** 값을 고치면 자동으로 체크되지만, 체크는 "고쳤다"는 표시가 아닙니다.
기본값 그대로라도 체크하면 넘어가고, 값을 고쳐 두었어도 체크를 풀면 넘어가지 않습니다. 이때 값은 기억해 둡니다.

### 자주 쓰는 예

| 하고 싶은 것 | 고르는 법 |
|---|---|
| 에디터를 DX12로 | 인자 `-dx12`, `-EnableEditor` 체크 |
| 특정 씬으로 에디터 시작 | 위에 더해 `gv_editorStartupScene` = `game/empty/maps/editortest.scene.xml` |
| 와이어프레임으로 보기 | `gv_viewMode` = `2` |
| 벤치 큐브 8개를 결정적으로 | `gv_benchMeshes` = `8`, `gv_benchAnimate` = `0` |
| 40 프레임 뒤 자동 종료 | `gv_profileFrames` = `40` |
| 스크린샷 찍기 | `gv_screenshot` = `out.ppm`. 실행은 패널 버튼으로(아래 "문제 해결") |
| 다른 명령줄을 통째로 가져오기 | **사용자 · 환경** 탭에 한 줄로 붙여 넣기 |

다른 명령줄을 붙여 넣으면 아는 인자는 알아서 해당 항목으로 들어갑니다. 마음에 드는 조합은 **프리셋** 탭에서 이름을 붙여 저장합니다.

## 화면 둘러보기

### 위쪽 요약

| 항목 | 설명 |
|---|---|
| 상태 알약 | 프로필 이름, 구성 프리셋, Dev나 Shipping, 활성 게임, 실행 대상 |
| 명령줄 | 지금 넘길 인자 전부. **복사** 는 PowerShell에 붙여 넣을 수 있게 따옴표까지 넣습니다 |
| 빨간 줄 | 값이 틀려서 명령줄에서 뺀 항목과 이유(예: `-W: 정수여야 합니다`) |
| 노란 줄 | 점(`.`)이 든 인자가 있을 때의 주의 |
| **CMake Tools 로 디버그** | CMake Tools의 디버그를 이 인자로 시작합니다. 중단점이 걸립니다 |
| **실행 (셸 없이)** | 디버거 없이 실행합니다. 필요하면 먼저 빌드합니다 |
| **모두 빼기** | 체크를 모두 풀어 명령줄을 비웁니다. 입력한 값은 기억합니다 |

상태 알약은 빌드를 바꾸면 따라 바뀝니다. **실행 (셸 없이)** 은 PowerShell을 거치지 않으므로 인자가 깨지지 않습니다.

### 탭

| 탭 | 하는 일 |
|---|---|
| **전역 변수** | `-gv_*` 변수. 기본은 모듈별로 그룹을 나눠 보여 줍니다 |
| **인자** | 엔진 명령줄 인자(`-W`, `-EnableEditor`, `-cook-shaders` 등). RHI 백엔드처럼 하나만 고르는 것은 드롭다운입니다 |
| **사용자 · 환경** | 목록에 없는 인자를 직접 적거나, 실행할 때 줄 환경 변수를 넣습니다 |
| **프리셋** | 지금 고른 것 전체를 이름 붙여 저장하고, 불러오고, 덮어쓰고, 지웁니다 |

### 찾기, 분류, 정렬

| 도구 | 하는 일 |
|---|---|
| 검색 | 이름, 설명, 철자에 그 글이 든 항목만 보입니다 |
| 체크된 항목만 | 명령줄에 넘어가는 항목만 보입니다 |
| **정렬** | 이름 오름차순과 내림차순, 체크된 항목 먼저, 소스 순서(정의된 파일과 줄 순서) |
| **분류** 칩 | 정의한 매크로 종류별로 보이거나 숨깁니다. 칩 옆 숫자는 그 분류의 변수 수입니다 |
| **묶기** | 모듈별, 분류별, 그룹 없음 |

이 저장소의 분류는 셋입니다. `일반` 은 `SW_GLOBAL_VARIABLE`, `테스트 (Shipping 제외)` 는 `SW_TEST_GLOBAL_VARIABLE`, `테스트 (Shipping 포함)` 은 `SW_TEST_GLOBAL_VARIABLE_SHIPPED` 로 정의한 변수입니다.
정렬은 그룹 안에서 적용됩니다. **그룹 없음** 과 이름 정렬을 함께 쓰면 전체를 가나다, ABC 순으로 볼 수 있습니다.
체크된 항목은 분류 칩으로 숨겨도 계속 보입니다. 넘어가는 인자가 화면에서 안 보이면 헷갈리기 때문입니다.
그룹 제목 옆의 `체크 2 / 42` 는 그 그룹에서 체크된 수와 전체 수입니다. 고른 정렬, 분류, 그룹 방식은 다음에 열어도 그대로입니다.

### 항목 한 줄 읽는 법

```text
☑ gv_viewMode   [int32]                 ← 체크하면 넘깁니다. 오른쪽은 타입
  [ 2          ]  ↺                     ← 값, 기본값으로 되돌리기
  기본값 0                              ← 소스의 기본값. 플래그는 "주지 않음"
  씬 보기 방식 (0 Lit / 1 Unlit / 2 Wireframe)   ← 소스에 적힌 설명
  Shipping 에서는 등록되지 않는 테스트용 변수입니다  ← 지금 빌드에서의 주의(있을 때만)
```

- **이름을 클릭**하면 그 변수를 정의한 소스 줄이 열립니다.
- bool 값은 처음 켤 때 기본값의 반대가 들어갑니다.
- `test` 와 `test-shipped` 배지는 벤치나 자동화용 변수라는 뜻입니다. 마우스를 올리면 분류 이름이 나옵니다.
- 줄이 그어진 이름은 지금 빌드에서 쓸 수 없는 변수입니다(예: Shipping에서 빠지는 테스트 변수). 다른 게임 전용 변수는 아예 숨겨집니다.

## 작동 원리

### `launch.json` 구성에서 쓰기

`launch.json` 에 직접 적은 디버그 구성은 원래 그 안의 `args` 로만 뜹니다. 구성에 **`"launchArgs"`** 한 줄을 넣으면, F5를 누르는 순간 패널에서 고른 인자와 환경 변수가 들어갑니다.

| 값 | 결과 |
|---|---|
| `"replace"` | 구성의 `args` 대신 패널의 인자만 씁니다 |
| `"append"` | 구성의 `args` 뒤에 패널의 인자를 붙입니다 |

`replace` 는 패널에서 모든 것을 고를 때, `append` 는 구성이 백엔드와 에디터 여부를 정하고 패널에서는 전역 변수만 더할 때 씁니다.

이 저장소의 `.vscode/launch.json` 은 이 방식으로 정리되어 있습니다. 백엔드나 에디터마다 구성을 따로 두지 않고, Run and Debug에서 구성 하나를 고른 뒤 사이드바에서 인자를 정합니다.

| 구성 | 하는 일 |
|---|---|
| App (CodeLLDB · 사이드바 인자) | 지금 CMake Tools 구성 프리셋의 빌드 폴더에서 App을 띄웁니다(`replace`) |
| CMake 실행 대상 (CodeLLDB · 사이드바 인자) | CMake Tools 상태 표시줄에서 고른 실행 대상을 띄웁니다. 필요하면 먼저 빌드합니다 |
| 시험 (CodeLLDB) | 테스트 실행 파일을 고르고 `--test_filter` 를 묻습니다. 사이드바 인자는 넣지 않습니다 |
| App 에 붙기 (CodeLLDB) | 떠 있는 App 프로세스를 골라 붙습니다 |
| App (WSL · 사이드바 인자), 시험 (WSL) | WSL 창에서 쓰는 같은 구성 |

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

- `launchArgs` 가 없는 구성은 건드리지 않습니다. CodeLLDB(`lldb`)뿐 아니라 MS C++ 디버거(`cppdbg`, `cppvsdbg`) 구성에서도 같은 한 줄로 됩니다.
- `append` 구성에 `-dx12` 가 적혀 있으면 패널의 RHI 백엔드는 "지정 안 함"으로 둡니다. 그렇지 않으면 백엔드 인자가 두 개가 됩니다.
- 패널의 **CMake Tools 로 디버그** 버튼도 CodeLLDB로 띄우려면 설정에 `"cmake.debugConfig": { "type": "lldb" }` 를 넣습니다. 이 저장소의 `.vscode/settings.json` 에는 이미 있습니다.
  이때 CMake Tools는 환경 변수를 MS 디버거 형식으로 넘기는데, 확장이 CodeLLDB 형식(`env`)으로 바꿔 줍니다.
- 무엇이 들어갔는지는 출력 창의 **Launch Args** 채널에 `injected [...]` 로 남습니다.

### 무엇이 어디에 저장되나

| 내용 | 저장 위치 | 공유 |
|---|---|---|
| 체크된 인자와 환경 변수 | `.vscode/settings.json` 의 `cmake.debugConfig` | git에 추적됨 |
| 체크를 푼 항목의 값, 사용자 인자, 프리셋 | `.vscode/launchArgs.json` | PC마다 따로(git 무시) |

- `settings.json` 의 주석과 다른 설정은 건드리지 않습니다. 넘길 것이 없으면 `cmake.debugConfig` 키를 지웁니다.
- `settings.json` 을 손으로 고치거나 git pull로 바뀌면 화면이 그 내용을 따라갑니다. 모르는 인자는 사용자 인자로 남습니다.

### 프로필 — 다른 프로젝트에서 쓰기

확장 코드는 이 엔진을 모릅니다. 소스에서 무엇을 읽을지는 **프로필**(JSON) 하나가 정하므로, 다른 CMake 프로젝트에서도 프로필만 쓰면 됩니다.
프로필이 없어도 사용자 인자, 환경 변수, 프리셋, 실행과 디버그는 그대로 쓸 수 있습니다.

설정 `launchArgs.catalog` 에 프로필 파일 경로(작업 폴더 기준)나 객체를 적습니다. 이 저장소에는 `"launchArgs.catalog": "Tools/launch-args/Profiles/SwEngine.json"` 이 들어 있습니다.
프로필 파일 첫 줄에 `"$schema": "<경로>/ProfileSchema.json"` 을 넣으면 편집기가 필드를 자동 완성하고 검사합니다.

- [`Profiles/SwEngine.json`](Profiles/SwEngine.json) 은 이 저장소용 프로필입니다.
- [`Test/Integration/Fixture/profile.json`](Test/Integration/Fixture/profile.json) 은 엔진과 무관한 작은 프로젝트용입니다. 매크로 이름, 인자 순서, `--` 접두사가 다릅니다.
- [`Profiles/ProfileSchema.json`](Profiles/ProfileSchema.json) 에 모든 필드의 정의가 있습니다.

| 필드 | 뜻 |
|---|---|
| `global_variable.files`, `macros`, `argument_index` | 전역 변수를 정의하는 파일(glob)과 매크로, 매크로 인자의 위치 |
| `global_variable.type_map` | C++ 타입을 편집기 종류(`bool`, `int`, `float`, `string`, `enum`)로 대응 |
| `global_variable.name_prefix` | 목록에 없는 키라도 이 접두사(`gv_`)로 시작하면 전역 변수로 읽음 |
| `global_variable.default_value_macros` | 기본값이 매크로일 때 그 값을 찾는 법 |
| `argument.files`, `macro`, `argument_index` | 명령줄 인자를 등록하는 매크로 |
| `argument.exclusive_groups` | 하나만 고르는 플래그 그룹(드롭다운) |
| `command_line` | 명령줄 형식. `prefix`(기본 `-`)와 `separator`(기본 `=`) |
| `module` | 목록을 나누는 기준 폴더와, 하나씩만 빌드되는 폴더 |
| `status` | 상태 알약에 보일 CMake 캐시 값 |
| `rules` | 조건이 맞으면 변수에 알림을 붙이는 규칙 |

`macros` 의 각 항목에는 `tag`(분류 키)와 `label`(화면에 보일 분류 이름)이 있습니다. 분류 칩, 그룹, 배지, 규칙이 `tag` 를 씁니다.
`type_map` 에 없는 타입은 같은 이름의 `enum class` 를 찾아 드롭다운으로 만듭니다. 인자의 기본값이 `true` 나 `false` 면 플래그, 숫자면 숫자 입력이 됩니다.
`module` 의 하나씩만 빌드되는 폴더(`variant_folders`)는 게임이나 플러그인 폴더이고, 어느 것을 빌드하는지는 CMake 캐시 변수로 고릅니다.
`rules` 의 조건은 캐시 변수(`when`), 태그(`match`), 모듈, `unless_argument` 이고, 알림 단계는 `info`, `warning`, `unavailable` 입니다.

CMake 캐시 값은 CMake Tools가 고른 빌드 폴더의 `CMakeCache.txt` 를 먼저 보고, 아직 configure 전이면 프리셋의 `cacheVariables` 를 봅니다.

## 확장하는 법 — 확장 자체를 고칠 때

```powershell
py -3 Tools/launch-args/Scripts/ExtensionTool.py test                 # 단위 테스트
py -3 Tools/launch-args/Scripts/ExtensionTool.py test --integration   # 격리된 VS Code에서 통합 테스트까지(CMake Tools 필요)
py -3 Tools/launch-args/Scripts/ExtensionTool.py package              # dist/launch-args-<버전>.vsix
py -3 Tools/launch-args/Scripts/ExtensionTool.py install              # 지금 VS Code에 설치
```

- 테스트는 VS Code에 들어 있는 Node로 돌고, VSIX는 파이썬이 직접 만듭니다. 버전을 올리면 `package.json` 의 `version` 을 바꿉니다.
- `Source/` 의 모듈 중 `CatalogProfile.js`, `CatalogScanner.js`, `CppTextUtil.js`, `LaunchArgumentUtil.js`, `SelectionStore.js` 는 `vscode` 를 모르는 순수 모듈이라 `Test/*.js` 가 단위 테스트합니다.
  나머지는 VS Code API를 쓰고, 통합 테스트(`Test/Integration/`)가 확장 호스트에서 확인합니다. 진입점은 `Extension.js` 입니다.
- 통합 테스트 픽스처의 C++과 CMake 파일은 `*.in` 으로 둡니다. 저장소의 커밋 훅이 엔진 코드로 보고 검사하지 않게 하려는 것이고, 테스트가 복사할 때 `.in` 을 뗍니다.
- 웹뷰에서는 `confirm()` 이 뜨지 않으므로, 지우기 확인은 확장 쪽 모달로 합니다.

**코드 규칙.** 저장소 규칙([AGENTS.md](../../AGENTS.md))을 JavaScript에 옮겨 씁니다.

- 파일은 `PascalCase.js`, 클래스는 `PascalCase`, 함수는 `camelCase`, 멤버는 `_camelCase`, 상수는 `kPascalCase` 입니다.
- bool은 `b` 접두사(`_bInitialized`), 컨테이너는 `list`, `map`, `unique` 접두사와 단수 이름(`_listPreset`, `_mapRow`)을 씁니다.
- 모듈 안 도우미는 `camelCaseInternal` 이고, 동사는 저장소의 동사 목록(`initialize`, `get`, `find`, `make`, `create`, `compute`)을 따릅니다.
- 비교는 `=== false` 처럼 드러내 적습니다. 한 줄 `if` 는 중괄호 없이 쓰고, 반복문에는 항상 중괄호를 씁니다.
- 선언 위 `/** @brief */` 는 한국어 "~합니다" 체, 본문 `//` 는 "~다" 체, 로그 글은 영어입니다.

## 함정과 주의

- **커밋 전에 "모두 빼기"를 누르세요.** 체크된 인자는 git이 추적하는 `.vscode/settings.json` 에 쓰이므로, 실수로 커밋될 수 있습니다.
- **점(`.`)이 든 인자는 CMake Tools 상태 표시줄의 ▷(실행)로 띄우지 마세요.** Windows PowerShell은 `-` 로 시작하고 점이 든 인자(`-gv_screenshot=out.ppm`)를 `-gv_screenshot=out` 과 `.ppm` 으로 쪼갭니다.
  패널의 **실행 (셸 없이)** 과 **CMake Tools 로 디버그** 버튼은 셸을 거치지 않아 안전합니다. 이런 인자가 있으면 패널 위쪽에 노란 줄이 뜹니다.
- **VS Code 터미널에서 VS Code를 띄우면 창 대신 Node로 돕니다.** 물려받은 `ELECTRON_RUN_AS_NODE` 때문이고, 종료 코드 9로 끝납니다. `ExtensionTool.py` 는 그 변수를 빼고 띄웁니다.
- **한국어 Windows 콘솔(cp949)에서 `ExtensionTool.py test` 가 출력 중에 죽을 수 있습니다.** 테스트 출력의 기호를 콘솔 인코딩으로 쓰지 못하기 때문입니다. `PYTHONIOENCODING=utf-8` 을 주고 실행합니다.
- 매크로나 인자 목록, 캐시 규칙을 바꾸면 `Profiles/SwEngine.json` 도 고칩니다.

## 문제 해결

**목록이 비어 있어요.** 설정 `launchArgs.catalog` 가 프로필을 가리키는지 확인합니다. 그래도 비면 출력 창(`Ctrl+Shift+U`)의 **Launch Args** 채널에 읽지 못한 항목과 이유가 있습니다.

**새로 추가한 전역 변수가 안 보여요.** 소스를 저장하면 자동으로 다시 읽습니다. 그래도 안 보이면 패널 제목 줄의 새로고침(Rescan Source)을 누릅니다.
다른 게임 폴더(`Source/Games/<게임>`)의 변수는 그 게임이 활성일 때만 보입니다.

**`launch.json` 구성으로 띄웠더니 패널 인자가 안 들어가요.** 그 구성에 `"launchArgs": "replace"` 나 `"append"` 가 있는지 확인합니다.

**"빌드 문맥 모름"이라고 나와요.** CMake Tools에서 아직 configure하지 않은 빌드입니다. configure하면 Debug와 Shipping, 활성 게임을 알아내 판정이 붙습니다.

**값이 빨갛게 표시돼요.** 타입에 맞지 않는 값입니다(정수 필드에 글자 등). 그 항목은 명령줄에서 빠져 있고, 고치면 다시 들어갑니다.

**프리셋과 기억해 둔 값을 모두 지우고 싶어요.** `Ctrl+Shift+P` 에서 **Launch Args: Delete Saved State** 를 고릅니다. 지금 체크된 항목은 남습니다.
