# 작업 백로그 — 남은 일과 이어받기

> 목적: 여러 PC·여러 세션에서 이어서 작업하기 위한 **단일 할 일 목록**이다. 무엇이 끝났고
> 무엇이 남았는지, 남은 것을 왜 그 순서로 두었는지, 손대기 전에 알아야 할 함정이 무엇인지를
> 여기 적는다. 작업을 끝내면 이 문서의 해당 항목을 지우거나 "완료"로 옮기고 같이 커밋한다.
>
> 마지막 갱신: 2026-09-09 · 기준 커밋 `e6973186`

---

## 0. 손대기 전에 — 검증 규칙

에디터 패널에는 **단위 테스트가 없다.** 컴파일이 통과해도 화면이 비어 있을 수 있다.
따라서 패널·위젯을 건드렸다면 반드시 실행해서 확인한다.

```powershell
cmake --build --preset Ninja-Debug            # 경고 0 이어야 한다
cmake --build --preset Ninja-Shipping         # Debug 가 숨기는 결함이 여기서 드러난다
ctest --preset Ninja-Debug-lint               # 컨벤션·include 순서

# 테스트 (현재 기준선)
#   Debug    : CoreTest 165 / EngineTest 420 / ReflectionTest 100 / EditorTest 27 / SmokeTest 19
#   Shipping : 157 / 418 / 96 / 27 / 1        ← 차이는 전부 Dev 전용 케이스의 정상 스킵
ctest --test-dir build/Ninja-Debug -L nogpu
ctest --test-dir build/Ninja-Shipping -L nogpu

# 에디터 실기동 — 패널 변경의 유일한 실질 검증 수단
cd build/Ninja-Debug/Bin
./App.exe -gv_profileFrames=40 -dx12 -EnableEditor    # 종료 코드 0, 로그에 [Error] 0건
./App.exe -gv_profileFrames=40 -dx11 -EnableEditor
```

**함정**

- `-gv_rhiBackend` 는 App 이 무시한다. `-dx11 / -dx12 / -vk / -gl` 을 쓴다.
- Shipping 테스트 바이너리는 `build/Ninja-Shipping/TestBin/` 에 있고 **작업 디렉터리는 `Bin/`**
  이어야 한다(리소스를 상대 경로로 찾는다). `Bin/` 에 낡은 테스트 exe 사본이 남아 있을 수 있으니
  `TestBin/` 쪽을 직접 실행할 것.
- 셰이더 소스(.hlsl/.hlsli)를 고쳤으면 재베이크가 필요하다. Shipping 쿠킹은
  `bake.stamp` 의 내용 해시로 이를 검증하고 어긋나면 빌드를 세운다
  (`Scripts/generate/CookAssets.py --verify-shaders`). 베이커가 있으면 스스로 다시 굽는다.

---

## 1. 남은 일 (우선순위 순)

### 1-1. 공용 위젯을 안 쓰는 패널 정리 — **진행 중**

공용 위젯은 이미 충분하다(`Common/Widgets/EditorWidgets.h` 27개 +
`Common/Gui/EditorChrome.h` 12개). 문제는 **채택률**이다.

| 패널 | ImGui 직접 호출 | 공용 위젯 사용 |
|---|---|---|
| `InputMapEditorPanel` | 313 | **0** |
| `ProfilerPanel` | 94 | **0** |
| `PrefabEditorPanel` | 52 | **0** |
| `TileMapPanel` | 42 | 1 |
| `SpriteClipPanel` | 40 | 1 |

**빈 상태 안내는 마쳤다.** 82곳의 `ImGui::TextDisabled` 를 한 곳씩 보고 **본문 빈 상태인 12곳만**
`EditorWidgets::drawEmptyHint` 로 옮겼다(ContentBrowser 1 · GlobalVariables 1 · InputMapEditor 3 ·
Inspector 3 · Material 1 · Profiler 3).

옮기지 **않은** 자리와 이유 — 다음에 같은 판단을 반복하지 않도록 적어 둔다:

- **메뉴 안 3곳** (`Hierarchy` 의 컴포넌트 추가 메뉴 2곳, `Inspector` 의 프리셋 메뉴 1곳).
  `EndMenu()` 로 닫히는 팝업 안이라 백로그의 경고대로 두었다.
- **상태줄 2곳** (`Sequencer` 의 파일명 라벨, `Inspector` 의 "Scene edits locked" 칩 옆).
  `SameLine` 으로 붙은 인라인 라벨이고, 한쪽 분기만 바꾸면 같은 줄이 두 API 로 갈린다.
- **진행 상태 1곳** (`GlobalVariables` 의 "Scanning presets...").  비어 있는 게 아니라 **기다리는**
  중이다. 빈 상태와 로딩 상태는 나중에 다르게 보여야 할 자리다.

남은 것은 표의 **호출 수** 자체다 — `InputMapEditorPanel` 313 · `ProfilerPanel` 94 등은 빈 상태가
아니라 위젯·레이아웃 조립이라 1-2 의 골격 추출과 같이 가야 줄어든다.

### 1-2. 목록형 패널 골격 추출

여러 패널이 "툴바 → 검색 → 목록/표 → 상태줄" 이라는 같은 뼈대를 각자 조립한다.
문서형 패널은 `Common/Gui/EditorDocumentPanel` 이 이미 그 역할을 하지만 채택이 제한적이다.
목록형 골격(가칭 `EditorListPanel`)을 두면 새 패널이 본문만 채우면 된다.

범위가 넓고 검증이 실기동뿐이므로 **한 패널씩 옮기고 매번 실기동 확인**한다.

### 1-3. 100줄 넘는 함수 20개 — 우선순위 낮음

분해 자체는 코드 총량을 줄이지 않는다. 공통부 추출(1-1, 1-2)을 먼저 한다.
목록이 필요하면 다중 행 시그니처를 중괄호 깊이로 정확히 재는 스크립트를 만들어 뽑는다
(단순 정규식은 여러 줄 시그니처를 잘못 잰다).

### 1-4. 되살리지 못한 테스트

`Test/EditorTest/TestEditorSceneCommands.cpp` 는 되살렸지만(현재 EditorTest 27개에 포함),
`EditorContext` 가 UI 매니저 전부를 `unique_ptr` 로 소유하는 구조는 그대로다. 더 깊은 분리
(패널·팝업 매니저 소유를 컨텍스트 밖으로)는 영향 범위가 커서 하지 않았다. 필요해지면
그때 소유 구조부터 정한다.

### 1-5. 확인만 하고 넘어간 것

Shipping `EngineTest` 에서 `RHITest.CommandListCreationAndExecution` 이 **한 번** SEGFAULT
했고 재실행 3회는 모두 통과했다. EngineTest 는 Editor 를 링크하지 않으므로 에디터 변경과는
무관하다. 재현되면 따로 볼 것.

> 2026-09-09 추가: Shipping 빌드에 `FrameProfiler.cpp` 경고 3건(`avgUs`/`perFrameX10` 미사용,
> `pTitle` 미사용 파라미터)이 **예전부터** 있다. 보고 경로가 Shipping 에서 컴파일 아웃되면서 남은
> 변수들이다. App 개편과 무관하므로 건드리지 않았다.
>
> 2026-09-09 추가: "테스트는 초록인데 앱만 깨진다" 의 실례가 하나 나왔다(3절의 DX11 항목).
> 원인은 테스트 씬이 그 코드 경로를 **아예 안 태우고 있던** 것이었다. 위 SEGFAULT 도
> 재현을 기다리기보다 "그 테스트가 실제로 무엇을 태우는가" 를 먼저 보는 편이 빠를 수 있다.

---

## 2. 작업 방식 — 정해진 방향

- **쪼개기보다 공통부 추출.** 긴 함수를 나누면 코드가 이동할 뿐 총량은 그대로다.
  중복은 증상이고 원인은 "매번 다시 만들어야 하는 구조"다. 원인을 없앤다.
- **추가·변경에 용이한 구조를 먼저 만든다.** 새 타입·새 패널·새 백엔드를 하나 더 넣을 때
  복사해야 할 것이 남아 있으면 그 자리가 다음 리팩터 대상이다.
- 주석과 커밋 메시지는 한국어. 규칙은 [AGENTS.md](../AGENTS.md) 와
  [04_CodingGuidelines.md](04_CodingGuidelines.md).

### 편집 함정

- 한 함수에서 **여러 구간을 빼낼 때는 뒤쪽 구간부터** 한다. 앞쪽을 먼저 빼면 뒤쪽 줄 번호가
  밀려 `switch` 중간을 자르는 식으로 깨진다.
- 파일을 스크립트로 고칠 때 CRLF 를 보존한다. 이 저장소는 CRLF 다.

---

## 3. 최근에 끝낸 일 (2026-09-08 ~ 09)

무엇을 이미 해결했는지 알아야 같은 것을 다시 파지 않는다.

**폴더 개편 중에 드러난 실제 버그 셋** (각각 별도 커밋)
- `formatstring` 이 공백을 플래그로 받아 `%#` 뒤 단어의 첫 글자를 먹고 있었다. 테스트 요약이
  `0ailed`, `%# of %#` 는 8진수, `%# present` 는 16진수 — 해당 로그가 82곳. 호출부가 아니라
  파서를 고쳤다(`6c0548f7`). 치수 로그 18곳의 `%#x%#` 는 설계상 16진수가 맞아 `×`(U+00D7)로.
- **창 크기 설정이 한 번도 반영된 적이 없었다.** `WIDTH`/`HEIGHT` 만 `bUseDefaultValue=true` 라
  인자를 주지 않아도 `getArgument` 가 등록된 기본값 1280 으로 `true` 를 돌려주고, 호출부의
  "설정값을 씨앗으로 두고 커맨드라인이 있으면 덮어쓴다" 패턴이 **항상** 덮어썼다. WIDTH 기본값이
  설정값과 같아 폭만 우연히 맞아서 가려져 있었다(창이 1280×1280 으로 떴다).
- **`Source/Core/CommandLine/ArgumentList.xxx` 는 죽은 사본이었다.** 실제로 include 되는 것은
  `Core/Predefined/ArgumentList.xxx` 이고, 죽은 쪽은 `LANGUAGE`·`BAKE_SHADERS` 가 빠진 낡은
  상태였다. 폴더 이름상 먼저 찾게 되는 자리라 고쳐도 아무 일이 없는 덫이었다 — 삭제했다.
- Shipping 경고 3건(`FrameProfiler`)도 없앴다. `report()` 는 Info 로그로만 출력하는데 배포본에서
  `SW_LOG_INFO` 가 사라져 **출력 없는 계산**을 하고 있었다 — 로그가 컴파일될 때만 돌게 묶었다.
  이제 Debug/Shipping 양쪽 모두 빌드 경고 0 이다.

**Source 폴더를 순서대로 개편 중 (App → RuntimeAPI → Core → Engine → …)**

진행한 폴더: `App`(`6efa4fd2`) · `RuntimeAPI`(`06889dc7`) · `Core`(`58c1ac30`) ·
`Engine`(`83b6ea60`) · `GameFramework`(아래).
**모두 끝났다.** `App`(`6efa4fd2`) · `RuntimeAPI`(`06889dc7`) · `Core`(`58c1ac30`) ·
`Engine`(`83b6ea60`) · `GameFramework`(`bfb00e1d`) · `Games`(`71c4e0aa`) · `Editor`(`cb55af53`) ·
`Tools/ReflectionParser`(아래).

**ReflectionParser — 죽은 X-macro 사본을 없애고 재발을 검사로 막았다**
- `.xxx` 목록 파일 **사본 6개**가 아무도 include 하지 않는 상태로 있었다. 이게 왜 나쁜지는
  내가 직접 겪었다 — 창 크기 버그를 고치려고 `Source/Core/CommandLine/ArgumentList.xxx` 를
  수정했는데 빌드 결과가 바뀌지 않았다. 실제로 include 되는 것은
  `Source/Core/Predefined/ArgumentList.xxx` 였고, 죽은 쪽은 폴더 이름상 먼저 찾게 되는 자리에
  있으면서 `LANGUAGE`·`BAKE_SHADERS` 가 빠진 낡은 상태였다.
- 지운 것: `Core/Predefined/PredefinedEnumBitFlagTags.xxx`,
  `Engine/Reflection/PredefinedEnumBitFlagTags.xxx`(둘 다 `REGISTER_ENUM_BITFLAG_TAG` 를 쓰는
  곳이 없다 — 비트플래그는 이제 명시 애노테이션과 2의 거듭제곱 자동 판정으로 잡는다),
  `Engine/Reflection/PredefinedContainerKind.xxx`, `Engine/Reflection/PredefinedFunctionNetRole.xxx`,
  `Tools/ReflectionParser/PredefinedAnnotationKind.xxx`(전부 `Core/Predefined/` 쪽을 include 한다),
  `Config/Reflection/AnnotationMeta.txt`(CMake·Constants.py 둘 다 `Source/Core/Predefined/` 를 쓴다
  — 이 폴더는 비어서 사라졌다).
- `Scripts/lint/CheckDataFileReferences.py` 신설 + CTest `lint` 등록(이제 lint 6개). 규칙:
  `Source/**`·`Tools/**` 의 모든 `.xxx` 는 include 또는 경로 참조가 하나는 있어야 한다.
  음성 테스트로 확인했다 — 죽은 사본을 되살리면 실패한다.
  (그 과정에서 검사 스크립트의 **독스트링에 적은 예시 경로**가 참조로 집계되어 한 번 통과해
  버렸다. 자기 자신은 세지 않게 고쳤다.)
- `cmake/Engine/GeneratedConstants.cmake` 는 **자동 생성물**이다. lint 경로 상수는
  `Scripts/common/Constants.py` 와 `Scripts/setup/GenerateCMakeConstants.py` 에 넣어야 한다 —
  생성물을 직접 고치면 다음 configure 가 지운다(이것도 한 번 겪었다).
- ReflectionParser README 의 파일 트리가 죽은 사본을 싣고 실제 파일
  (`PredefinedAnnotationField.xxx`)은 빠뜨리고 있었다 — 고쳤다.

**Editor — README 가 이미 정확했다. 고칠 것은 하나였다**
- `Source/Editor/README.md` 는 "어디에 두나" 표(ImGui 그림 / 상태만 / 바꾸거나 읽고 쓰기)까지
  갖춘 이 저장소에서 가장 잘 적힌 문서다. 폴더 구조도 실제와 맞는다 — 손대지 않았다.
- `HierarchyPanel` 이 계층 뱃지를 고르려고 타입 **이름** 7개를 if/else 로 비교하고 있었다
  (`UnitStatsComponent`, `HPBarBaseComponent` 처럼 게임플레이 타입까지 포함). **같은 파일이
  215행에서는 이미 `TypeInfo::getCategory()` 로 "컴포넌트 추가" 메뉴를 묶고 있었다** — 데이터는
  있는데 한쪽만 안 쓰고 있었다. `EditorUtil::appendCategoryBadge` 로 옮기고 Category 에서
  끌어온다. 등록된 어떤 컴포넌트든(게임이 만든 것 포함) 뱃지가 붙고, 같은 Category 는 한 번만
  나온다. `EditorTest.HierarchyBadgeComesFromReflectionCategory` 로 규약을 고정했다.
  (뱃지 글자가 `[Cam]` → `[Camera]`, `[Mesh]` → `[Rendering 3D]` 로 길어진다. 임의의 약어표를
  새로 만드는 것보다 리플렉션에 적힌 값을 그대로 쓰는 편이 낫다고 봤다.)
- `EditorViewportPreview::isDialogueRunnerType` 의 이름 비교는 **남겼다.** 에디터는
  GameFramework 를 링크할 수 없으므로 리플렉션으로 찾는 것이 정해진 탈출구다(짧은 이름과 FQN 을
  둘 다 본다). 결함이 아니다.
- **하지 않은 것**: 1-1(공용 위젯 채택)·1-2(목록형 패널 골격). 이 둘은 검증 수단이 **화면을 보는
  것**뿐이고(0절), 나는 종료 코드와 `[Error]` 개수만 볼 수 있다. 패널을 빈 화면으로 만들어 놓고도
  통과했다고 보고할 수 있는 작업이라 손대지 않았다. 사람이 띄워 보면서 한 패널씩 가는 편이 맞다.

**Games — 온보딩 안내가 존재하지 않는 전역 변수를 가리키고 있었다**
- README 가 `kGameFrameworkModuleName` · `kGameKitModules` · `kGameModuleName` 세 전역 변수로
  핫리로드 대상을 설명했다. **코드베이스에 그 이름은 하나도 없다.** 정본은
  `Config/App/AppConfig.json` 의 `_listGameKitModule` 이다. "새 게임 추가" 5단계 중 5번을
  그대로 따르면 아무 일도 일어나지 않았다.
- `Empty` 템플릿의 95% 가 메시 벤치 하네스였다(375줄 중 ~340줄). README 1번이 "이 폴더를
  복사하라" 이므로 새 게임은 벤치를 같이 들고 시작했다 → `BenchScene.{h,cpp}` 로 떼어내
  `EmptyGame` 을 ~40줄 템플릿으로 되돌렸다. 지울 경계를 파일 경계와 맞췄다.
  `Scripts/dev/BackendSmoke.py` 가 `-gv_benchMeshes` 에 기대므로 타깃·플래그는 그대로 두었다.

**GameFramework — 리플렉션이 조용히 누락되는 덫 둘, 그리고 키트 소속 기준이 없던 것**
- 리플렉션 대상 헤더를 소스와 **다른 규칙**으로 모으고 있었다. `GameFramework` 는
  `Base`/`Data`/`Transition`/`UI` 네 폴더를 **이름으로** 적어 두었고(소스는 재귀 GLOB),
  키트는 루트의 `*.h` 만 모았다(소스는 `GLOB_RECURSE`). 최상위 폴더나 키트 하위 폴더를 새로
  만들면 그 안의 `REFLECT()` 타입이 **컴파일은 되고 등록만 안 되는** 상태가 된다 — 역직렬화가
  조용히 기본값으로 떨어지므로 원인 찾기가 어렵다. 키트는 `sw_addReflectionStep` 의 자동 탐색에
  맡기고(재귀 + 매크로 필터), GameFramework 는 소스와 같은 재귀 GLOB + `Kits/` 제외로 맞췄다.
- 키트 소속 기준을 README 에 적었다. **의존 관계로는 판별되지 않는다** — 키트 컴포넌트는 전부
  `Engine` 만 include 해서 컴파일러에는 어디든 같다. 그래서 `HPBarBaseComponent`,
  `DamageUIComponent`, `GravityComponent` 가 `Kits/ActionCombat` 에 있었다. HP 바와 데미지
  숫자는 턴제도 쓰고 중력은 플랫포머도 쓴다 → `UI/` · `Base/` 로 옮겼다.
- README 의 폴더 목록이 낡아 있었다(`Events`/`Save` 폴더는 없다).
- **다음 차례(Editor)에서 같이 볼 것**: `HierarchyPanel.cpp:399` 가 컴포넌트 타입 이름 7개를
  if/else 로 하드코딩해 계층 뱃지를 고른다(`UnitStatsComponent`, `HPBarBaseComponent` 포함).
  에디터가 게임플레이 컴포넌트 이름을 알고 있고, 다른 게임의 컴포넌트는 뱃지가 없다.
  `REFLECT( Category = "UI" )` 메타데이터가 이미 있으니 거기서 끌어내면 if/else 가 사라진다.

**Engine — 문서에 적힌 레이어 순서가 코드와 달랐다**
- `Audio/XAudio2System` 이 모든 플랫폼에서 컴파일되며 `#if` 22개로 몸통을 비우고 있었다.
  `IAudioSystem::create()` 는 이미 Windows 에서만 이 클래스를 만드는데도 그랬다
  → `Audio/Windows/` 로 옮기고 파일 전체 가드 1개로(= `Window/Windows`·`Input/Windows` 형태).
- `Utility/Module` → `Module` 로 승격. LiveReloadManager 가 로드된 모든 Scene 의
  GameObjectManager 를 다시 묶는 **상위 서브시스템**인데 최하위 티어 폴더에 있었다.
- `ResourcePackReader` 의 플랫폼 분기 5벌을 Core 의 `PlatformFileUtil` 로 (Core 차례의 남은 일).
- DX11 `isHazardMessage` 의 `switch` 가 `-Wswitch-enum` 경고를 냈다(1328개 중 10개만 다룸)
  → 목록 순회로. 경고를 억누르지 않고 없앴다.

**`CheckEngineLayers` 의 내부 레이어 규칙을 근거 있는 것으로 바꿨다.**
예전에는 손으로 고른 네 쌍(`Utility->Graphics` 등)만 **경고로 찍고 실패시키지 않았다**.
지금은 include 그래프를 Tarjan SCC 로 줄여 얻은 티어 표를 쓰고, 위반은 실패다.

> ### Engine 코어 열 폴더는 하나의 강결합 묶음이다 — 남은 큰 일
>
> `Config` `Graphics` `Module` `Object` `Reflection` `Resource` `Scene` `Sequencer`
> `Serialization` `Window` 이 서로 도달 가능하다. README 가 주장했던 5단 순서는 **사실이
> 아니었다.** 묶음 안의 엣지 수(측정값, 소수 방향이 고칠 후보):
>
> | 엣지 | 수 | 내용 |
> |---|---|---|
> | `Object -> Graphics` | 3 | `MeshComponent` 가 Material·Mesh·RHITypes 를 든다 |
> | `Scene -> Graphics` | 5 | `Scene.cpp` 가 FrameRenderer·MaterialCache·IRHIDevice 를 부른다 |
> | `Reflection -> Serialization` | 4 | `ReflectAny.cpp` 가 직렬화기를 부른다 |
> | `Serialization -> Object` | 3 | `SerializeContext`·`SchemaMigrate` 가 TagSystem·ComponentHandle 을 안다 |
> | `Object -> Scene` | 6 | `ComponentPtr.cpp` 가 SceneManager 로 핸들을 푼다 |
> | `Object -> Sequencer` | 3 | `SequencePlayerComponent` (반대 방향도 3) |
> | `Config -> Graphics` | 1 | `EngineConfig` 가 `RHIBackend` 열거형을 든다 |
> | `Graphics`/`Resource` `-> Module` | 3 | 셰이더·리소스 핫리로드가 `ReloadFileManager` 를 쓴다 |
>
> 풀어내는 순서 제안(작은 것부터, 각각 독립): ① `Config -> Graphics` — `RHIBackend` 를
> `Config` 나 더 아래로 내린다. ② `Serialization -> Object` — 컴포넌트 핸들 해석을 콜백으로
> 받는다. ③ `Reflection -> Serialization` — `ReflectAny` 의 직렬화를 등록 가능한 훅으로.
> ④ 나머지(`Object`/`Scene` ↔ `Graphics`)는 컴포넌트 모델 자체의 설계라 별개의 큰 일이다.

**Core — 같은 플랫폼 분기가 세 파일에 복사돼 있었다**
- `PlatformFileUtil` 신설. `fopen_s`↔`fopen`, `_fseeki64`↔`fseeko`, `_ftelli64`↔`ftello` 의
  `#if` 가 `FileUtil.cpp` 에 5벌, `Logger.cpp` 에 1벌, Engine 의 `ResourcePackReader.cpp` 에
  5벌 있었다. **`ResourcePackReader` 는 아직 안 고쳤다 — Engine 차례에 같이 한다.**
- `FileUtil.cpp` 의 플랫폼 분기 26 → 10, `Logger.cpp` 5 → 3, `StringUtil.cpp` 12 → 4.
- `FileUtil::getFileSize` 가 크기만 알려고 파일을 열고 끝까지 탐색했다 → `std::filesystem::file_size`.
  바로 위 `getFileTimestamp` 는 이미 그렇게 하고 있었다.
- `readTextFile` 이 BOM 바이트를 손으로 세고 있었다(같은 파일 아래 `skipUtf8Bom` 이 정본).
- `StringUtil` 의 로케일 변환 두 방향이 "크기 질의 → 버퍼 → 변환" 전체를 각자 적어 같은 `#if` 가
  네 벌이었다 → 원시 연산 2개(`wideToMultiByte` / `multiByteToWide`)로 내리고 나머지는 공유.
- **건드리지 않은 것**: `Container/` 의 `vector`/`map`/`string` 은 std 호환 커스텀 구현체다
  (3800줄). 표면이 std 와 같아야 하는 물건이라 "중복" 처럼 보이는 것이 실은 계약이다.
  `Memory/` 의 할당기 3종도 정렬 계산만 닮았고 수명 모델이 달라 묶을 공통부가 아니다.

**App 폴더 구조 개편 — 상용 엔진과 대조해서**
- **시간 정책을 `App/Frame/FrameTimeline` 로.** 루프 안에 `constexpr 1.0f/60.0f` 로 박혀 있던
  고정 스텝을 `EngineConfig::_fixedDeltaTime` / `_maxFixedStepPerFrame` 로 올렸다. 예전 코드의
  스텝 수 상한은 `_maxFrameDeltaTime` 클램프에 **우연히 의존**하고 있었다 — 누산기가 잔액을
  남기므로 보장이 아니었다. 이제 상한이 명시적이고, 넘긴 잔액은 버린다(고정 스텝 스파이럴 차단).
  UE 의 `MaxPhysicsDeltaTime`/`MaxSubsteps`, Unity 의 `fixedDeltaTime`/`maximumDeltaTime` 과 같은 자리.
- **프레임당 에디터 상태 래치 — `ModuleFrameState`.** `isPlaying`/`isPaused`/`getGameViewport` 를
  프레임 안에서 8~10회 따로 묻던 것을(고정 스텝마다 DLL 경계를 다시 넘었다) 두 지점 래치로 바꿨다.
  **래치 지점이 두 개인 것은 의도다**: 게임플레이 활성 여부는 `beginFrame`(게임 업데이트 이전),
  게임 뷰포트와 씬 틱 여부는 `updateEditorUI`(에디터가 입력을 처리한 이후)다. 후자를 프레임 앞으로
  옮기면 **에디터 Step 한 칸이 틱 없이 소비**되어 아무 일도 일어나지 않는다 — `ImGuiEditor::isPaused`
  가 `paused && !pendingStep` 이기 때문이다. 옮기려면 이 사실부터 확인할 것.
- **`ModuleHost::suspendModules( ModuleScope, bReleaseApiTable )` 로 4경로 통합.**
  `shutdown` / `onBeforeEditorReload` / `onBeforeGameReload` / RHI 핫스왑이 "배수 → 상태 보존 →
  파괴" 를 각자 조립하고 있었다. 덤으로 두 가지가 정리됐다: (1) 핫스왑 경로가 `drainRenderWorkers`
  를 **두 번** 돌았다(App 이 부르고 `onBeforeRhiSwap` 이 또 불렀다), (2) `onBeforeGameReload` 는
  `_game == nullptr` 이면 조기 반환해서 **언로드 직전에도 API 테이블과 타입 등록을 놓지 않았다**.
- **RHI 백엔드 교체를 `App/Rhi/BackendSwapController` 로.** 재진입 가드와 "배수 → 파괴 → 디바이스
  재생성 → 재생성" 순서를 아는 자리를 하나로 뺐다. `gv_rhiBackend` 훅을 `shutdown` 에서 **떼어 낸다** —
  예전에는 끊지 않아 GlobalVariableManager 가 죽은 뒤에도 훅이 App 을 가리켰다(실제로 터지진 않았다).
- **Shipping 에서 셸 ActionMap 을 돌리지 않는다.** `updateShellActions` 는 리소스에서 ActionMap 을
  올려 매 프레임 갱신하는데, Shipping 에는 그것을 질의하는 코드가 **하나도 없었다**.
  `pollReloadHotkeys` 를 `#if !defined( SW_SHIPPING )` 로 통째로 비웠다.
- 헤드리스 분기도 줄었다. `shutdown`/`run` 의 `isHeadless()` 특수 경로는 실제 선행 조건
  (`_window == nullptr`) 과 같았다 — 모드 플래그 대신 조건을 적는다.
- 검증: Debug/Shipping 빌드 경고 0(App 기준), lint 5/5, nogpu 테스트 Debug·Shipping 전부 통과,
  실기동 `-dx12 -EnableEditor` / `-dx11 -EnableEditor` / `-dx12`(에디터 없음) / Shipping `-dx12`
  모두 종료 코드 0 · `[Error]` 0건.
- **남은 것**: 백엔드 **실제 교체**(gv_rhiBackend 런타임 변경)는 에디터 UI 로만 낼 수 있어
  자동 검증을 못 했다. 다음에 에디터를 띄울 일이 있으면 백엔드를 바꿔 보고 `[Error]` 0건을 확인할 것.

**Shipping 이 아예 돌지 않던 문제 (4종)** — Debug 는 전부 조용히 삼키고 있었다.
- `SceneComponent::_pManager` 미초기화 → 소멸자에서 쓰레기 포인터 역참조 (`cdcd92ef`)
- `TypeRegistry` 가 타입 하나당 `TypeInfo` 를 **두 벌**(FQN·짧은 이름) 만들어, 그것을 키로 쓰는
  컴포넌트 풀이 항상 조회 실패 → 풀 메모리를 힙 해제로 반납 → 힙 손상. 즉 컴포넌트 풀은 한 번도
  회수된 적이 없었다 (`7b6d16c0`)
- 리소스 팩 경로·`/WHOLEARCHIVE` 누락(열거형 리플렉션 탈락)·`Archive` 읽기/쓰기 경로 비대칭
  (`eb954da3`)
- 셰이더 베이커가 런타임이 요구하는 퍼뮤테이션을 굽지 않던 문제 + 재발 방지용 `bake.stamp`
  내용 해시 검증 (`6d164397`, `3030c06c`)

**App/Editor 구조 정리**
- App 부팅 시퀀스 분리, ModuleHost 생명주기 중복 제거 (`bdfbf96e`)
- Editor 백그라운드 잡 6벌 → 공통 뼈대 1벌 + 테스트 3개 신설 (`03fb47c9`)
- `EditorUtil` 을 ImGui 에서 분리, 빌드에서 빠져 있던 테스트 복구 (`b4ba276e`, `02b0e7cc`)
- 활성 씬 접근자로 24곳 통일 + 널 검사 없던 역참조 3곳 수정 (`2c237fe2`)
- DXGI 뷰포트 보정을 백엔드 밖으로 (`cb99db8f`)
- 타입 이름·노드 id 변환·도구 패널 순회 통합 (`d9e0a21a`)
- 툴팁을 공용 위젯 하나로 — 지연·줄바꿈이 제각각이었다 (`488f6b4f`)
- 빈 상태 안내 12곳을 `EditorWidgets::drawEmptyHint` 로 (1-1 의 앞부분)

**에디터·Shader 폴더 재편** (`799484f3`)
- `Shader/` 를 `Compile/` · `Reflection/` · `Binding/` 으로. `Renderer/` 를 나눴을 때와 같은 기준.
- `Common/Workspace/` 에 섞여 있던 ImGui 드로잉 둘(토스트·우클릭 메뉴)을 `Common/Gui/` 로,
  세션 상태를 `Common/Workspace/` 로, `EditorCamera` 를 `Viewport/` 로.
- `EditorUtil::isXxxAssetPath` 7개 삭제 — `EditorAssetTypeRegistry::matches` 가 정본인데 입구가
  둘이었다.
- `EditorModule` Unity 빌드 ON. "ODR 충돌이 정리될 때까지" 라던 주석은 이미 유효하지 않았다.

**파일을 옮기면 include 말고도 깨지는 것들** (`799484f3`)
- `ReflectionParser` 가 헤더 이동을 감지 못 했다. `git mv` 는 mtime 을 안 바꾸는데 `.gen.cpp` 는
  원본을 절대경로로 include 한다 → 산출물 머리말의 `// Source:` 경로를 대조하게 했다.
- `sw_skipUnitySources` 가 없는 경로를 조용히 넘겨서 Renderer 재편 이후 제외 6개가 죽어 있었다
  → `FATAL_ERROR`.
- 병합에서 또 드러났다: `Test/EditorTest/CMakeLists.txt` 의 소스 목록. **경로를 문자열로 적어 둔
  곳**은 컴파일러가 안 잡는다.

**빌드 시스템·프레임워크·도구 정리** (`b5264c3e` ~ `3782977d`)
- CMake: `ModuleBuildRules.cmake` 598줄을 역할별 셋으로(BuildLayout / TargetRules / ThirdPartyLibs).
  `sw_flag_libraries` 를 `sw_global_options` 가 흡수해 타겟마다 쓰던 가드 11곳 제거.
  Clang.cmake 의 `$<BOOL:${MSVC}>` 32곳을 `if(MSVC)` 로 — 설정 시점 변수에 제너레이터 표현식은
  필요 없고, 그것 때문에 같은 개념을 드라이버마다 두 번 적고 있었다.
- GameFramework: 키트에서 게임 하나의 스키마를 걷어냈다(기술 슬롯 2칸 고정 → 데이터가 정함,
  보상 2종 고정 → 이름 맵). id 조회를 두 키트가 다른 방식으로 하던 것도 통일.
- ReflectionParser: `logger->shutdown()` 11곳을 RAII 하나로. 생성 파일을 지우면 플레이스홀더가
  최종 산출물로 굳어 영구히 링크가 깨지던 덫을 스탬프로 해소.
- Scripts: CheckSourceGlob 이 빌드 트리를 역알파벳순으로 골라 Unity 트리를 읽고 210개를
  오탐하던 것을 `.clangd` 기준으로. 출력 인코딩을 `common.useUtf8Stdout()` 한 곳으로(9개 전부).

**커밋된 `bake.stamp` 가 소스보다 낡아 있었다.** 네 백엔드 스탬프 모두 `instancesort.hlsl` 의
해시를 옛 값(`cd4ffd32…`)으로 적고 있었다. 구워진 `.dxbc`/`.spv` 자체는 현재 소스와 같았으므로
**기록만** 낡은 것이었고, Shipping 을 빌드할 때마다 작업 트리가 더러워졌다. 스탬프만 갱신해
따로 커밋했다. (처음 관찰에서 "94개 바이너리가 다시 구워진다" 고 적었는데, 그건 그 빌드 디렉터리의
첫 전체 쿠킹이었고 소스 변경 때문이 아니었다.)

**성능은 세 번 재고 세 번 기각했다.** GpuScene 배치 키 중복 계산(3670→3761us, 차이 없음),
린트 ProcessPool 전환(5.3→9.4s, 더 느림), 짝 헤더 파싱 메모이즈(차이 없음). 남긴 변경은
중복 제거·경계 정리 때문이지 성능 때문이 아니다. `GT.GpuScene.build.batches` 는 여전히 게임
스레드의 지배적 비용이고 원인은 못 찾았다 — 다음에 볼 때는 그 안을 더 잘게 재는 것부터.

**DX11 이 앱에서 아무것도 안 그리던 문제** (`913f13ae`)
- `instanceanim` 컴퓨트가 인스턴스 버퍼를 UAV 로 쓴 뒤 정점 셰이더가 SRV 로 읽는데, D3D11 은
  같은 리소스를 출력과 입력에 동시에 걸 수 없어 런타임이 **SRV 를 NULL 로 강제**했다.
  `transitionBuffer` 가 no-op 이라 UAV 를 안 뗐다 — 배리어는 없어도 의무는 있었다.
- 로그에 한 줄도 안 나온 이유: D3D11 은 이 해저드를 WARNING 으로 낸다. 심각도로 거르고 있었다
  → 해저드 ID 만 ERROR 로 올린다.
- 테스트가 못 잡은 이유: 패리티 테스트 씬에 `spinSeed` 가 없어 컴퓨트가 **아예 안 돌았다**
  → `RenderPassTest.InstanceAnimationKeepsInstancesReadable` 신설.
