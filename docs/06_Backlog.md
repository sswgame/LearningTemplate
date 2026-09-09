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

**화면을 볼 수 없을 때는 `-gv_editorPanelDump=N` 을 쓴다.** N 번째 ImGui 프레임에 창 하나당 한 줄
(이름 · 크기 · 정점 수 · 활성/접힘/숨김)과 요약을 로그에 남긴다. **보이는데 정점이 0인 패널**이
곧 빈 패널이고, 컨테이너(자식이 내용을 든 창)와 순수 오버레이(`NoInputs`, 예: ImGuizmo 의 `gizmo`)는
빼고 센다. 패널을 고치기 전후로 이 블록을 비교하면 된다.

```powershell
cd build/Ninja-Debug/Bin
./App.exe -gv_profileFrames=40 -dx12 -EnableEditor -gv_editorPanelDump=25 > before.log
# ... 패널 수정 후 ...
./App.exe -gv_profileFrames=40 -dx12 -EnableEditor -gv_editorPanelDump=25 > after.log
```

현재 기준선: **창 14개 · 내용 없는 패널 0개.** 정점 수가 정확히 같기를 기대하면 안 된다 —
폰트·DPI·도킹·애니메이션이 값을 흔든다. 보는 것은 "0 이 아닌가" 와 "창 목록이 그대로인가" 다.
(도구가 실제로 잡는지 확인했다: `HierarchyPanel::drawContent` 를 즉시 return 으로 막으면
`Hierarchy ... vtx=0 <== BLANK` 와 "내용 없는 패널 1개" 가 나온다.)

```powershell
cmake --build --preset Ninja-Debug            # 경고 0 이어야 한다
cmake --build --preset Ninja-Shipping         # Debug 가 숨기는 결함이 여기서 드러난다
ctest --preset Ninja-Debug-lint               # 컨벤션·include 순서

# 정적 분석 (게이트 아님 — 판단이 필요한 자료다). 검사 목록은 루트 .clang-tidy 가 정한다.
py -3 Scripts/lint/RunClangTidy.py
py -3 Scripts/lint/RunClangTidy.py --filter Core

# ASan (Windows) — 2026-09-09 부터 실제로 빌드된다
cmake --preset Ninja-Debug-ASAN
cmake --build --preset Ninja-Debug-ASAN
ctest --test-dir build/Ninja-Debug-ASAN -L nogpu

# 테스트 (현재 기준선)
#   Debug    : CoreTest 168 / EngineTest 423 / ReflectionTest 100(+1 skip) / EditorTest 34 / SmokeTest 19
#   Shipping : 160 / 421 / 96 / 34 / 1        ← 차이는 전부 Dev 전용 케이스의 정상 스킵
#   ASan     : SmokeTest 는 Disabled (아래 1-3). 나머지 4개는 보고 0건으로 통과하고 전체 21초다.
#   ReflectionTest 의 스킵 1건은 Shipping·Debug 공통이다 — Bin/ 에 ReflectionParser.exe 가 없으면
#   ReflectionParser.MultiBitBitfieldCompilationErrorDiagnosis 가 스스로 빠진다(실패가 아니다).
ctest --test-dir build/Ninja-Debug -L nogpu
ctest --test-dir build/Ninja-Shipping -L nogpu

# 에디터 실기동 — 패널 변경의 유일한 실질 검증 수단
cd build/Ninja-Debug/Bin
./App.exe -gv_profileFrames=40 -dx12 -EnableEditor    # 종료 코드 0, 로그에 [Error] 0건
./App.exe -gv_profileFrames=40 -dx11 -EnableEditor
./App.exe -gv_profileFrames=40 -vk   -EnableEditor
./App.exe -gv_profileFrames=40 -gl   -EnableEditor    # 2026-09-09 부터 여기도 [Error] 0건이다
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

> **2026-09-09 측정 결과 — 표의 전제가 일부 틀렸다.** 세 패널이 "툴바 → 검색 → 목록/표 → 상태줄"
> 이라는 같은 뼈대를 쓴다고 적어 두었지만, `ProfilerPanel` 은 **목록 패널이 아니다** — 탭 +
> `CollapsingHeader` + 통계표다. 검색도 목록도 상태줄도 없다. 그래서 `EditorListPanel` 골격(예전 1-2)을
> 만들어도 이 패널의 94개 호출은 줄지 않는다. 골격을 만들기 전에 나머지 패널도 실제 모양을
> 확인해야 한다(지금 표는 호출 수만 세었다). **→ 확인했다. 3절 "검색 필터" 항목에 결과를 적었다.**
>
> 대신 `ProfilerPanel` 에서 **다른 종류의 문제**를 찾아 고쳤다 — `HierarchyPanel` 뱃지와 같은
> 패턴이다. 컴포넌트 분포표가 타입 이름 5개(`SceneComponent`·`MeshComponent`·`SpriteComponent`·
> `BoxCollider2DComponent`·`CameraComponent`)를 손으로 나열하고 `getComponent<T>()` 로 각각 셌다.
> 결과: 게임이 만든 컴포넌트는 표에 **아예 안 나오고**, 한 오브젝트에 같은 타입이 여럿이어도 1 로
> 세서 "Active Instances" 라는 열 이름과 맞지 않았다. 집계를 `EditorSceneCommands::
> collectSceneStatistics` 로 옮기고(ImGui 무의존 → 테스트 있음) 리플렉션 `TypeInfo` 로 묶는다.
> 표 코드 25줄이 루프 하나가 되고, 엔진·게임이 컴포넌트를 늘릴 때 이 패널을 고칠 일이 없다.
>
> **검증에서 배운 것**: `-gv_editorPanelDump` 의 정점 수는 **클리핑된 내용을 구분하지 못한다.**
> Profiler 패널은 기본 레이아웃에서 높이 139px 이라 분포표가 화면 밖이고, 변경 전후 정점 수가
> 똑같이 1028 이었다. 그려지는 코드는 실행되지만 픽셀은 확인되지 않는다 — 그래서 집계 쪽에
> 단위 테스트를 붙였다. 도구는 "패널이 비었는가" 를 잡고, "표 내용이 맞는가" 는 테스트가 잡는다.

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
아니라 위젯·레이아웃 조립이다. 목록형 골격(예전 1-2)을 만들면 줄어든다고 적어 두었지만, 측정해 보니
골격은 이미 있었다(아래 3절 "검색 필터" 항목). 이 두 패널의 호출 수는 **목록형이 아니라서** 남은
것이므로, 줄이려면 각자의 모양에 맞는 공통부를 따로 찾아야 한다.

### 1-2. clang-tidy 지적 65건 — 분류는 끝났고 판단만 남았다

`py -3 Scripts/lint/RunClangTidy.py` 가 고유 지적 65건을 낸다(처음 훑을 때 110건에서 줄었다).
**이미 전부 한 번 훑었으니 같은 분류를 다시 하지 말 것.** 오탐으로 판정한 자리는
`NOLINTNEXTLINE` + 이유 주석을 달아 두었으므로, 새로 뜨는 것은 분류하지 않은 새 코드다.

| 종류 | 건수 | 판정 |
|---|---|---|
| `clang-analyzer-optin.cplusplus.VirtualCall` | 14 | **대부분 오탐.** 이 엔진은 생성/파괴와 `initialize`/`shutdown` 을 분리하는데 분석기가 소멸자→`shutdown` 을 가상 디스패치 문제로 본다. `Component.cpp:271` 만 한 번 더 볼 값이 있다 |
| `bugprone-macro-parentheses` | 14 | **오탐.** 인자가 **타입 이름**이라 괄호를 씌우면 문법이 깨진다(`sw_new (EditorClass)()`). RuntimeAPI 의 두 매크로는 NOLINT 처리함 |
| `bugprone-branch-clone` | 8 | **오탐.** 본문이 같아도 분기 **순서가 규약**인 자리다(vector 재할당의 이동/복사 우선순위, Windows 전용 DX11·DX12) |
| `bugprone-suspicious-stringview-data-usage` | 5 | **오탐.** `append( data(), count )` 처럼 크기를 함께 넘기는 자리다 — 커스텀 `string` 의 오버로드를 인식하지 못한다. (실제였던 `XmlSerializer` 두 곳은 고쳤다) |
| `clang-analyzer-deadcode.DeadStores` | 4 | 의도된 기본값이거나 영향 없음 |
| `clang-analyzer-security.ArrayBound` | 4 | **기술적으로 UB.** `&float3::_x` 를 `const float32*` 로 넘겨 `[1]`·`[2]` 를 읽는 관용구다. `data()` 를 추가해 **찾은 자리는 전부 옮겼고**(아래 3절) 남은 것은 분석기가 경계를 넘어 추론한 경로다. 더 줄이려면 C-ABI 경계의 시그니처를 `const float3&` 로 바꿔야 한다 |
| `bugprone-implicit-widening-of-multiplication-result` | 4 | 남은 것은 `reserve` 힌트(`propCount * 32`)와 SPIR-V 파서다 — 파서는 위에서 `offset + instrWords > wordCount` 로 경계를 막고 instrWords 가 16비트라 넘칠 수 없다 |
| `clang-analyzer-optin.performance.Padding` | 3 | 구조체 멤버 순서 제안. 영향 낮음 |
| `bugprone-use-after-move` | 2 | **오탐.** `Base{ std::move( other ) }` 는 기반 부분객체만 이동한다 |
| `bugprone-exception-escape` | 2 | `~TaskManager`, `LocalizationManager::operator=`. 둘 다 뮤텍스 락이 이론상 던질 수 있다. 이 저장소는 `/EHsc` 로 예외를 켜 두고 복구 불가 상황은 종료시키는 쪽이라 현재 동작이 의도와 맞다 — 바꿀 근거가 생기면 그때 본다 |
| `bugprone-unhandled-self-assignment` | 1 | **오탐.** `this != &rhs` 가드가 있다 |

### 1-3. ASan: 모듈을 해제한 뒤 적재하면 초기화가 실패한다 (미해결)

`SmokeTest` 는 ASan 빌드에서 **스위트째 Disabled 다**(`Test/SmokeTest/CMakeLists.txt`). 모듈을
올렸다 내리고 다음 모듈을 올리면 두 번째 DLL 의 정적 초기화가 실패한다(`LoadLibrary` → 1114
`ERROR_DLL_INIT_FAILED`). **8/8 결정적이다.**

처음에는 `Architecture.AllRHIModulesAbiStampExports` 한 케이스만 스킵했는데, 그 뒤
`Architecture.LiveReloadGenreKitsIndividuallyAndCascaded` 에서 **같은 결함이 또 났다**(EditorModule 을
내리고 GF 키트를 올리는 경로). 케이스 하나의 문제가 아니라 `LiveReload*` 전반이 걸리므로 스위트
단위로 끈다. 바이너리는 계속 빌드되니 손으로 돌려 볼 수 있고, 원인이 잡히면 그 블록만 지우면 된다.

측정해서 **배제한** 것 — 다시 하지 말 것:

- 비-ASan 빌드는 통과한다(19/19). ASan 에서도 모듈을 **개별로** 올리면 전부 정상이다.
- 장난감 ASan DLL 로 적재→해제→적재: 정상. CRT weak 전역을 일부러 품게 해도 정상. 즉
  "ASan 이 DLL 언로드를 못 버틴다" 는 설명은 **틀렸다.**
- 구조 문제도 아니다. `RHI_DX12.dll` 은 `Logger::registerCaller` 를 `Engine.dll` 에서
  **임포트**한다(Core 사본 중복이 아니다). `registerCaller` 의 경계도 정확하다(배열 512/가드 512).
- `Engine.dll` 이 의존성으로만 올라왔다가 같이 내려가는 변종은 **Engine 을 고정하면 사라진다**
  (`err=1114` → `err=0`). 하지만 SmokeTest 는 Engine 을 링크해 이미 고정돼 있으므로 그 설명은
  여기 맞지 않다 — **남은 트리거를 못 찾았다.** 장난감과 다른 점은 SmokeTest 가 워커 6개가 도는
  멀티스레드 상태라는 것이다.

ASan 의 첫 진단은 CRT 내부 `_Avx2WmemEnabledWeakValue` 의 odr-violation 이고, 보고 도중
`nested bug in the same thread` 로 중단되어 전역 귀속까지 가지 못한다. `handle_segv=0` 으로 두면
맨 세그폴트(139)가 된다 — ASan 이 만들어낸 가짜가 아니라 실제 접근 위반이다.

재현: `build/Ninja-Debug-ASAN/Bin` 에서 ASan 으로 빌드한 12줄 호스트로
`LoadLibrary("RHI_DX11.dll")` → `FreeLibrary` → `LoadLibrary("RHI_DX12.dll")`.

### 1-4. 100줄 넘는 함수 20개 — 우선순위 낮음

분해 자체는 코드 총량을 줄이지 않는다. 공통부 추출(1-1 공용 위젯 채택)을 먼저 한다.
목록이 필요하면 다중 행 시그니처를 중괄호 깊이로 정확히 재는 스크립트를 만들어 뽑는다
(단순 정규식은 여러 줄 시그니처를 잘못 잰다).

### 1-5. 되살리지 못한 테스트

`Test/EditorTest/TestEditorSceneCommands.cpp` 는 되살렸지만(현재 EditorTest 27개에 포함),
`EditorContext` 가 UI 매니저 전부를 `unique_ptr` 로 소유하는 구조는 그대로다. 더 깊은 분리
(패널·팝업 매니저 소유를 컨텍스트 밖으로)는 영향 범위가 커서 하지 않았다. 필요해지면
그때 소유 구조부터 정한다.

### 1-6. 확인만 하고 넘어간 것

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

**요청한 "나머지 문제" 처리 (3건 전부)**

1. **Engine 강결합 해체 (10 → 7)** — 아래 항목.
2. **에디터 패널 검증 수단** — `-gv_editorPanelDump=N` (0절 참고). 공용 위젯 채택·목록형 골격 항목은 이제 화면을
   보지 않고도 검증할 수 있다. 리팩터 자체는 사람이 한 패널씩 가는 편이 맞아 남겨 두었다.
3. **GL 플랫폼 컨텍스트 분리** — `Graphics/RHI/GL/Platform/` 에 `IOpenGLPlatformContext` +
   WGL/GLX/NSGL 세 구현. `OpenGLRHIDevice` 의 플랫폼 분기 **14개 → 0개**. 플랫폼을 하나 더
   지원하려면 파일 한 쌍과 팩토리 한 줄이면 된다(예전에는 멤버 함수 여섯 곳의 `#if` 사다리를
   모두 찾아 고쳐야 했다).
   - Linux/macOS 코드는 **내용을 고치지 않고 그대로 옮겼다.** 여기서 컴파일할 수 없기 때문이다
     (WSL 없음). Linux 는 CI(ubuntu-22.04 × 3) 가 컴파일한다. **macOS 는 CI 에도 없어 여전히
     어디서도 컴파일되지 않는다** — 옮기기 전과 같은 상태다.
   - 동작을 바꾸지 않으려고 "프레임 시작에 컨텍스트 되찾기" 를 `reacquireForFrame()` 로 두고
     WGL 만 구현했다. GLX·NSGL 은 예전에도 여기서 아무것도 하지 않았다.
   - 검증: 네 백엔드(`-dx12 -dx11 -vk -gl`) 모두 종료 코드 0 / `[Error]` 0건.
     GL + 에디터의 에러 3건은 이 변경과 무관한 기존 버그였고(스태시로 기준선을 다시 빌드해
     확인했다), **그 다음에 따로 고쳤다** — 아래 "GL 컨텍스트" 항목.

**전수 조사 2회차 — 분류해 둔 것을 실제로 고친다 (65건까지)**

1회차에서 110건으로 분류해 둔 것을 하나씩 판단해 처리했다. 고친 것은 전부 **실물로 확인한 뒤**
고쳤고, 오탐은 자리마다 이유를 남겼다.

**가장 위험했던 것 — `StringUtil` 이 비-ASCII 바이트를 부호 있게 다뤘다.** `char` 의 부호성은 구현
정의이고 UTF-8 의 0x80 이상 바이트는 signed char 에서 음수다. 그대로 int/uint64 로 넓히면서 둘이 깨졌다:

- `compare` 가 한글처럼 비-ASCII 가 섞인 문자열을 ASCII 보다 **작다고** 답했다(`strcmp` 규약의
  반대다). 같은 함수의 대소문자 **구분** 경로는 이미 `uint8` 로 비교하고 있어서, 두 모드가 서로
  다른 순서를 냈다 — 정렬·이진 검색에 쓰면 일관성이 깨진다.
- `computeHash64` 는 `bIgnoreCase` 경로만 부호 확장됐다. **그 인자는 기본값이 true 라 거의 모든
  호출이 이 경로다.** 같은 바이트가 경로에 따라 다른 값으로 해싱되고, `char` 가 unsigned 인
  플랫폼(ARM)에서는 해시 자체가 달라진다.

둘 다 `uint8` 을 거치게 고쳤다. ASCII 는 동작이 그대로이고(부호 확장이 없다), 바뀌는 것은 비-ASCII
뿐이다 — 저장되는 경로·타입 이름은 소문자 ASCII 로 강제돼 있어 안전하다.
`Core_String.NonAsciiBytesAreUnsigned` 로 고정했다(수정 전이라면 `compare` 가 -119 를 돌려줘 실패한다).
**주의**: 같은 텍스트라서 utf16 오버로드까지 치환됐던 것을 되돌렸다 — UTF-16 코드 단위를 `uint8` 로
자르면 값이 잘린다. 고칠 때 오버로드를 반드시 구분할 것.

**널 역참조 둘.**
- `ImGuiOpenGLRendererBackend::initialize` 가 널·비-OpenGL 을 걸러 `_pRHIDevice` 를 nullptr 로 만들어
  두고도, Windows 분기에서 **검증되지 않은 원시 매개변수**를 역참조했다. 바로 아래 Linux 분기는
  `&& pRhiDevice != nullptr` 로 막고 있었으니 Windows 쪽만 빠진 것이다. 양쪽을 검증된 멤버로 맞췄다.
- `HashedStringPool::shutdown` 이 `SW_ASSERT` 만 믿고 역참조했다. Shipping 에서 단정은 사라지므로
  initialize 전에 또는 두 번 불리면 죽는다. shutdown 은 여러 번 불려도 안전해야 한다.

**`int` 곱셈 후 확대** — `Win32SplashWindow` 의 픽셀 수·바이트 오프셋(포인터 오프셋으로 쓰인다),
`TileMapPanel` 의 `_width*_height`·`indexOf`, `ProfilerPanel` 의 바이트 경계, D3D12 디스크립터 오프셋
8곳. `EventDispatcher` 는 마침 같은 값의 상수(`kDefaultLinearCapacity`)가 이미 있어 그걸 쓴다.

**`float3`/`float4x4` 에 `data()` 와 `static_assert` 를 추가했다.** `&m._11` 을 받아 `[0..15]` 로 읽는
코드가 여럿 있었다 — 형식적으로 배열이 아닌 멤버를 배열로 읽는 것이라 UB 다. `data()` 로 모으면
가정이 한 곳에 있고, `static_assert( sizeof(float4x4) == 16*sizeof(float32) )` 가 그 가정(패딩 없음)을
**컴파일 타임에** 지킨다. 찾은 호출부 4곳을 옮겼다.

**ContentBrowser 의 체커보드를 정수 루프로.** float 변수를 루프 카운터로 써서 반복마다 오차가 쌓였고
(칸 수가 경계에서 달라질 수 있다) 칸 색을 정하려고 다시 나눗셈을 했다. 인덱스로 돌면 위치는 곱셈
한 번, 색은 인덱스 합의 홀짝이다.

**`BlendSpace2D::evaluate` 의 가중치 배열을 0 으로 시작한다.** 위에서 `empty()` 를 걸러 실제로는 쓰기
전에 읽히지 않지만, 그 사실이 `MathUtil::min` 을 거친 `sampleCount` 에 숨어 있어 읽는 사람도 분석기도
확신할 수 없다. 32개 float 을 0 으로 두는 비용은 없다.

**ASan 설정을 두 번 고쳤다 — 측정해 보고 판단이 바뀌었다.**

- `detect_odr_violation` 을 **1 → 0** 으로. 1 로 두면 "크기가 다른 중복만 보고" 하니 진짜 ODR 버그는
  남는다는 계산이었는데, **레벨 1 도 등록된 전역 전체를 훑는 비용은 그대로** 치른다. 모듈을 반복해
  올리고 내리는 SmokeTest 가 비-ASan 6.9초 → ASan **1800초 초과**였고, 0 으로 바꾸자 **5초**였다.
  260배는 오버헤드가 아니라 사용 불가다.
- 타임아웃 배수를 `sw_addTestExecutable` 안에만 두었더니 **손으로 `add_test` 한 `EngineTest_NoGPU` 가
  빠졌다.** 그쪽만 평시 타임아웃을 써서 혼자 타임아웃으로 떨어졌고, "스위트 전체 20초인데 한
  테스트가 시간 초과" 라는 모순된 결과가 나왔다. 보정을 `sw_applySanitizerTestProperties()` 한 곳으로
  빼고 양쪽이 부른다.

결과: ASan 스위트가 **1820초/1건 실패 → 21초/전부 통과**(SmokeTest 는 Disabled).

**잠재 결함 전수 조사 — 도구를 먼저 고쳐야 결함이 보였다**

ASan 과 clang-tidy 를 돌렸더니 **둘 다 쓸 수 없는 상태**였다. 도구를 고치는 것이 조사의 절반이었다.

**Windows ASan 빌드는 한 번도 완주한 적이 없었다** (CI 는 Linux ASan 만 돌린다 — `ci.yml`).
세 군데가 독립적으로 막고 있었고 전부 고쳤다:

1. ASan 런타임 DLL 을 `Bin/` 에만 복사했다. 코드젠 도구는 `BuildTools/` 에 있어 못 찾고
   `0xc0000135` 로 뜨지 못한다 → **빌드가 코드젠 단계에서 죽었다.** 두 곳 모두에 복사한다.
2. `/MD` 를 FORCE 하는데(clang-cl ASan 은 디버그 CRT 를 지원하지 않는다 — `/MDd` 로 바꾸면
   컴파일러가 거부한다) vcpkg 는 Debug 구성에서 `/MDd` 라이브러리를 물려줬다.
   `_ITERATOR_DEBUG_LEVEL` 이 어긋나 `EditorModule.dll` 링크가 섰다. 임포트 구성을 Release 로
   매핑하고 런타임 DLL 도 릴리스 쪽을 직접 staging 한다(applocal 경로는 툴체인에 박혀 있어
   그냥 두면 `z.dll` 이 없어 `CoreTest.exe` 가 뜨지 못한다).
3. `/fsanitize=address` 를 켜면 MSVC STL 이 컨테이너에 ASan 주석을 달고 오브젝트에
   `annotate_string`/`annotate_vector`/`annotate_optional` 표시를 심는다. ASan 없이 빌드된 vcpkg
   라이브러리와 또 어긋난다. 개별 매크로를 막으면 다음 것이 나오므로 우산 매크로
   `_DISABLE_STL_ANNOTATION` 하나로 끈다(잃는 것은 STL 컨테이너 오버플로 탐지뿐이다).

그 위에 ASan 을 **실제로 쓸 수 있게** 한 것:

- **타임아웃.** 평시 기준을 그대로 써서 5개 테스트가 **전부 타임아웃으로 실패**하고 있었다.
  결함처럼 보이지만 설정 문제다. `SW_ENABLE_SANITIZER` 면 10배로 둔다.
- **ODR 오탐.** 플러그인 DLL 이 여럿이고(RHI_*, SWGame, GF_*, EditorModule) 같은 SDK·CRT 헤더를
  포함하니, 헤더가 박는 전역이 DLL 마다 생겨 ASan 이 ODR 위반으로 본다 — 나온 것이 `d3d11.h` 의
  `D3D11_DEFAULT` 와 CRT 내부 `_Avx2WmemEnabledWeakValue` 다. 영구 오탐이므로 CTest 환경에
  `detect_odr_violation=1` 을 둔다. **끄지(0) 않는다** — 크기가 다른 진짜 ODR 버그는 계속 잡힌다.
- 결과: `CoreTest` 167/167, `ReflectionTest` 100, `EditorTest` 34/34 가 ASan 하에서 보고 0건.

**clang-tidy 는 오탐이 신호를 덮고 있었다.** 설정을 저장소에 넣어 구조적으로 막았다:

- `.clang-tidy` — 이 코드베이스에서 **쓸 수 없다고 실측한** 두 검사만 끈다.
  `bugprone-easily-swappable-parameters` 158건, `clang-analyzer-optin.core.EnumCastOutOfRange`
  39건(비트 플래그 조합). 끈 이유를 파일에 적었다.
- `Scripts/lint/RunClangTidy.py` — 한 명령으로 같은 설정. 가장 큰 것은 **`/Y-` 로 MSVC PCH 옵션을
  무효화**한 것이다. clang-tidy 는 그 PCH 를 쓸 수 없는데 `/Yu`·`/FI` 가 남아 헤더를 두 경로 표기로
  두 번 파싱한다. `#pragma once` 가 같은 파일로 보지 못해 **"redefinition of ..." 오류가 쏟아졌다**
  — 정의는 하나뿐인데도. Core/Memory 기준 지적 3건 → 진짜 1건으로 줄었다.
- 결과: 노이즈 포함 ~300건 → 고유 110건. 종류별 판정은 위 1-2 에 적었다.

**그렇게 해서 찾은 실제 결함 (전부 고쳤다)**

- **`BattleState::applyMove` 널 역참조.** 종족·카탈로그 조회가 실패하면 `pMove` 가 nullptr 인데
  `dmg == 0` 분기가 `pMove->_name` 을 무방비로 읽었다. `dmg > 0` 분기는 `pMove != nullptr` 을
  함의해 안전했기 때문에 **정상 데이터로는 절대 걸리지 않았다** — 기술 데이터가 빠지면 크래시다.
- **XML 역직렬화가 길이를 버렸다.** `IXmlBackend::initXmlDeserialization` 이 `const utf8*` 를 받아
  호출부가 `string_view::data()` 를 넘기며 길이를 잃었다. 뷰가 더 큰 버퍼의 일부면 널 종단이 없어
  파서가 끝을 넘어 읽는다. `XmlDocument::parse` 는 원래부터 `string_view` 를 받아
  `load_buffer(data, size)` 로 안전하게 읽으므로 **이 중간 계층만 구멍이었다.** 인터페이스를
  `string_view` 로 바꿔 길이를 끝까지 흘려보낸다(구현체 셋 — 테스트의 `SimpleXmlBackend` 포함).
- **Windows ASan 에서 누수 검사가 도는 척만 했다.** Linux/macOS 는 ASan 이면 LSAN 으로 갈라 두었는데
  **Windows 만 ASan 여부를 보지 않고** CRT 검사를 골랐다. ASan 이 힙을 대체하므로 `_Crt*` 가 전부
  무효가 된다. 유일한 증상이 "set but not used" 경고 둘이었고 Windows ASan 빌드를 처음 돌리고
  나서야 보였다.
- **`int` 곱셈 후 size_t 확대** 네 곳(`TileMap::indexOf`, `TileMap`/`TileMapXml` 의
  `_width*_height`, `Defines.h`·`hashed_string.h` 의 크기 상수). 넘친 뒤 확대되므로 캐스트가 값을
  지켜 주는 것처럼 보이지만 이미 틀린 값이다.
- `FrameDoubleBuffer` 의 기본 용량이 숫자로 박혀 있었다 → 같은 파일이 이미 쓰는
  `constant::kDefaultFrameArenaCapacity` 로.

**확인했지만 결함이 아니었던 것** (같은 의심을 반복하지 않도록): `fixed_string`·`Delegate` 의
자기대입(가드 있음), `Memory.h` 의 `static_assert(sizeof(T)>0)`(불완전 타입 가드 관용구),
`'0'+digit` 좁힘(48–57), DX11·DX12 동일 분기(Windows 전용), `XmlDocument` 의
`append(data(), count)`(크기를 넘긴다), `StringUtil::stristr`(가드 페이지 테스트로 확인 —
`equals` 의 단축 평가가 종단자에서 끊는다).

**검색 필터를 한 곳으로, 그리고 "0건" 을 말하게 한다 (예전 1-2 "목록형 패널 골격")**

백로그에 "여러 패널이 툴바 → 검색 → 목록 → 상태줄을 각자 조립한다" 고 적어 두고 `EditorListPanel`
골격을 만들자고 했다. **전제가 또 틀렸다** — 17개 패널을 세어 보니 조립은 이미 공유되고 있었다:
`EditorChrome::beginToolbar` 10개, `EditorWidgets::drawSearchField` 6개(= 검색 있는 패널 전부),
`drawCountLabel` 4개. 골격을 또 만들면 이미 공유된 것을 한 겹 더 싸는 일이다. 게다가 목록형으로
보이는 패널조차 모양이 제각각이어서(ContentBrowser 2분할, DataTable 탭, GlobalVariables 표 3개,
Hierarchy 트리+드래그드롭+단축키) 하나의 템플릿에 들어가는 패널이 사실상 없다.

**실제로 복사되고 있던 것은 판정 로직이었다.** 검색 가능한 6개 패널이 `matchFilter` 를 각자 썼다 —
"필터가 비면 전부 통과" 가드 + 필드 수만 다른 `stristr` 체인(Console 3 · ContentBrowser 1 ·
DataTable 4 · GlobalVariables 3 · Hierarchy 1 · Inspector 3). 이 가드를 빼먹으면 조용히 반대로
동작한다 — `stristr( x, "" )` 는 nullptr 을 주므로 **필터가 비었을 때 목록이 전부 사라진다.**

- `Common/Widgets/EditorListFilter` — 필터를 한 번 정규화(trim)해 들고 항목마다 판정만 한다.
  ImGui 에 의존하지 않으므로 단위 테스트가 붙는다(EditorTest +5). 필드는 `string_view` 로 받는다 —
  종단자 없는 조각도 안전하다(`stristr` 은 널 종단 문자열만 받는다).
- 6개 패널이 전부 이것을 쓴다. `Source/Editor/Panels` 에 손으로 쓴 검색 술어는 남아 있지 않다.
- `DataTablePanel` 은 행 루프 안에서 필터링하던 것을 표 열기 **전에** 걸러 두도록 바꿨다. 0건 여부를
  알아야 안내를 그릴 수 있고(표가 남은 영역을 다 차지해서 표 뒤에 그리면 화면 밖으로 밀린다),
  행마다 필터를 다시 만들지 않는 효과도 같이 온다.

**진짜 구멍은 "0건" 이었다.** 기존 안내 문구 20곳은 **전부** 데이터·서비스가 없다는 뜻이고,
"검색어가 아무것도 맞히지 못했다" 를 말하는 곳은 **한 곳도 없었다.** 아무것도 맞지 않는 검색어를
치면 설명 없는 빈 상자가 남는다 — 고장처럼 보인다. 복사할 선례가 없으니 새 패널도 똑같이 빠뜨린다.
`EditorWidgets::drawNoSearchResultHint( filter )` 를 만들어 6개 패널에 붙였다. `ConsolePanel` 은
0건의 이유를 셋으로 나눈다(로그가 아직 없음 / 검색어가 걸러냄 / 레벨을 전부 끔) — 고치는 방법이
서로 다르기 때문이다.

**이번에는 UI 변경을 실제로 측정했다.** 생성자에서 필터를 아무것도 안 맞는 값으로 시작시켜
`-gv_editorPanelDump` 를 찍고, 힌트만 끈 빌드와 비교했다(측정 후 계측은 걷었다):

```
힌트 없음: Output Log/##log_scroll  vtx=0  <== BLANK      ← 빈 패널 1개, 경고 발생
          Content Browser/##cb_assets vtx=24
힌트 있음: Output Log/##log_scroll  vtx=104               ← 빈 패널 0개
          Content Browser/##cb_assets vtx=128
```

**도구가 실제 결함을 잡았다** — 맞지 않는 검색어가 패널을 진짜로 비웠고, 힌트가 그것을 없앤다.
다만 `Hierarchy`·`Inspector`·`DataTable`·`GlobalVariables` 의 0건 분기는 **런타임에서 확인하지
못했다**: 활성 게임이 `Empty` 뿐이라 씬 오브젝트가 없어 Hierarchy 는 "No active scene." 로 빠지고,
Inspector 는 선택이 없어 "Nothing selected." 로 빠지며, DataTable 은 도구 패널이라 닫힌 채 뜬다.
이 네 곳은 술어 단위 테스트 + 코드 검토까지다. 씬이 있는 게임이 생기면 같은 방법으로 확인할 수 있다.

**곁가지로 확인한 것 — `StringUtil::stristr` 은 정상이다.** `string_view( pStr, subLen )` 를 만들어
비교하므로 남은 길이가 검색어보다 짧으면 종단자를 지나 읽을 것처럼 보였다. 가드 페이지 테스트로
확인하니 **넘어가지 않는다** — `equals` 의 비교 루프가 첫 불일치에서 끊기고, 널 종단자는 검색어의
어떤 문자와도 반드시 불일치한다. 안전한 이유가 단축 평가라는 구현 세부에 걸려 있으므로(비교를 여러
바이트씩 처리하도록 "최적화" 하면 바로 깨진다) 테스트는 남겼다: `Core_String.StristrStopsAtTerminator`.

**GL 컨텍스트를 기다려서 가져온다 — `-gl -EnableEditor` 에러 3건 해결**

`-gl -EnableEditor` 이 늘 뱉던 에러 3건(`wglMakeCurrent failed (err=170)` ×2 +
`Failed to resolve GL texture`)이 **한 원인**이었고 GL 안에서 끝났다. 네 백엔드 × 에디터
유무 8조합 모두 `[Error]` 0건이다.

**원인.** GL 디바이스는 `requiresExclusiveContextThread()==true` — 컨텍스트는 한 스레드만
current 로 가질 수 있고, 렌더 워커가 프레임마다 쥐고 놓는다(`RenderThread::executePacket`).
그 사이에 다른 스레드가 GL 리소스를 만들려 하면 `wglMakeCurrent` 가 `ERROR_BUSY` 로 실패한다.
문제는 실패를 다룬 방식이었다 — `ScopedOpenGLContext` 가 로그만 남기고 `_bNeedsUnbind=false`
로 두는데, **가드의 본문은 그대로 실행됐다.** 그래서 컨텍스트 없이 `glGen*` 이 나가 리소스가
조용히 만들어지지 않고, 한참 뒤 "Failed to resolve GL texture" 로 드러났다. 세 에러가 한 뿌리다.

**고친 방법.** 한 번 시도하고 포기하는 대신 **차례를 기다린다.** 워커가 프레임 끝마다 놓으므로
한 프레임 안에 온다.
- `OpenGLRHIDevice::acquireGraphicsContextBlocking( timeoutMs=250 )` 를 추가했다 — 1ms 간격으로
  조용히 다시 집고, 제한 시간을 넘길 때만 로그를 남긴다.
- `ScopedOpenGLContext` 가 이것을 쓴다.
- 플랫폼 `makeCurrent()` 는 **실패해도 로그를 남기지 않는다** (경합은 정상이다. 재시도마다
  에러를 찍으면 정상 동작이 오류로 보인다 — 실제로 그 2건이 그랬다). 알릴 책임은 호출부로
  옮겼다: 기다리지 않는 `bindGraphicsContext()` 는 예전처럼 실패를 로그한다.

**앞선 진단이 너무 넓었다.** 이 자리에 있던 1-3 은 원인을 "UI 스레드가 GL 컨텍스트를 요구하는
경로가 둘" 로 보고 "어느 스레드가 컨텍스트를 갖는가" 라는 규약을 새로 세워야 한다고 적었다.
호출부마다 태그를 붙여 측정하니 **에디터 백엔드 `initialize`/`shutdown` 과
`OpenGLRHICommandList::beginCommandList` 는 한 번도 실패하지 않았다** — 실패 2건은 전부
`ScopedOpenGLContext` 였다. 스레드 소유권 재설계는 필요하지 않았다.

**요청한 "나머지 문제" 처리 — Engine 강결합 해체 (10 → 7)**
자세한 내용은 아래 3절의 해당 항목과 `Source/Engine/README.md` 의 티어 표에 있다.
부수로 드러난 버그 둘도 같이 고쳤다:
- **초기화 실패 경로가 SEGFAULT 로 끝났다.** 요청한 백엔드가 이 빌드에 없으면
  `RHI` 객체는 생기지만 디바이스가 없는데, `EngineLoop::shutdown` 이 `hasDevice()` 검사 없이
  `getDevice()` 를 역참조했다. 설정에 `DirectX11` 을 적은 Shipping 빌드(DX12 전용)에서 재현되고,
  Debug 에서도 `RHI_DX11.dll` 을 숨기면 같다. 이제 정상 종료(`main` 의 `return -1`)한다.
- **그 오류 메시지가 `No factory registered for backend 0` 이었다.** 설정 파일을 고친 사람이
  원인을 알 수 없다 → `DirectX11 백엔드는 이 빌드에 없습니다. 사용 가능: DirectX12`.

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
- **하지 않은 것**: 공용 위젯 채택(1-1)·목록형 패널 골격. 이 둘은 검증 수단이 **화면을 보는
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

> ### Engine 코어 묶음 — 열에서 일곱으로 줄였다 (남은 것은 컴포넌트 모델 설계)
>
> **해결:** `Reflection`·`Serialization`·`Config` 가 묶음에서 빠졌다. 원인은 세 줄이었다.
>
> - 직렬화기가 `TagID`·`ComponentHandle` 때문에 `Object` 를 include 했다. 두 타입 모두 **Core
>   기능만 쓰는 값 타입**인데 `Object/Component/` 에 분류되어 있었다 → `Core/String/TagID.h`,
>   `Core/Container/ComponentHandle.h`(형제 `ObjectHandle` 옆). 이 엣지 하나로 10→8 이 됐다.
> - `Reflection` 이 `ReflectAny`·`Rpc` 의 **인코딩** 때문에 `Serialization` 을 include 했다 →
>   규칙 하나로 정리: **리플렉션 타입의 인코딩은 Serialization 이 갖는다**
>   (`SerializeReflectAny.cpp`, `SerializeReflectionRpc.cpp`). 선언은 Reflection 에 남는다.
> - `EngineConfig` 가 `RHIBackend` **이름 하나** 때문에 `RHITypes.h`(732줄)를 전부 끌어왔다 →
>   `Config/RHIBackendType.h`. "어느 백엔드를 쓰는가" 는 설정값이고 `Graphics` 는 이미 `Config` 를
>   참조한다(14곳). 8→7.
>
> 이제 아래 절반이 **완전히 정렬**된다: Common/Physics/Utility → Reflection → Serialization →
> Config → 코어 7 → Input → 루트. `CheckEngineLayers` 의 티어 표를 그대로 갱신했고 위반은 0이다
> (음성 테스트로 잡히는 것도 확인했다).
>
> **남은 7 묶음은 성격이 다르다** — 컴포넌트가 머티리얼을 들고 씬이 에셋을 읽는 것은 컴포넌트
> 모델 자체의 설계다. 측정된 엣지:

> ### (옛 측정) Engine 코어 열 폴더는 하나의 강결합 묶음이다
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
