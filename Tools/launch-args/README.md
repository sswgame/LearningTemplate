# Launch Args — 실행 인자를 클릭으로 고르는 VS Code 확장

게임 · 에디터를 띄울 때마다 `-dx12 -EnableEditor -gv_viewMode=2 …` 같은 인자를 손으로 적고 있었다면, 이 확장이 그 일을 대신합니다.

- 엔진 소스에 정의된 **전역 변수(`-gv_*`)와 커맨드라인 인자를 목록으로 보여 줍니다.** 설명 · 기본값 · 타입이 함께 나옵니다.
- 체크하고 값을 고르면 VS Code 의 **CMake Tools 디버그 · 실행**, 그리고 **`launch.json` 의 CodeLLDB 구성(F5)** 에 그대로 넘어갑니다.
- 자주 쓰는 조합은 **프리셋**으로 저장해 두었다가 한 번에 불러옵니다.
- 지금 빌드(Debug · Shipping, 활성 게임)에서 **쓸 수 없는 변수는 이유를 알려 줍니다.**

> 이 확장은 C++ 코드를 읽기만 하고 바꾸지 않습니다. 엔진에 새 전역 변수를 추가하면 저장하는 즉시 목록에 나타납니다.

**CMake Tools 와의 관계** — 디버그 · 실행 자체는 CMake Tools(와 CodeLLDB 등 디버거)가 합니다. Launch Args 는 "어떤 인자로" 만 정해
CMake Tools 설정(`cmake.debugConfig`)에 써 주는 도구입니다. 이름 · 설정 키(`launchArgs.*`) · launch.json 칸(`"launchArgs"`)을
CMake Tools 의 `cmake.*` 와 겹치지 않게 지었습니다. 상태 표시줄의 **`인자 3`** 항목이 지금 넘기는 인자 수이고, 마우스를 올리면 명령줄 전체가, 누르면 이 패널이 열립니다.

---

## 1. 설치 (처음 한 번)

준비물: VS Code, **CMake Tools** 확장(`ms-vscode.cmake-tools`), Python 3. Node.js 는 필요 없습니다.

저장소 루트에서 아래를 실행한 뒤, VS Code 에서 `Ctrl+Shift+P` → **Developer: Reload Window**.

```powershell
py -3 Tools/launch-args/Scripts/ExtensionTool.py install
```

왼쪽 활동 막대에 **Launch Args** 아이콘이, 아래 상태 표시줄에 **`인자 없음`** 항목이 생기면 설치된 것입니다.

> 예전 판(`yourname.launch-args` 2.x)을 쓰고 있었다면 확장 목록에서 먼저 제거하세요. 확장 id 가 `sw-engine.launch-args` 로 바뀌었습니다.

## 2. 3분 안에 써 보기

1. CMake Tools 상태 표시줄에서 **구성 프리셋**(예: `Ninja-Debug`)과 **실행 대상**(예: `App`)을 평소처럼 고릅니다.
2. 활동 막대의 **Launch Args** 를 엽니다.
3. **인자** 탭 → `RHI 백엔드` 에서 `DirectX12 (-dx12)` 를 고르고, `-EnableEditor` 를 체크합니다.
4. **전역 변수** 탭 → 검색창에 `viewMode` → `gv_viewMode` 의 값을 `2` 로 바꿉니다(값을 고치면 자동으로 체크됩니다).
5. 위쪽 **명령줄** 상자에 `-dx12 -EnableEditor -gv_viewMode=2` 가 보이는지 확인합니다.
6. **CMake Tools 로 디버그** 버튼(또는 평소처럼 CMake Tools 의 디버그 · Run and Debug 의 "App (CodeLLDB · 사이드바 인자)")을 누르면 이 인자로 App 이 뜹니다.

끝입니다. 빼고 싶으면 체크를 풀거나 **모두 빼기** 를 누릅니다.

> **체크된 항목만 명령줄에 넘어갑니다.** 값을 고치면 자동으로 체크되지만, 체크는 "고쳤다" 는 표시가 아닙니다 —
> 기본값 그대로라도 체크하면 넘어가고, 값을 고쳐 두었어도 체크를 풀면 넘어가지 않습니다(값은 기억합니다).

## 3. 화면 둘러보기

### 위쪽 — 요약

| 무엇 | 설명 |
|------|------|
| 상태 알약 | 프로필 이름 · 구성 프리셋 · `Dev`/`Shipping` · `게임 Empty` · 실행 대상. 빌드를 바꾸면 따라 바뀝니다 |
| 명령줄 | 지금 넘길 인자 전부. **복사** 는 PowerShell 에 그대로 붙여 넣을 수 있게 따옴표까지 넣어 복사합니다 |
| 빨간 줄 | 값이 틀려서 **명령줄에서 뺀** 항목과 그 이유(예: `-W: 정수여야 합니다`) |
| 노란 줄 | 점(`.`)이 든 인자가 있을 때의 주의 — 아래 [자주 묻는 것](#7-자주-묻는-것--문제-해결) 참고 |
| **CMake Tools 로 디버그** | CMake Tools 의 디버그를 이 인자로 시작합니다(중단점이 걸립니다) |
| **실행 (셸 없이)** | 디버거 없이 실행합니다. 셸(PowerShell)을 거치지 않아 인자가 깨지지 않습니다. 필요하면 먼저 빌드합니다 |
| **모두 빼기** | 체크를 모두 풀어 명령줄을 비웁니다. 입력해 둔 값은 기억합니다 |

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
| 체크된 항목만 | 체크된 항목(명령줄에 넘어가는 것)만 보입니다 |
| **정렬** | `이름 ↑` · `이름 ↓` · `체크된 항목 먼저` · `소스 순서`(정의된 파일 · 줄 순서). 묶음 안에서 정렬됩니다 |
| **분류** 칩 | 매크로 종류별로 보이기 · 숨기기 — 이 저장소는 `일반`(`SW_GLOBAL_VARIABLE`) · `테스트 (Shipping 제외)`(`SW_TEST_GLOBAL_VARIABLE`) · `테스트 (Shipping 포함)`(`SW_TEST_GLOBAL_VARIABLE_SHIPPED`). 칩 옆 숫자는 그 분류의 변수 수 |
| **묶기** | `모듈별`(엔진 · 에디터 · 게임 …) · `분류별`(위 분류) · `묶지 않음`(한 목록 — 이름 정렬과 함께 쓰면 가나다 · ABC 순 전체 목록) |

체크된 항목은 분류 칩으로 숨겨도 계속 보입니다(넘어가는 것이 안 보이면 헷갈리므로). 묶음 제목 옆 `체크 2 / 42` 는 그 묶음에서 체크된 수 / 전체 수입니다. 고른 정렬 · 분류 · 묶기는 다음에 열어도 그대로입니다.

### 항목 한 줄 읽는 법

```
☑ gv_viewMode   [int32]                 ← 체크 = 넘긴다 · 타입
  [ 2          ]  ↺                     ← 값 · 기본값으로 되돌리기
  ─────────────────────
  기본값 0                              ← 소스의 기본값(플래그는 "주지 않음")
  ─────────────────────
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
| 스크린샷 찍기 | `gv_screenshot` = `out.ppm` (이 경우 **실행 · 디버그 버튼**을 쓰세요 — 7절) |
| 다른 명령줄을 통째로 가져오기 | 사용자 · 환경 탭에 `-dx11 -gv_viewMode=1` 처럼 한 줄로 붙여 넣기 → 아는 인자는 알아서 제 칸으로 들어갑니다 |

마음에 드는 조합은 **프리셋** 탭에서 이름을 붙여 저장하세요(예: `벤치 8 · 와이어`).

## 5. CodeLLDB · launch.json 구성에서 쓰기

`launch.json` 에 직접 적은 디버그 구성은 원래 그 안에 적힌 `args` 로만 뜹니다.
구성에 **`"launchArgs"`** 한 줄을 넣으면, F5 를 누르는 순간 패널에서 고른 인자 · 환경 변수가 들어갑니다.

| 값 | 결과 |
|----|------|
| `"replace"` | 구성의 `args` 대신 **패널의 인자만** 씁니다. 패널에서 모든 것을 고를 때 |
| `"append"` | 구성의 `args` **뒤에** 패널의 인자를 붙입니다. 구성이 백엔드 · 에디터를 정하고 패널에서는 전역 변수만 더할 때 |

이 저장소의 `launch.json` 은 이 방식으로 정리되어 있습니다. 백엔드 · 에디터마다 구성을 따로 두지 않고, Run and Debug 에서 하나를 고른 뒤 사이드바에서 인자를 정합니다.

| 구성 | 하는 일 |
|------|---------|
| **App (CodeLLDB · 사이드바 인자)** | 지금 CMake Tools 구성 프리셋의 빌드 폴더에서 App 을 띄운다(`replace`) |
| **CMake 실행 대상 (CodeLLDB · 사이드바 인자)** | CMake Tools 상태 표시줄에서 고른 실행 대상을 띄운다(필요하면 먼저 빌드) |
| **시험 (CodeLLDB)** | 시험 실행 파일을 고르고 `--test_filter` 를 묻는다(사이드바 인자는 넣지 않는다) |
| **App 에 붙기 (CodeLLDB)** | 떠 있는 App 프로세스를 골라 붙는다 |
| **App (WSL · 사이드바 인자)** · **시험 (WSL)** | WSL 창에서 쓰는 같은 구성 |

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

- `launchArgs` 가 없는 구성은 건드리지 않습니다. CodeLLDB(`lldb`) 말고 MS C++ 디버거(`cppdbg` · `cppvsdbg`) 구성에서도 같은 한 줄로 됩니다.
- `append` 를 쓰는 구성에 `-dx12` 가 적혀 있으면 패널의 RHI 백엔드는 "지정 안 함" 으로 두세요. 백엔드 인자가 둘이 됩니다.
- 패널의 **CMake Tools 로 디버그** 버튼도 CodeLLDB 로 띄우려면 설정에 `"cmake.debugConfig": { "type": "lldb" }` 를 넣습니다 — 이 저장소의 `.vscode/settings.json` 에는 들어 있습니다.
  이때 CMake Tools 가 환경 변수를 MS 디버거 모양으로 넘기는데, 확장이 CodeLLDB 모양(`env`)으로 바꿔 주므로 그대로 전달됩니다.
- 무엇이 들어갔는지는 출력 창 **Launch Args** 에 `injected [...]` 로 남습니다.

## 6. 무엇이 어디에 저장되나

| 내용 | 저장 위치 | 공유 |
|------|-----------|------|
| **체크된** 인자 · 환경 변수 | `.vscode/settings.json` 의 `cmake.debugConfig` (CMake Tools 가 읽는 곳) | git 에 추적됨 |
| 체크를 푼 항목의 값 · 사용자 인자 · 프리셋 | `.vscode/launchArgs.json` | PC 마다 따로(git 무시) |

- `settings.json` 의 주석과 다른 설정은 건드리지 않습니다. 넘길 것이 없으면 `cmake.debugConfig` 키를 지웁니다.
- **커밋 전에 "모두 빼기" 를 누르세요.** 체크된 인자가 `settings.json` 변경으로 보여 실수로 커밋될 수 있습니다.
- `settings.json` 을 손으로 고치거나 git pull 로 바뀌면 화면이 그 내용을 따라갑니다(모르는 인자는 사용자 인자로 남습니다).

## 7. 자주 묻는 것 · 문제 해결

**목록이 비어 있어요.**
설정 `launchArgs.catalog` 가 프로필을 가리키는지 보세요. 이 저장소에서는 이미 들어 있습니다:
`"launchArgs.catalog": "Tools/launch-args/Profiles/SwEngine.json"`.
그래도 비면 출력 창(`Ctrl+Shift+U`)에서 **Launch Args** 채널을 보세요. 읽지 못한 항목과 이유가 적혀 있습니다.

**새로 추가한 전역 변수가 안 보여요.**
소스를 저장하면 자동으로 다시 읽습니다. 그래도 안 보이면 패널 제목 줄의 새로고침(Rescan Source)을 누르세요.
다른 게임 폴더(`Source/Games/<게임>`)의 변수는 그 게임이 활성일 때만 보입니다.

**launch.json 의 CodeLLDB 구성으로 띄웠더니 패널 인자가 안 들어가요.**
그 구성에 `"launchArgs": "replace"`(또는 `"append"`)가 있는지 보세요 — [5절](#5-codelldb--launchjson-구성에서-쓰기).

**`gv_screenshot=out.ppm` 이 이상하게 들어가요.** *(노란 경고가 뜰 때)*
Windows PowerShell 은 `-` 로 시작하고 점(`.`)이 든 인자를 `-gv_screenshot=out` 과 `.ppm` 으로 **쪼갭니다.**
CMake Tools 상태 표시줄의 ▷(실행)가 이렇게 깨집니다. 이 패널의 **실행 (셸 없이)** · **CMake Tools 로 디버그** 버튼은 셸을 거치지 않아 안전합니다.

**"빌드 문맥 모름" 이라고 나와요.**
CMake Tools 에서 아직 구성(configure)을 하지 않은 빌드입니다. 구성하면 Debug/Shipping · 활성 게임을 알아내 판정이 붙습니다.

**값이 빨갛게 표시돼요.**
타입에 맞지 않는 값입니다(정수 칸에 글자 등). 그 항목은 명령줄에서 빠져 있으니 고치면 다시 들어갑니다.

**프리셋 · 기억해 둔 값을 모두 지우고 싶어요.**
`Ctrl+Shift+P` → **Launch Args: Delete Saved State**. 지금 체크된 항목은 남습니다.

---

## 8. 다른 프로젝트에서 쓰기 — 프로필

확장 코드는 이 엔진을 모릅니다. "소스에서 무엇을 읽을지" 는 **프로필**(JSON) 하나가 정하므로, 다른 CMake 프로젝트에서도 프로필만 쓰면 됩니다.
프로필이 없어도 사용자 인자 · 환경 변수 · 프리셋 · 실행 · 디버그는 그대로 쓸 수 있습니다.

설정 `launchArgs.catalog` 에 프로필 파일 경로(작업 폴더 기준)나 객체를 적습니다.
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

## 9. 확장을 고치는 사람에게

```powershell
py -3 Tools/launch-args/Scripts/ExtensionTool.py test                 # 단위 시험
py -3 Tools/launch-args/Scripts/ExtensionTool.py test --integration   # + 격리된 VS Code 에서 통합 시험(CMake Tools 필요)
py -3 Tools/launch-args/Scripts/ExtensionTool.py package              # dist/launch-args-<판>.vsix
py -3 Tools/launch-args/Scripts/ExtensionTool.py install              # 지금 VS Code 에 설치
```

- 시험은 VS Code 안에 든 Node 로 돌고, VSIX 는 파이썬이 직접 만듭니다. 판을 올리면 `package.json` 의 `version` 을 바꿉니다.
- VS Code 터미널에서 VS Code 를 띄우면 물려받은 `ELECTRON_RUN_AS_NODE` 때문에 창 대신 Node 로 돕니다(종료 코드 9). `ExtensionTool.py` 는 그 변수를 빼고 띄웁니다.
- 통합 시험 픽스처의 C++ · CMake 파일은 `*.in` 으로 둡니다. 저장소의 커밋 훅이 엔진 코드로 보고 검사하지 않게 하려는 것이고, 시험이 복사할 때 `.in` 을 뗍니다.
- 웹뷰에서는 `confirm()` 이 뜨지 않으므로 지우기 확인은 확장 쪽 모달로 합니다.

### 구조

```
package.json                    확장 매니페스트(명령 · 뷰 · 설정 launchArgs.catalog)
Source/
  Extension.js                  진입점 — CMake 작업 폴더를 골라 잇는다
  LaunchArgumentController.js   카탈로그 · 선택 · 설정 · 상태 파일을 맞춘다(화면 의도 → 선택 → 미뤄 쓰기, 밖의 변경 읽기)
  LaunchArgumentViewProvider.js 웹뷰 공급자(CSP nonce, 카탈로그는 바뀔 때만 보낸다)
  CatalogProfile.js             프로필 읽기 · 기본값 · glob            ┐
  CatalogScanner.js             프로필대로 소스를 읽어 카탈로그        │ vscode 를 모르는 순수 모듈 —
  CppTextUtil.js                주석 지우기 · 매크로 인자 · 리터럴     │ Test/*.js 가 시험한다
  LaunchArgumentUtil.js         선택 ↔ 명령줄 · 값 검사 · 규칙 판정   │
  SelectionStore.js             .vscode/launchArgs.json        ┘
  BuildContextProvider.js       CMake Tools API — 프리셋 · 빌드 폴더 · 캐시 값
  DebugConfigWriter.js          cmake.debugConfig 읽기 · 쓰기(설정 API, 주석 보존)
  TargetRunner.js               디버그(cmake.debugTarget) · 셸 없는 실행
  DebugConfigurationInjector.js 모든 디버거의 시작 직전 훅 — launchArgs 구성에 주입 · CodeLLDB env 변환
  StatusBarIndicator.js         상태 표시줄 "인자 N" — 마우스를 올리면 명령줄, 누르면 패널
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

## 함정 · 계약

- **VS Code 실행 인자 GUI 는 `Tools/launch-args`**(프로필 `Profiles/SwEngine.json` — 매크로 · 인자 표 · 캐시 규칙을 바꾸면 이 파일을 고친다). 켜진 인자는 추적되는 `.vscode/settings.json` 의 `cmake.debugConfig` 에 쓰인다. 시험 · 패키징은 `py -3 Tools/launch-args/Scripts/ExtensionTool.py test --integration` · `package` — VS Code 터미널에서 띄우는 VS Code 는 물려받은 `ELECTRON_RUN_AS_NODE` 를 빼야 창으로 뜬다(종료 코드 9).
