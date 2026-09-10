# 작업 백로그 — 남은 일과 이어받기

> 목적: 여러 PC·여러 세션에서 이어서 작업하기 위한 **단일 할 일 목록**이다. 무엇이 끝났고
> 무엇이 남았는지, 남은 것을 왜 그 순서로 두었는지, 손대기 전에 알아야 할 함정이 무엇인지를
> 여기 적는다. 작업을 끝내면 이 문서의 해당 항목을 지우거나 "완료"로 옮기고 같이 커밋한다.
>
> 마지막 갱신: 2026-09-10 · 기준 커밋 `f9fc5d0f`

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

**도구 패널까지 재려면 `-gv_editorOpenAllPanels=1` 을 같이 준다.** 기본 레이아웃에는 도구 패널
(Sequencer·Material·Prefab·TileMap·SpriteClip·AnimGraph·DialogueGraph·DataTable·InputMap)이 닫혀
있어서 덤프가 다섯 개만 본다. 이 스위치는 (1) 등록된 패널을 전부 열고, (2) 저장된 도킹 레이아웃을
적용하지 않으며(도킹하면 같은 노드의 탭 중 앞의 하나만 그려진다), (3) 첫 사용 크기를 900×620 으로
준다(ImGui 기본 크기는 내부 Child 를 9px 로 눌러 "내용 없음" 오탐을 만든다 — 실제로 봤다). 켠
실행의 가시성·레이아웃은 **저장하지 않는다.**

```powershell
./App.exe -gv_profileFrames=60 -dx12 -EnableEditor -gv_editorOpenAllPanels=1 -gv_editorPanelDump=40
```

현재 기준선: **기본 창 14개 · 내용 없는 패널 0개**, 전부 열면 **창 29개 · 내용 없는 패널 0개**.

> ⚠️ **이 실기동 검증은 빈 씬을 본다.** `SW_ACTIVE_GAME=Empty` 는 맵이 없고 저장소에
> `.scene.xml` 자체가 없다 — 로그를 보면 `SceneManager` 가 씬 없이 Initialized → Shut down 한다.
> 따라서 이 게이트는 **오브젝트를 도는 코드 경로를 전혀 태우지 않는다**: 뷰포트 피킹, 컴포넌트
> 시각화, Hierarchy 트리, Profiler 의 컴포넌트 분포표, 씬 세대 변경 훅
> (`syncAfterSceneGenerationChange`) 모두 해당한다. 정점 수 비교가 증명하는 것은 그리드·통계·
> 방향 큐브처럼 **오브젝트와 무관한** 그리기가 그대로라는 것까지다.
>
> 오브젝트 경로는 단위 테스트로 덮는다(피킹은 `TestEditorViewportPick` 7케이스). 실기동으로도
> 덮고 싶으면 **작은 테스트 씬 애셋**이 필요하다 — 오브젝트 몇 개(메시·스프라이트·콜라이더·
> 카메라 하나씩)와 프리팹 인스턴스 하나면 위 경로가 전부 켜진다. 콘텐츠를 추가하는 결정이라
> 여기서는 하지 않았다.

(전부 열었을 때 한 번 나왔던 `Sequencer/00000379` 는 확인해서 닫았다 — ImSequencer 가 함수 앞머리에서
`GetWindowDrawList()` 를 잡아 두고 `BeginChild( 889 )`(=0x379) 안에서도 그 리스트에 그리기 때문에
자식의 정점이 0 이다. 라이브러리가 **정수 id 로 만든 자식 창**은 우리가 판별할 수 없으므로 세지
않는다 — 우리 자식 창은 늘 문자열 id 를 쓴다.) 정점 수가 정확히 같기를 기대하면 안 된다 —
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
#   Debug    : CoreTest 169 / EngineTest 424 / ReflectionTest 100(+1 skip) / EditorTest 51 / SmokeTest 19
#   Shipping : 161 / 422 / 96 / 51 / 1        ← 차이는 전부 Dev 전용 케이스의 정상 스킵
#   ASan     : 5개 전부 통과한다(30초). SmokeTest 는 2026-09-10 부터 다시 돈다 — 아래 3절 참고.
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

### 1-0. 뷰포트 뷰 모드(Lit/Unlit/Wireframe)를 렌더러에 연결한다

뷰포트 툴바 맨 앞의 콤보는 **아무 일도 하지 않았다** — `ViewportToolbarSettings::_renderMode` 를
읽는 코드가 어디에도 없다(쓰기만 하고, 콤보 자신이 되읽을 뿐이었다). 고르면 값만 바뀌고 화면은
그대로였다. 지금은 **비활성 + 툴팁**으로 사실을 표시해 두었다(거짓 컨트롤보다 정직한 비활성).

연결하려면 렌더러 작업이 필요하다:

- RHI 는 **네 백엔드 모두 준비되어 있다** — `RHIFillMode::Wireframe` 을 DX11·Vulkan·GL 이 읽고,
  DX12 도 이제 읽는다(예전에는 `D3D12_FILL_MODE_SOLID` 로 못박혀 있었다. 아래 3절 참고).
- 없는 것은 그 위 계층이다: `RenderPipelineResource`/`RenderPassResource` 에 채우기 모드가
  노출되지 않고(`Graphics/Renderer` 어디에도 `fillMode` 가 없다), 와이어프레임 PSO 변형도,
  프레임 렌더러가 뷰 모드를 고르는 경로도 없다.
- Unlit 은 셰이더/파이프라인 변형이 더 필요하다(조명 항을 빼는 패스나 셰이더 순열).
- 연결한 뒤에는 콤보의 `BeginDisabled`/툴팁을 걷고, 네 백엔드에서 실기동으로 확인할 것.

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

**2026-09-10: 텍스트 입력을 공용 위젯으로 옮겼다.** 표에 적힌 "공용 위젯 사용 0" 은 채택률
문제로 보였지만, 실제로 8개 패널에 복사돼 있던 것은 위젯이 아니라 **임시 버퍼를 만들어 넣었다
빼는 다섯 줄**이었다(`fixed_string<N> buf{ text.c_str() }` → `InputText` → 되돌려 담기). 크기를
자리마다 골랐고(64·128·256·512) 그 크기를 넘으면 잘렸다 — 게다가 `fixed_string` 은 넘치는 입력에서
버퍼 밖을 쓰고 있었다(3절). `EditorWidgets::drawTextField( label, string&, width )` 하나로 16곳을
옮겼고, 손으로 만든 임시 버퍼 InputText 는 남아 있지 않다.

**2026-09-10: 상태색을 테마에서 가져오게 했다.** 남은 호출 수를 뜯어 보니 `TextColored` 가
`InputMapEditorPanel` 에만 23개였고, 전부 `ImVec4` 리터럴을 자리마다 새로 적고 있었다 — 같은 뜻의
**초록이 여섯 가지**(0.2/1/0.2, 0.2/1/0.3, 0.2/1/0.5, 0.2/1/0.4, 0.4/1/0.4, 0.35/0.85/0.35),
호박색 여섯, 빨강 다섯이다. 그런데 `EditorThemeUtil` 에는 이미 테마별 상태색과
`textSuccess`/`textWarning`/`textError`/`textMuted` 가 있었고 **아무도 쓰지 않았다**(색 게터만
ContentBrowser 가 썼다). 즉 테마를 바꿔도 이 글자들은 안 바뀌었다. 상태 의미가 분명한 23곳을
테마 API 로 옮기고, 서식이 필요한 자리를 위해 `pushTextColor`/`popTextColor` 와 `textInfo` 를 더했다.
남긴 것: Dialogue 노드 뱃지(START/CHOICE/BRANCH…)와 플랫폼 브랜드색은 **분류색**이지 상태색이
아니다 — 상태색으로 접으면 뜻이 사라진다.

**측정해서 기각한 것 — 표(Table) 골격.** `BeginTable` + `TableSetupColumn` × N + `TableHeadersRow`
가 15곳에 있어 공통부처럼 보였다. 세어 보니 3열 표는 5문장 → 골격을 쓰면 배열 3줄 + desc 4줄로
**늘어난다.** 7열 표에서만 이득이고 전체로는 60줄 남짓이며, 대신 어떤 ImGui 플래그가 걸리는지가
한 겹 숨는다. 안 만든다.

**복사된 블록도 찾아봤다** — Editor 전체에서 5줄 이상 동일한 블록을 서로 다른 파일 간에 훑으니
`}` · `namespace` 닫기 같은 뼈대만 나왔다. 남은 호출 수는 **패널마다 고유한 조립**이지 중복이
아니다. `InputMapEditorPanel` 302개의 절반은 `SameLine` 37 · `Text` 36 · `Button` 32 · `Separator` 23
이다 — 이걸 감싸면 읽기만 나빠진다. 1-1 은 여기서 닫는다. 목록형 골격(예전 1-2)을 만들면 줄어든다고 적어 두었지만, 측정해 보니
골격은 이미 있었다(아래 3절 "검색 필터" 항목). 이 두 패널의 호출 수는 **목록형이 아니라서** 남은
것이므로, 줄이려면 각자의 모양에 맞는 공통부를 따로 찾아야 한다.

### 1-2. clang-tidy 지적 — **버전마다 다른 숫자가 나온다**

`py -3 Scripts/lint/RunClangTidy.py` 를 쓴다. 두 PC 가 같은 날 같은 코드를 훑고 **"0건" 과 "72건"**
이라는 다른 답을 받았다. 둘 다 맞다 — clang-tidy 버전이 다르면 검사 목록이 다르다. 그래서 숫자만
적으면 다음 사람이 헷갈린다. 버전을 같이 적는다.

- **clang-tidy 20 기준(한쪽 PC)**: 110 → 65 → 20 → **0건**. 고칠 수 있으면 고치고, 구조적으로
  불가능한 자리는 `NOLINTNEXTLINE` + 이유를 남겼다.
- **clang-tidy 22 기준(다른 PC)**: 같은 코드가 **308건**이 된다. 늘어난 243건은 22 에서 새로 생긴
  검사 넷이 전부다. 하나씩 훑어 **72건**까지 줄였다(아래 표).

**이 PC 에 clang-tidy 가 아예 없기도 했다** — 저장소가 받아 두는 `Tools/LLVM` 은 clang-tidy 를 뺀
축소판이라 스크립트가 "찾지 못했습니다" 로 끝났다. 이제 Visual Studio 가 같이 설치하는
LLVM(`VC/Tools/Llvm/x64/bin`)까지 찾는다.

**새로 뜨는 지적은 분류하지 않은 새 코드다.** 판단해서 고치거나, 의도한 것이면 그 자리에
`NOLINTNEXTLINE` 과 **이유**를 함께 남긴다. 이유 없는 NOLINT 는 다음 사람이 되살리고 같은 분류를
다시 하게 만든다.

검사 목록은 `.clang-tidy` 가 정한다. 끈 검사는 다섯이고 각각 왜 이 코드베이스에서 쓸 수 없는지
실측과 함께 적혀 있다: `easily-swappable-parameters` · `EnumCastOutOfRange` · `Padding` ·
`invalid-enum-default-initialization` · `derived-method-shadowing-base-method` ·
`std-namespace-modification`.

**clang-tidy 22 의 새 검사 넷은 이렇게 처리했다:**

| 종류 | 처리 |
|---|---|
| `bugprone-throwing-static-initialization` 179 | **150건은 `SW_LOG_CALLER` 하나가 냈다.** `Logger::registerCaller` 를 `noexcept` 로 만들어 없앴다 — 고정 배열과 뮤텍스뿐이라 실제로 던질 것이 없고, 정적 초기화에서 부르는 함수라는 계약을 타입에 못박는 편이 맞다. 남은 29건은 전역 변수 등록자·설정 싱글턴처럼 **시작 시 실패가 곧 종료**인 자리다 |
| `bugprone-invalid-enum-default-initialization` 48 | **껐다.** `D3D11_RASTERIZER_DESC desc{}` 처럼 SDK 구조체를 0 으로 비우면 그 안의 열거형이 짚힌다 — 열거형 19종이 전부 D3D11/D3D12/Vulkan 타입이고 우리 열거형은 0건이다(전수 확인) |
| `bugprone-derived-method-shadowing-base-method` 11 | **껐다.** 11건 전부 `REFLECT_BODY()` 가 만드는 `swReflectSelf` 다(전수 확인). NOLINT 는 매크로 안에 넣을 수 없어 REFLECT 를 쓰는 모든 자리에 붙여야 한다 |
| `bugprone-std-namespace-modification` 10 | **껐다.** 10건 전부 `tuple_size`/`tuple_element`/`hash`/`equal_to` 를 프로그램 정의 타입에 특수화한 것으로 `[namespace.std]` 가 허용한다(전수 확인). 그중 `tuple_size<FormattedValue<T>>` 만 `integral_constant<uint32,2>` 였어서 표준대로 `size_t` 로 맞췄다 |
| `bugprone-command-processor` 1 | **고쳤다.** ContentBrowser 의 "Show in Explorer" 가 `system()` 이었다 — 셸을 거쳐 경로의 `&`·`"` 가 명령으로 해석되고 콘솔 창이 깜빡였다. `EditorAssetCommands::showInFileExplorer` 로 옮겨 셸 없이 프로세스를 띄운다(macOS `open -R`, 리눅스 `xdg-open` 도 같이) |

**분류만 해 두었던 것도 다시 봤다. 절반은 오탐이 아니라 "확신할 수 없게 쓰인 코드" 였다.**

- `bugprone-use-after-move` — `StringBuilder::appendFormat` 은 **재시도 루프 안에서 같은 인자 팩을
  다시 forward** 했고, `TaskFuture::setContinuation` 은 옮긴 델리게이트를 bool 플래그에 기대어 다시
  읽었다. 둘 다 억제가 아니라 코드로 풀었다.
- `clang-analyzer-security.ArrayBound` — `&vec._x` 를 넘겨 인덱스로 읽던 **세 자리는 시그니처를
  `const float3&`/`const float4&` 로 바꿔** UB 자체를 없앴다(내부 호출부뿐이라 C-ABI 제약이 없었다).
  남은 하나는 `float4x4::data()` 를 쓰는 자리라 NOLINT + 이유로 닫았다 — 레이아웃은
  `MatrixMath.h` 의 `static_assert( sizeof(float4x4) == 16 * sizeof(float32) )` 가 지킨다.
- `bugprone-implicit-widening-of-multiplication-result` — 명시적 캐스트.
- `clang-analyzer-deadcode.DeadStores` — Material 의 죽은 계산은 **미완성 코드의 흔적**이었고(빈
  `if` 와 짝), 에디터의 붙여넣기는 **실패를 아무도 읽지 않고 있었다**. 열거형→이름 두 곳은 값을
  먼저 넣고 switch 로 덮어쓰는 대신 돌려주는 함수로 바꿔 억제 없이 없앴다.
- `bugprone-unhandled-self-assignment` — `fs = fs.c_str()` 가 자기 버퍼를 자기에게 memcpy 하고
  있었다(가드 + 테스트 추가). 같은 파일의 다른 자리는 NOLINT 가 `template` 줄에 가려 적용되지 않고
  있었다 — **NOLINTNEXTLINE 은 진단이 붙는 줄 바로 위여야 한다.**

clang-tidy 22 에서 남은 것(합 72건, 이 병합 이후 `Padding` 을 끄면 69건이 된다 — 다시 재야 한다):

| 종류 | 건수 | 판정 |
|---|---|---|
| `bugprone-throwing-static-initialization` | 29 | 전역 변수 등록자·설정 싱글턴. 시작 시 실패가 곧 종료다 |
| `clang-analyzer-optin.cplusplus.VirtualCall` | 13 | **오탐.** 생성/파괴와 `initialize`/`shutdown` 을 분리하는 구조를 분석기가 가상 디스패치 문제로 본다(파괴 중 호출 12곳은 클래스 이름으로 한정해 의도를 코드로 적었다) |
| `bugprone-macro-parentheses` | 12 | **오탐.** 인자가 **타입 이름**이라 괄호를 씌우면 문법이 깨진다(`sw_new (EditorClass)()`) |
| `bugprone-branch-clone` | 8 | **오탐.** 본문이 같아도 분기 **순서가 규약**인 자리다 |
| `bugprone-suspicious-stringview-data-usage` | 5 | **오탐.** `append( data(), count )` 처럼 크기를 함께 넘긴다 |
| `clang-analyzer-optin.performance.Padding` | 3 | 이 병합으로 `.clang-tidy` 에서 껐다 — 인스턴스가 하나뿐인 매니저·정적 표라 아끼는 양이 무의미하다 |
| `bugprone-exception-escape` | 2 | `~TaskManager`, `LocalizationManager::operator=`. 뮤텍스 락이 이론상 던진다 — 현재 동작이 의도와 맞다 |

### 1-3. 100줄 넘는 함수 20개 — 우선순위 낮음

분해 자체는 코드 총량을 줄이지 않는다. 공통부 추출(1-1 공용 위젯 채택)을 먼저 한다.
목록이 필요하면 다중 행 시그니처를 중괄호 깊이로 정확히 재는 스크립트를 만들어 뽑는다
(단순 정규식은 여러 줄 시그니처를 잘못 잰다).

### 1-4. 되살리지 못한 테스트

`Test/EditorTest/TestEditorSceneCommands.cpp` 는 되살렸지만(현재 EditorTest 27개에 포함),
`EditorContext` 가 UI 매니저 전부를 `unique_ptr` 로 소유하는 구조는 그대로다. 더 깊은 분리
(패널·팝업 매니저 소유를 컨텍스트 밖으로)는 영향 범위가 커서 하지 않았다. 필요해지면
그때 소유 구조부터 정한다.

### 1-5. 확인만 하고 넘어간 것

Shipping `EngineTest` 에서 `RHITest.CommandListCreationAndExecution` 이 **한 번** SEGFAULT
했다. EngineTest 는 Editor 를 링크하지 않으므로 에디터 변경과는 무관하다.

**2026-09-10 재현 시도: 8회 모두 통과했다** — 그 케이스만 5회, Shipping `EngineTest` 전체 3회
(421/423, 2 스킵). 한 번 본 것으로 "확정" 하지 않는 것과 같은 이유로, 8회 통과했다고 없는 일이
되는 것도 아니다. 다시 보이면 그때는 재현율부터 재고, 그 테스트가 실제로 무엇을 태우는지를 먼저
본다(아래 2026-09-09 메모).

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

## 3. 최근에 끝낸 일 (2026-09-08 ~ 10)

무엇을 이미 해결했는지 알아야 같은 것을 다시 파지 않는다.

### 2026-09-10 (씬을 바꿔도 이전 씬을 가리키는 상태가 남아 있었다)

"선언만 있고 아무도 부르지 않는 함수" 를 훑었다(Editor 헤더 선언 528개 → 트리 전체 등장이
2회 이하인 것 **27개**). 대부분은 그냥 죽은 편의 API 였지만, `EditorWorkspace` 의 세 개는
**불려야 하는데 안 불리고 있었다** — 이름 그대로 씬이 바뀔 때 버려야 하는 상태다.

`EditorAssetCommands::syncAfterSceneGenerationChange` 는 씬 세대가 바뀐 것을 아는 **유일한**
자리다(`setObservedSceneGeneration` 호출부가 여기뿐이다). 여기서 dirty·선택을 지우고 프리팹
맵을 다시 만들지만, 다음 셋은 그대로 남겼다. 오브젝트 ID 는 `GameObjectManager` 마다 다시
시작하므로 남은 상태는 **새 씬의 엉뚱한 오브젝트에 붙는다**:

- **Undo 스택** — 커맨드가 든 XML 스냅샷은 사라진 씬의 것이다. Edit 메뉴는 Undo 를 켜 둔 채로
  두고, 누르면 아무 일도 없거나(guid 조회 실패) 재사용된 ID 를 통해 다른 오브젝트를 덮어쓴다.
  (`EditorPlaySession` 은 플레이 전이마다 비운다 — 씬 전환만 빠져 있었다.)
- **GUID 맵**(`clearGuidMap`) — Undo·PIE 복원이 오브젝트를 다시 찾는 신분증이다
  (`findGameObjectByGuid`). 낡은 `guid → 옛 ID` 가 남으면 새 씬에서 같은 ID 를 쓰는 오브젝트가
  잡히고, `getOrAssignGuid` 는 새 오브젝트에 **옛 guid** 를 돌려준다. 씬을 갈아탈수록 늘기도 한다.
- **프리팹 Isolation**(`clearPrefabIsolation`) — 프레임이 옛 씬의 오브젝트 ID 를 든다. 격리 중에
  씬을 열면(막는 가드가 없다) `isPrefabIsolationActive()` 가 계속 true 라 UI 는 격리 중이라 믿고,
  `exitPrefabIsolation` 이 새 씬의 무관한 오브젝트를 되살린다.

세 clear 함수는 모두 순수 상태 리셋이라(오브젝트를 만지지 않는다) 사라진 씬에 대해 부르는 것이
정확히 맞다. 프리팹 맵만 버리지 않고 다시 만든다(새 씬에도 인스턴스가 있다).

**검증의 한계 — 중요**: 이 훅은 **빈 씬 실기동에서 한 번도 실행되지 않는다.** 활성 게임이
`Empty` 라 씬이 로드되지 않아 세대가 0에 머문다(임시 로그로 확인: 60프레임 동안 훅 미발동).
그래서 이 수정의 근거는 코드 수준이다 — 세 함수의 본문, 훅이 유일한 세대 감지 지점인 것,
GUID 맵/Isolation 프레임이 ID 로 옛 씬을 가리키는 것을 읽어서 확인했다. 실기동은 "빈 씬
경로를 깨지 않는다" 까지만 보증한다. 0절에 이 게이트의 사각지대를 적어 두었다.

**측정해 둔 죽은 API 27개** (지우는 것은 판단이 필요해 두었다):
`EditorThemeUtil` 9개(`getBorderColor`·`getPanelBgColor`·`getWindowBgColor`·`push/popAccentButton`·
`push/popAccentHeader`·`textAccent`·`textMuted`) · `EditorWidgets` 4개(`acceptAssetDrop`·
`drawHelpMarker`·`drawPropertyRowBegin/End` 짝 전체·`drawSearchFilter` — 이건 쓰이는
`drawSearchField` 의 죽은 사촌이라 잘못 부르기 쉽다) · `AssetEditorManager` 2개(등록 확장점) ·
`EditorWorkspace` 의 `removeGuid`·`isGameObjectPrefabInstance`·`clearGameObjectPrefabMap`
(마지막은 rebuild 가 대신하므로 정상) · `EditorTransaction::captureBinarySnapshot` ·
`EditorInspectorCommands::pushStringEdit` · `EditorDataTableCommands::hasModifiedLocalization` ·
`EditorGlobalVariableCommands` 2개 · `EditorAssetTypeRegistry::getPanelMappings`.

**검증**: Debug·Shipping 경고 0, nogpu 5/5(양쪽), 린트 6/6, 네 백엔드 실기동 종료 코드 0 ·
`[Error]` 0건 · `Game View` 정점 수 742 동일, 전부 열기 창 29개/빈 패널 0개.

### 2026-09-10 (핫 리로드·종료 때 언맵된 DLL 로 뛰는 Undo 스택, 그리고 조용한 실패 셋)

**Undo 스택이 에디터 모듈보다 오래 산다.** `CommandStack` 은 `EngineLoop` 이 소유하고
(`_commandStack`), 거기에 쌓는 것은 **전부 에디터**다 — `EditorTransaction` 이 넣는 커맨드는
패널의 `this` 를 잡은 **람다** 델리게이트이고, 그 람다의 코드와 소멸자
(`Delegate::_managerFunc`)는 `EditorModule.dll` 안에 있다. 그런데 `ImGuiEditor::shutdown()` 이
이 스택을 비우지 않았다. 그래서 두 경로가 언맵된 이미지로 뛴다:

- **핫 리로드**: EditorModule 을 다시 컴파일하면(Build 메뉴에 있다) DLL 이 언맵·재맵되는데
  스택에는 옛 이미지의 델리게이트가 남는다 → 그 뒤 `Ctrl+Z` 가 사라진 코드를 부른다.
- **종료**: `App::shutdown()` 이 `_moduleHost->shutdown()`(모듈 내림) → `_engineLoop.shutdown()`
  (`_commandStack.reset()`) 순서다. 즉 델리게이트 **소멸자**가 언맵된 DLL 로 점프한다
  (`Delegate::release()` 가 `_managerFunc` 를 부른다 — 람다 델리게이트만 이 필드가 채워진다).

넣은 쪽이 치우게 했다 — `ImGuiEditor::shutdown()` 이 모듈이 내려가기 전에 스택을 비운다.
`EditorPlaySession::setState` 가 전이마다 같은 이유로 비우고 있어 선례도 있고, 에디터 밖에서
이 스택에 push 하는 코드는 **없음을 확인**했다(`Source/Editor` 밖에는 소유·바인딩만 있다).
**트레이드오프**: 에디터 핫 리로드 후 Undo 히스토리가 사라진다. 유지하려면 커맨드가
직렬화 가능해야 하고(모듈에 묶인 람다가 아니라), 그건 별개의 큰 작업이다. 지금 선택지는
"비우기" 와 "언맵된 코드로 뛰기" 뿐이다.

**실증에 대한 한계**: 헤드리스 실행에서는 편집이 없어 스택이 **0개**다(임시 로그로 확인).
즉 이 결함은 사용자가 실제로 편집한 뒤 리로드하거나 종료할 때만 드러난다 — 그래서 지금까지
안 잡혔다. 크래시 재현은 만들지 않았고, 근거는 코드 수준이다(언로드 순서 + `release()` 가
모듈 함수 포인터를 부른다는 점).

**그리고 조용히 실패하던 자리 셋에 피드백을 붙였다.** 백로그가 예전에 같은 종류
("에디터의 붙여넣기는 실패를 아무도 읽지 않고 있었다")를 고친 적이 있는데 남아 있었다:

- `EditorAssetCommands::loadScene` — `requestLoadAsync` 실패 시 **로그도 알림도 없이** false 만
  돌려줬고 호출부도 그 값을 읽지 않는다. 사용자가 씬을 골랐는데 아무 일도 일어나지 않는다.
- `EditorTransformCommands::loadComponentPreset` — 파일이 없거나 XML 이 그 컴포넌트 타입과
  맞지 않으면 실패하는데 반환값을 버렸다. "프리셋을 골랐는데 아무 일도 없다".
- `EditorTransformCommands::saveComponentPreset` — 같은 형태(쓰기 실패).

**이번 라운드에 훑고 결함이 없다고 확인한 것** (같은 곳을 다시 파지 않도록):

- `formatstring` 의 `%s`·`%d` 혼용 — 정상이다. 이 포매터는 `%s`/`%d`/`%f` 등을 모두
  "다음 인자" 자리표로 받는다(`%#` 이 기본형일 뿐).
- **멤버 인덱스 경계** — `_selectedFrame`·`_selectedKey`·`_selected`·`_selectedGameDataIndex`·
  `_filterIndex`·`_historyIndex`·팔레트 `_selectedIndex` 전부 접근 직전에 범위를 검사한다.
- **백그라운드 잡 수명** — `EditorBackgroundJob` 이 상태를 `shared_ptr` 로 워커에 넘기고 세대
  번호로 낡은 결과를 버린다. 패널이 먼저 사라져도 안전하다.
- **엔진에 남는 다른 콜백** — Logger 구독은 `ConsolePanel` 이 소멸자·shutdown 양쪽에서 해제하고,
  창 닫기 핸들러는 `ImGuiEditor::shutdown` 이 비운다. 에디터는 엔진 파일 감시자에 등록하지 않는다.
- `EditorConfig` 필드 16개 — 읽히지 않는 것 없음.

**검증**: Debug·Shipping 경고 0, nogpu 5/5(양쪽), 린트 6/6, 네 백엔드 실기동 종료 코드 0 ·
`[Error]` 0건 · `Game View` 정점 수 742 동일, 전부 열기 창 29개/빈 패널 0개.

### 2026-09-10 (구조를 정리하다 드러난 결함 다섯 개)

리팩터 뒤에 도구를 다시 돌리고(clang-tidy 85 TU **0건**, ASan nogpu **5/5**) "쓰기만 하고 읽지
않는 필드" 를 기계로 훑었다(멤버 442개 중 후보 4개). 나온 것을 하나씩 확인해 고쳤다.

**1. 프레임마다 씬 전체를 힙에 복사하고 있었다.** `GameObjectManager::getAllGameObjects()` 의
값 반환 오버로드는 헤더가 "매 프레임 도는 곳에는 쓰지 말라" 고 못박아 둔 것인데, 에디터가
프레임마다 네 번 그렇게 부르고 있었다 — 통계 오버레이가 **개수만 알려고** 한 번,
디버그 시각화가 한 번(내가 표로 쪼개며 두 번으로 늘렸다), `EditorCamera::find` 가 뷰포트
update/draw 에서 두 번. 지금은 뷰포트가 프레임당 스냅샷 **하나**를 재사용 버퍼에 만들어
시각화와 통계가 함께 보고, `EditorCamera::find` 는 복사조차 하지 않는 `forEachGameObject` 를
쓴다. `HierarchyPanel` 은 스냅샷이 **필요하다**(트리를 그리는 도중 오브젝트가 지워질 수 있고,
매니저를 잠근 채 그리면 같은 스레드가 배타 락을 다시 잡아 교착한다) — 그래서 순회로 바꾸지 않고
재사용 버퍼 + out 파라미터 오버로드로 옮겼다.

**2. DX12 만 채우기 모드를 무시했다.** `D3D12RHIResourcePipeline` 이 바로 다음 줄에서
`desc._cullMode` 는 읽으면서 `FillMode` 는 `D3D12_FILL_MODE_SOLID` 로 못박혀 있었다 —
`RHIFillMode::Wireframe` 을 요청한 파이프라인이 DX12 에서만 조용히 솔리드로 그려진다.
DX11·Vulkan·GL 과 같은 형태로 고쳤다. 지금 Wireframe 을 요청하는 파이프라인이 없어 동작 변화는
없고, 백엔드 간 어긋남만 없어진다.

**3. 뷰포트 뷰 모드 콤보가 아무 일도 하지 않았다.** 위 1-0 항목으로 옮겼다.

**4. `editordata.xml` 의 `_clearColor` 가 무시되고 있었다.** Game View 렌더 타깃의 클리어 색은
`EditorContextLifecycle` 에 **같은 값이 손으로 박혀** 있었다(`0.12,0.15,0.18,1` — XML 과 정확히
일치했다. 즉 XML 이 이걸 몰게 하려던 것이었다). XML 을 고쳐도 아무 일도 없었다. 이제 XML 값을
쓴다 — `_clearColor` 를 `0.9,0.1,0.7,1` 로 두고 실기동해 그 값이 렌더 타깃까지 가는 것을 임시
로그로 확인한 뒤 되돌렸다.

**5. 죽은 시드 필드 둘을 걷어냈다.** `EditorData::_defaultMaterial` 은 `EngineData::_defaultMaterial`
과 기본값까지 똑같은 복사본이고 읽는 곳이 없었다(엔진 쪽이 정본이다). `_playerSpeed` 는 Source
전체에서 **선언 한 줄만** 있는, 게임 템플릿에서 흘러온 잔재였다 — 에디터 도구 시드에 있을 값도
아니다. 구조체와 XML 양쪽에서 지웠다. (`_fontSize`·`_clearColor` 는 읽는 곳이 있어 남겼다.)

**검증**: Debug·Shipping 경고 0, nogpu 5/5(양쪽), ASan 5/5, 린트 6/6, 컨벤션 0건,
clang-tidy(Editor 85 TU) 0건, 네 백엔드 실기동 종료 코드 0 · `[Error]` 0건.
그리기가 그대로인지는 네 백엔드 모두 `Game View` 정점 수 **742** 로 같은 것을 확인했다.

**교훈으로 남길 도구**: "쓰기만 하고 읽지 않는 멤버" 훑기는 값이 있었다 — 뷰 모드 콤보와 죽은
시드 필드가 이 방법으로 나왔다. 정확한 판정은 못 하지만(대입만 쓰기로 세는 어림) 후보를 4개로
좁혀 주므로 사람이 확인할 만하다.

### 2026-09-10 (뷰포트 피킹·시각화의 컴포넌트 종류 나열 제거 — 예전 1-0)

**게임이 만든 컴포넌트는 뷰포트에서 클릭으로 집히지 않았다.** `EditorViewportClient.cpp` 안에
`considerMeshPick`·`considerSpritePick`·`considerBoxPick`·`considerScenePick` 네 함수가 엔진 타입을
손으로 나열했고, 마지막 폴백은 `getPrimarySceneComponent()` **하나만** 봤다 — 게임 컴포넌트가 주
컴포넌트가 아니면 후보에 아예 들어가지 못했다.

피킹을 `Common/Commands/EditorViewportPick` 으로 옮겼다(ImGui 없음 → **테스트가 붙는다**).
종류를 아는 제공자는 표의 한 줄이고(`_order3D`/`_order2D` 가 동거리 우선순위를 정한다 — 2D 에서
스프라이트가 메시보다 앞서는 규약을 그대로 옮겼다), 전용 제공자가 못 잡은 오브젝트는 그
오브젝트의 **모든** `SceneComponent` 를 기본 반지름 0.35 로 훑는다. RTTI 가 꺼져 있어
`dynamic_cast` 를 쓸 수 없으므로 리플렉션 `castTo` 로 판별한다.

`TestEditorViewportPick.cpp` 7케이스 — 레이-구 교차(맞음·빗나감·뒤쪽·원점이 안에 있는 경우) ·
제공자 표가 비지 않았는지 · 전용 종류 없는 컴포넌트가 집히는지 · **주 컴포넌트가 아닌
SceneComponent 가 집히는지** · 가까운 오브젝트 우선 · 비활성 제외 · 빈 씬/널 안전.
폴백을 옛 코드(주 컴포넌트만)로 되돌리면 `NonPrimarySceneComponentIsPickable` 이 실패하는 것까지
확인했다. EditorTest 44 → **51**.

**디버그 시각화도 같은 문제였다.** `drawDebugVisualizers` 가 BoxCollider2D 와 CameraComponent 를
손으로 나열하고, 각자 `ViewportToolbarSettings` 의 bool 하나 + 툴바 체크박스 하나에 짝지어 있었다 —
시각화를 하나 더하려면 세 파일 네 곳을 고쳐야 했다. `Viewport/EditorViewportVisualizer` 의 표로
모았고(라벨·툴팁·기본값·그리기 함수가 한 줄), **툴바 체크박스가 그 표에서 만들어진다.** 설정은
bool 두 개 대신 `_visualizerMask` 하나이고 기본값은 표가 정한다(뷰포트 설정은 저장되지 않으므로
표현을 바꿔도 마이그레이션이 없다).

공유 투영 헬퍼는 `Viewport/EditorViewportProjection` 으로 뺐다. 헤더는 `struct ImVec2;` 전방
선언만 두어 ImGui 를 include 하지 않는다(`EditorViewportClient.h` 의 `ImDrawList` 와 같은 방식) —
그래서 호출부를 고칠 필요가 없었다.

`EditorViewportClient.cpp` 는 1278줄 → **1026줄**(62KB → 48KB)로 줄었고, 남은 것은 카메라 조작·
기즈모·그리드·자·통계 오버레이다.

**검증**: Debug·Shipping 경고 0, nogpu 5/5(양쪽), 린트 6/6, 컨벤션 0건, 네 백엔드 실기동 종료
코드 0 · `[Error]` 0건, 덤프 창 14개·전부 열기 29개 / 빈 패널 0개.
**그리기가 그대로인지**는 스태시로 기준선을 다시 빌드해 `Game View` 창의 정점 수를 비교했다 —
고치기 전·후 모두 **742**로 같다(그리드·자·오버레이가 전부 이 창에 그려진다).
**한계**: 클릭 자체는 실기동으로만 확인할 수 있다. 어느 컴포넌트가 선택되는지는 이제 단위
테스트가 잡지만, ImGui 의 히트 판정·기즈모 우선순위는 사람이 눌러 봐야 한다.

### 2026-09-10 (테마 프리셋 표 + ClassicDark 가 저장되지 않던 버그)

프리셋을 하나 더하려면 `EditorThemeUtil.cpp` **네 곳**을 맞춰 고쳐야 했다 — `applyPreset` 의
팔레트 switch(데이터라 불가피), `loadFromConfig` 의 문자열→열거형 사다리, `saveToConfig` 의
열거형→문자열 switch, `drawThemeSettingsDialog` 의 이름 배열. 뒤 셋은 같은 사실(이름↔열거형)을
세 번 적은 것이고, 이름 배열은 `static_cast<int32>( _preset )` 을 인덱스로 써서 **열거형 순서에
묶여** 있었다 — 순서를 바꾸면 콤보가 조용히 틀린 이름을 보여 준다.

정의를 표(`getPresetRows`) 하나로 모았다: `{ 열거형, 저장 이름, 콤보 라벨, ImGui 기본색 사용 여부,
팔레트 }`. 콤보는 표 순서로 만들고 현재 항목은 **열거형 비교**로 찾으므로 인덱스 결합이 없다.
표와 열거형의 개수는 `static_assert` 로 맞춘다(`EditorThemePreset::Count` 추가).

**그 과정에서 찾은 버그: Classic Dark 선택이 저장되지 않았다.** `applyPreset( ClassicDark )` 가
`ImGui::StyleColorsDark()` 를 부르고 **그대로 return** 해서 `s_activeTheme` 이 이전 프리셋에 머물렀다.
그래서 (1) `saveToConfig()` 가 옛 이름을 써 다음 실행에 옛 테마로 돌아갔고 (2) 대화상자 콤보가
옛 프리셋을 선택된 것으로 보여 줬고 (3) `textSuccess`/`textWarning`/... 상태색 API 가 옛 테마의
색을 냈다. 이제 프리셋과 상태색을 기록하고, 지오메트리는 현재 스타일에서 되읽어 기록이 화면과
어긋나지 않게 한다 — **창 색은 여전히 `StyleColorsDark()` 그대로여서 보이는 모습은 안 바뀐다.**

**실증**: `EditorConfig.json` 의 `_themePreset` 을 `ClassicDark` 로 두고 실기동해 `loadFromConfig`
직후의 활성 프리셋을 임시 로그로 찍었다 — 고치기 전 `0`(ModernDark), 고친 뒤 `3`(ClassicDark).
확인 후 로그와 설정 파일을 되돌렸다.

**남긴 것**: Classic Dark 에서 액센트·라운딩을 편집하면 우리 팔레트가 적용되어 사실상 프리셋을
벗어난다. 예전에도 그랬고(다만 *이전* 테마의 팔레트로 튀었다) 지금은 최소한 예측 가능하다.

**검증**: Debug·Shipping 경고 0, nogpu 5/5, 린트 6/6, 네 백엔드 실기동 종료 코드 0 · `[Error]`
0건, 전부 열기 덤프 창 29개/빈 패널 0개.

### 2026-09-10 (미저장 문서 계약을 기반으로)

**InputMapEditorPanel 의 편집이 조용히 사라지고 있었다.** 이 패널은 자기 `_bDirty` 를 들고
화면에 "* Unsaved changes" 까지 띄웠지만, `IEditorPanel` 의 문서 계약
(`isDocumentDirty`/`trySaveDirtyDocument`/`discardDirtyDocument`)을 **하나도 구현하지 않았다.**
그래서:

- `Ctrl+S` 를 이 패널에 포커스를 두고 눌러도 `saveFocusedDirtyDocument()` 가 이 패널을 dirty 로
  보지 못해 false 를 돌려주고, `saveFocusedOrScene` 이 대신 **씬을** 저장했다.
- 종료·새 씬·씬 열기의 미저장 확인은 `countDirtyDocuments()` 로 세는데 이 패널이 0 으로 세어졌다.
  `saveAllDirtyDocuments()`·`discardAllDirtyDocuments()` 도 건너뛰었다 — 즉 편집이 사라졌다.
- 제목의 미저장 표시(`UnsavedDocument`)도 붙지 않았다.

원인은 계약이 **가상 함수 넷 + getPanelFlags 재정의**여서, 패널마다 자기 dirty 플래그를 들고
같은 것을 다시 구현해야 했다는 것이다. 실제로 세 패널(`EditorDocumentPanel`·`DataTablePanel`·
`GlobalVariablesPanel`)이 네 메서드와 `getPanelFlags` 의 같은 분기를 각자 복사하고 있었고, 넷째는
복사하지 않아 반쪽이 되었다.

**dirty 비트를 `IEditorPanel` 이 들게 했다.** `isDocumentDirty()` 는 더 이상 가상이 아니고,
파생은 `markDocumentDirty()` 로 알리고 `saveDocument()`/`revertDocument()` 만 구현한다.
`trySaveDirtyDocument`/`discardDirtyDocument` 는 기반이 dirty 를 보고 그 둘을 부른다. 미저장
표시는 `draw()` 가 dirty 를 보고 스스로 더하므로, 다른 플래그 때문에 `getPanelFlags()` 를
재정의한 패널도 표시를 잃지 않는다. 이제 **반쪽 구현이 불가능하다** — 패널이 dirty 를 알리는
유일한 방법이 기반 비트이기 때문이다.

계약은 ImGui 없이 컴파일되므로(두 함수를 헤더 인라인으로 내렸다) 가짜 패널로 테스트가 붙는다 —
`TestEditorPanelDocument.cpp` 5케이스: 알림이 잡히는지 · dirty 일 때만 저장하는지 ·
**저장 실패 시 dirty 가 남는지**(종료를 멈추는 근거다) · 버리기가 되돌리기를 부르는지 ·
문서 없는 패널이 조용히 지나가는지. `EditorTest` 40 → **44**
(쓰이지 않게 된 `EditorSessionPolicy::isToolSessionDirty` 와 그 케이스 1개를 걷어냈다).

감사로 확인한 것: 이제 문서 계약을 재정의하는 자리는 `saveDocument`/`revertDocument` 뿐이고,
패널에 남은 `*Dirty` 비트는 전부 **캐시 무효화** 플래그다(ContentBrowser 의 루트·폴더 목록,
GlobalVariables 의 프리셋 목록, Profiler 의 카탈로그, Inspector 의 프리셋 목록) — 문서 dirty 가
아니다. `DataTablePanel` 의 `_bLocDirty`/`_bGameDataDirty` 만 남았고, 이는 문서가 둘이라 어느
쪽을 저장할지 알아야 하기 때문이며 `syncDocumentDirty()` 한 곳에서 기반 비트와 맞춘다.

**검증**: Debug·Shipping 경고 0, nogpu 5/5(양쪽), 린트 6/6, 네 백엔드 실기동 종료 코드 0 ·
`[Error]` 0건, 전부 열기 덤프 창 29개/빈 패널 0개.
**한계**: 계약 자체는 테스트가 잡지만, "어떤 패널이 계약을 쓰는가" 는 테스트가 잡지 못한다.
새 패널이 또 자기 플래그를 만들면 위 감사 grep 으로만 보인다.

### 2026-09-10 (에디터 커맨드 SSOT)

**에디터 커맨드 하나가 세 곳에 따로 적혀 있었다.** `EditorMenuBar` 의 메뉴 항목(라벨·아이콘·
단축키 **문자열**·툴팁·활성 조건·동작), 같은 파일의 `processHotkeys` 키 사다리(조합 → 동작),
`CommandPalettePopup` 의 정적 목록(분류·라벨·설명·동작). 세 곳이 서로를 모르니 실제로 어긋났다:

- **`Ctrl+Z` 가 두 번 되돌렸다.** 전역 `processHotkeys` 와 `InspectorPanel::drawContent` 가 각각
  `undo()` 를 불렀고, ImGui 의 `IsKeyPressed` 는 소비되지 않으므로 같은 프레임에 두 호출자가
  모두 true 를 본다. Inspector 가 포커스면 두 칸 되돌아갔다. 게다가 Inspector 경로에는 플레이
  중 가드도, `WantTextInput` 가드도 없어서 값을 타이핑하다 Ctrl+Z 를 누르면 씬 편집이 되돌아갔다.
- **`F7`(게임 컴파일)은 어느 라벨에도 없었다.** 키 사다리에만 있어서 아무도 모른다.
- **`Ctrl+Shift+Z`(다시 실행)는 Inspector 가 포커스일 때만 먹었다.** 메뉴는 `Ctrl+Y` 만 알렸다.
  게다가 전역 `Ctrl+Z` 는 Shift 가 눌렸는지 확인하지 않아서 `Ctrl+Shift+Z` 에 undo 까지 함께 발동했다.
- **팔레트의 "Save Scene" 은 `saveFocusedOrScene()` 을 불렀다** — 메뉴의 "Save" 와 같은 동작이고
  메뉴의 "Save Scene"(`saveActiveSceneOrPrompt`)이 아니다. 라벨이 거짓이었다.
- **정렬/분배 7개는 세 경로였다** — 뷰포트 툴바는 `EditorWorkspace` 전달자를, 팔레트는
  `EditorTransformCommands` 를 직접 불렀고 라벨도 달랐다("Align X" vs "Align X (Center)").

정의를 `Common/Gui/EditorCommandGui.cpp` 의 표 **하나**로 모았다(커맨드 27개). 세 표면은 그것을
읽기만 한다 — 메뉴는 `drawMenuItem( "<id>" )` 한 줄, 단축키는 표를 훑는 루프 하나, 팔레트는
열릴 때 레지스트리를 읽는다. 커맨드를 하나 더하면 세 곳에 동시에 나타난다. 단축키 라벨과 툴팁의
`(Ctrl+S)` 도 표의 조합에서 만들어 붙으므로 라벨이 실제 처리와 어긋날 수 없다.

모델(`Common/Commands/EditorCommandRegistry`)은 **ImGui 없이** 컴파일되므로 테스트가 붙는다
(`EditorTest` 34 → 40). 그중 하나가 **중복 조합 검사**다 — 두 커맨드가 같은 키 조합을 주장하면
`validate()` 가 잡고, 에디터 시작 시 `[Error]` 로 남으므로 실기동 검증(0절)이 게이트가 된다.
위의 Ctrl+Z 중복이 다시 들어올 수 없다는 뜻이다.

단축키 조합 비교를 **정확 비교로 바꿨다**(예전엔 필요한 수정자만 확인했다). Alt+F4 처럼 OS 가
가로채는 것은 `kDisplayOnly` 비트로 라벨에만 남긴다 — 처리하는 척하지 않는다.

**곁들여 없앤 것**: `EditorWorkspace` 의 정렬·분배·바닥 스냅 전달자 3개(유일한 호출자가
뷰포트 툴바였다), `CommandPalettePopup::registerCommand`/`registerCommandInstance`(호출자 0),
그리고 그 파일의 정적 커맨드 목록. 팔레트는 이제 커맨드를 **가지지 않고 읽기만** 한다.

**검증**: Debug/Shipping 빌드 경고 0, nogpu 5/5(Debug·Shipping), 린트 6/6, 컨벤션 0건,
네 백엔드 실기동(`-dx12/-dx11/-vk/-gl`) 종료 코드 0 · `[Error]` 0건,
패널 덤프 기본 **창 14개/빈 패널 0개** · 전부 열기 **창 29개/빈 패널 0개**(기준선과 같다).
런타임에 커맨드 27개가 실제로 등록되는 것도 임시 로그로 확인한 뒤 로그를 걷었다.

**남은 구멍 하나**: 메뉴가 부르는 id 가 표에 없으면 그 항목은 조용히 사라진다(경고만 남고,
메뉴를 열지 않는 헤드리스 실행에서는 그 경고조차 나오지 않는다). 메뉴를 손댔으면
`Source/Editor/README.md` 의 "커맨드를 하나 더하려면" 절에 있는 `comm` 한 줄로 대조할 것.
린트로 승격하는 것은 스크립트 등록(GeneratedConstants·AssetAndToolTargets·PreCommitLint)까지
건드려야 해서 이번에는 하지 않았다.

### 2026-09-10 (재검증)

**억제한 것들이 정말 안전한지 하나씩 실증했다.** 억제는 판단이고, 판단은 근거가 남아야 다음 사람이
다시 세우지 않는다.

- **ASan `report_globals=0`(SmokeTest 전용)** — 네 가지 결함을 일부러 내는 12줄 프로브를 ASan 으로
  빌드해 같은 옵션으로 돌렸다: heap-buffer-overflow · heap-use-after-free · stack-buffer-overflow 는
  **그대로 잡히고**, global-buffer-overflow 만 안 잡힌다(끈 검사가 그것 하나라는 뜻이다). 기본
  옵션으로 돌리면 그 global 도 잡히는 것까지 확인해 프로브 자체가 유효함을 보였다. 다른 네 테스트는
  기본값이라 global 검사도 그대로다.
- **`.clang-tidy` 에서 끈 검사 셋** — 전수로 확인했다. `invalid-enum-default-initialization` 48건은
  열거형 종류가 19가지인데 **전부 D3D11/D3D12/Vulkan SDK 타입**이고 `sw::` 열거형은 0건이다.
  `derived-method-shadowing-base-method` 11건은 **전부 `swReflectSelf`** 다.
  `std-namespace-modification` 10건은 전부 `tuple_size`/`tuple_element`/`hash`/`equal_to` 를
  프로그램 정의 타입에 대해 특수화한 것으로 `[namespace.std]` 가 허용하는 형태다 — 다만 그중
  `tuple_size<FormattedValue<T>>` 이 `integral_constant<uint32,2>` 였다. 표준은 `size_t` 를 요구하므로
  (구조적 바인딩이 우연히 동작했을 뿐) `size_t` 로 맞췄다.
- **NOLINT 두 곳** — `BVHTree3D` 의 근거로 든 `static_assert( sizeof(float4x4) == 16 * sizeof(float32) )`
  가 `MatrixMath.h:471` 에 실제로 있다. `fixed_string` 의 자기대입은 주장만 있고 시험이 없었으므로
  두 경로(`fs = fs`, `fs = fs.c_str()`)를 테스트로 고정했다.
- **패널 덤프의 "정수 id 자식 창" 규칙** — 실제 덤프에서 이 규칙에 걸리는 창은 `Sequencer/00000379`
  하나뿐이다. 우리 자식 창은 전부 문자열 id 라 가려지지 않는다.

**그리고 이번 세션의 변경 하나가 동작을 바꿨다는 것을 찾아 고쳤다.** `Component` 생성자에서
기본값 적용을 걷어내면서, gamedata 의 **기반 타입 노드**(`<SceneComponent>` 같은)가 적용될 자리가
사라졌다. 예전 코드도 온전하지는 않았다 — 기반 생성자에서는 가상 `getTypeInfo()` 가 파생으로
디스패치되지 않아 언제나 `Component` 노드 하나만 봤고, 중간 기반은 한 번도 적용된 적이 없다.
이제 `applyTypeDefaults` 가 상속 체인을 **뿌리 → 파생** 순서로 전부 적용한다(파생이 마지막에
덮어쓴다). `EngineTest.ComponentDefaults.BaseTypeDefaultsApplyBeforeDerived` 로 고정했고, 고치기
전 코드로 되돌리면 기반 값 세 개가 실패하는 것까지 확인했다.
(곁들여 배운 것: 벡터 기본값의 텍스트 형식은 **쉼표 구분**이다 — `"2,3,4"`. 공백으로 적으면
파싱이 조용히 실패하고 값이 그대로 남는다.)

### 2026-09-10

**`fixed_string` 이 용량을 넘는 입력에서 버퍼 밖을 썼다.** 생성자·대입·`insert`·`append`·
`push_back` 이 전부 같은 모양이었다 — 길이가 N 을 넘는지 `SW_LOG_ASSERT` 로 **알리기만 하고 원래
길이 그대로 복사**했다. 단정은 실행을 멈추지 않는다(Debug 는 브레이크, 그 밖은 로그만). 에디터가
이 함정을 16곳에서 밟고 있었다(`fixed_string<128> buf{ text.c_str() }` 패턴). 모든 쓰기 경로를
`clampToCapacity`/`clampToRemaining` 두 헬퍼로 모으고, 보고는 **단정에서 경고로** 내렸다 — 넘치는
길이는 데이터에서 오지(긴 대사·긴 경로) 프로그래밍 계약 위반이 아니다.
`Core_String.FixedStringTruncatesInsteadOfOverflowing` 으로 고정했다(문자열 뒤에 감시값을 두고
잘림과 이웃 보존을 함께 본다). 옛 코드로 되돌리면 첫 케이스에서 프로세스가 죽는 것까지 확인했다.

**에디터 텍스트 입력을 `EditorWidgets::drawTextField` 하나로.** 8개 패널이 임시 버퍼를 만들어
넣었다 빼는 다섯 줄을 각자 적고 있었고, 버퍼 크기(64·128·256·512)를 자리마다 골랐다. ImGui 의
리사이즈 콜백으로 `string` 자체를 버퍼로 쓰므로 **길이 상한이 없다.** 16곳을 옮겼고 손으로 만든
임시 버퍼 InputText 는 남아 있지 않다(타입이 다른 `hashed_string` 3곳 제외).

**`-gv_editorOpenAllPanels=1`** — 0절 참고. 이걸로 바로 드러난 것: DataTable 패널이 열릴 때마다
빈 경로로 파일을 읽어 `[Error]` 3건(활성 게임에 `data/localization` 도메인이 없다), 그리고
`Config/Editor/DialogueGraphEditor.json` 이 `.gitignore` 에 빠져 있던 것.

**`bake.stamp` 해시가 PC 마다 달랐다.** 셰이더 소스를 바이트 그대로 해싱하는데 `.gitattributes`
가 없어 줄 끝이 체크아웃마다 다를 수 있다 — 실제로 `instancesort.hlsl` 하나만 LF 였다. 두 PC 가
서로의 스탬프를 번갈아 덮어쓰고 있었다(Shipping 을 빌드할 때마다 작업 트리가 더러워진 원인).
CR 을 뺀 바이트로 해싱하고(굽는 쪽·확인하는 쪽 같은 정규화) 형식을 `SWBAKE 2` 로 올렸다.

**ASan: 모듈을 해제한 뒤 적재하면 실패하던 문제 — 원인 규명, SmokeTest 복귀.** 예전에는 스위트째
Disabled 였고 원인을 "못 찾았다" 고 적어 두었다. `detect_odr_violation=0` 덕분에 ODR 보고가
앞을 가리지 않게 되자 진짜 보고가 나왔다: `RHI_DX12.dll` 의 정적 초기화가 `SW_LOG_CALLER` 의
`__FILE__` 를 읽는 자리에서 global-buffer-overflow. 이유는 **이 ASan 런타임이 DLL 을 내려도 그
모듈의 전역 등록을 지우지 않는 것**이다 — `report_globals=2` 로 세어 보면 스위트 한 번에
"Added Global" 5241건, 제거 **0건**이다. 그래서 다음 모듈이 그 주소 범위에 매핑되면 자기 전역을
읽는데도 앞 모듈이 남긴 레드존을 밟는다. 보고하려고 주소를 설명하는 순간 이미 언매핑된 모듈의
디스크립터를 역참조해 "nested bug in the same thread, aborting" 으로 죽는다 — 예전에 보이던
`LoadLibrary err=1114` 와 맨 세그폴트가 이것이다. 이 스위트는 존재 이유가 모듈 적재·해제라
우회할 수 없으므로 **SmokeTest 에서만** `report_globals=0` 을 주고 스위트를 되살렸다(힙·스택·
use-after-free 는 그대로 잡힌다). ASan nogpu 5/5, 30초.

**Shipping 테스트가 도구가 없어서 실패하고 있었다.** `ShaderBindingContractTest.
ReflectionNamesAreUniformAcrossBackends` 가 "forwardlit_ps g_SwMaterials 원소 없음: dx12" 로
떨어졌다. 셰이더가 아니라 `dxcompiler.dll` 이 없어서 DXIL 리플렉션을 못 얻은 것이었다 —
`sw_copyDxcDlls(EngineTest)` 가 `NOT SW_SHIPPING_BUILD` 로 막혀 있었다. 테스트 바이너리는 `Bin`
이 아니라 `TestBin` 으로 나가므로 배포물에 섞이지 않는다. 가드를 걷었다.

**정적 분석·기타** — 위 1-2 참고. `Component` 의 기반 생성자 가상 호출(파생 기본값이 한 번도
적용되지 않던 것), ContentBrowser 의 `system()`, `Logger::registerCaller` noexcept, 그리고
`SW_ACTIVE_GAME` 이 낡은 캐시를 가리킬 때의 오류 메시지(가능한 게임 목록과 고치는 법을 같이
알려준다 — Ninja-Debug-ASAN 이 없어진 'Demo' 를 들고 있어서 configure 조차 못 했다).

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

**전수 조사 3회차 — 남은 지적을 0 으로 (65 → 0건)**

남은 65건을 하나씩 판단했다. 고칠 수 있으면 고치고, 구조적으로 불가능한 자리는 **이유를 적어**
NOLINT 했다. 이유 없는 억제는 남기지 않았다.

**찾은 실제 결함**

- **`Component` 생성자가 가상 함수를 불러 기본값이 틀린 타입으로 적용됐다.** 생성자가
  `initialize()` → 가상 `getTypeInfo()` 를 부르는데, 생성 중에는 객체가 아직 `Component` 라
  **파생 타입이 아니라 기반 타입**의 TypeInfo 가 나온다. 즉 `MeshComponent` 를 만들어도
  "Component" 이름으로 기본값을 찾았다. 실제 생성 경로는 타입을 아는 쪽이 이미 올바르게 넘겨
  준다 — `GameObject::addComponent<T>` 와 `GameObjectManager` 의 이름 기반 생성이 둘 다
  `applyTypeDefaults( 파생 TypeInfo )` 를 부른다. 즉 생성자 호출은 **중복이면서 틀린 조회**였고,
  컴포넌트를 만들 때마다 헛일을 했다. 호출과 (호출자가 없어진) `initialize()` 를 걷어냈다.
  안전한지 확인한 근거: `initialize()` 는 `private` 이고 호출자가 생성자뿐이며, 기본값 데이터는
  게임이 `setPath` 를 부르지 않으면 아예 로드되지 않는다(`Resource/` 에 `<Defaults>` 노드도 없다).
- **`FrameRendererPassExecute` 의 SSAO PSO 폴백이 자기 자신이었다.**
  `getEnginePso(SSAO) != 0 ? getEnginePso(SSAO) : getEnginePso(SSAO)` — 참·거짓이 같아 아무 효과가
  없고 함수만 세 번 불렀다. 형제 패스는 전부 **다른** PSO 로 폴백한다(DepthPrepass→Shadow,
  GBufferAlbedo→GBuffer, Tonemap→Present). 복사하면서 대체 대상을 바꾸지 않은 자리다. 폴백을
  짐작해 넣지 않고, PSO 가 0 이면 `drawFullscreen` 이 건너뛰므로 형제들처럼 그대로 넘기게 했다.
- **`StringBuilder::appendFormat` 이 재시도 루프에서 매번 `std::forward` 했다.** 버퍼가 모자라면
  같은 인자로 다시 포맷하는데, 그때는 이미 이동된 값을 쓰게 된다. `formatstring` 은 값을 읽어
  찍기만 하므로 lvalue 로 넘겨 위험 자체를 없앴다.
- **컴포넌트 붙여넣기가 실패를 삼켰다.** `pasteComponentAsNew` 가 역직렬화 결과를 계산해 놓고
  **아무도 읽지 않았다.** 둘 다 실패해도 빈 컴포넌트를 붙이고 "붙여넣기" 실행 취소 항목까지
  남겨서, 쓰는 사람은 왜 비었는지 알 수 없었다. 경고 로그를 남긴다.
- **`Material` 의 죽은 코드.** 첫 순회가 `packSize` 를 계산했지만 뒤따르는 `if` 는 본문이 주석뿐인
  빈 블록이었고 값은 아무도 읽지 않았다. 실제 패킹은 두 번째 순회가 다시 계산해서 한다.
- **콘텐츠 브라우저 필터 라벨이 `string_view` 였다.** 쓰는 쪽 셋이 모두 곧바로 `.data()` 를 ImGui 로
  넘기는데 ImGui 는 널 종단을 요구한다. 지금 표가 전부 리터럴이라 우연히 맞을 뿐이라, 타입을
  `const utf8*` 로 바꿔 계약을 적었다.
- 소멸자·생성자에서의 가상 호출 12곳을 클래스 이름으로 한정했다. 파괴 중에는 파생 재정의가 이미
  사라진 뒤라 이 클래스의 것이 불린다 — 지금 동작이 의도한 것이므로 코드로 적었다.
- `.bin`/`.xml` 은 -4, `.json` 은 -5 처럼 확장자 길이를 손으로 쓰던 자리를 표로 돌게 했다.
  `Json`/`Xml` 직렬화의 같은 본문 두 분기는 조건으로 합쳤다. `ShaderBaker` 의 SPIR-V 두 케이스는
  묶어서 "같아야 한다" 를 드러냈다.

**오탐이라 이유만 남긴 것** — 다시 판단하지 말 것:
`Base{ std::move(other) }` 뒤의 파생 멤버 읽기(기반 부분객체만 이동한다), 타입 이름·`##` 인자를
받는 매크로(괄호를 씌우면 문법이 깨진다), 순서가 규약인 분기(vector 재할당의 이동/복사 우선순위,
Windows 전용 DX11·DX12, "여기서 안 하고 아래서 한다" 는 케이스 묶음), 크기를 함께 넘기는
`append(data(), count)`(커스텀 string 의 오버로드를 인식하지 못한다), `float3`/`float4x4` 를
연속 float 로 훑는 자리(배치는 옆의 `static_assert` 가 보장한다), 뮤텍스 락이 던질 수 있어
noexcept 와 어긋난다는 지적(잠그지 못하는 상황은 복구 대상이 아니다).

**끈 검사 하나 추가** — `clang-analyzer-optin.performance.Padding`. 걸린 셋이 전부 인스턴스가
하나뿐인 매니저이거나 20행짜리 정적 표라 아끼는 양이 무의미한데, 이 저장소는 "생성자 초기화는
선언 순서" 규약이라 멤버를 옮기면 초기화 목록도 같이 옮겨야 한다. 대량 배열로 쓰이는 뜨거운
구조체가 생기면 그때 개별로 재는 편이 낫다.

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
