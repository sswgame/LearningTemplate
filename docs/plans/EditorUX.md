# 에디터 UX 기반

> 상태: 참고 · 선행: 없음 · 영역: editor · 진행: 해당없음

에디터의 색 · 간격 · 모서리는 **테마 토큰 파일**에서만 나오고, 화면은 **캡처**로 확인하고, 캡처는 **체크리스트**로 점검한다. Figma 는 이 흐름에 값을 넣어 주는 선택 입력이다. 이유: 색을 코드에 숫자로 박으면 테마를 바꿔도 그 색만 남아 글자가 안 보이고, UI 결함은 로그가 초록이어도 화면에서만 보이므로 캡처 판정 기준이 글로 있어야 하며, Figma 값을 손으로 옮기면 틀리므로 검사가 붙은 스크립트가 맡는다. 10절(우클릭과 입력 · 명령 계층)은 코드가 들어간 설계로, 코드 주석이 이 문서의 10-2 ~ 10-5 번호를 가리킨다.

## 1. 구성

```
Figma 변수(선택) ⇄ Resource/editor/theme/<이름>.tokens.json(원본) → 검사(키 집합 · 색 형식 · 수 범위 · 대비비)
토큰 → 에디터 스타일(EditorThemeUtil 접근자) → 패널 그리기 → UI 캡처 PNG → 체크리스트 점검 → 결함 표 → 패널 수정
숫자 리터럴 색 린트가 패널 그리기 코드의 직접 색 지정을 막는다
```

- **토큰 파일이 원본이다.** `Resource/editor/theme/<이름>.tokens.json` 다섯 개(dark, charcoal, midnight, light, highcontrast)에 색 · 모서리 · 간격 · 선 두께 · 글자 크기 · 밀도 배율이 있다.
- **Figma 는 선택이다.** 로그인하지 않아도 토큰 수정 · 검사 · 캡처 · 점검이 동작한다.
- **코드는 토큰만 읽는다.** `EditorThemeUtil` 접근자로 값을 얻고, 숫자 리터럴로 색을 만들면 린트가 막는다(6-1).
- **점검은 읽기 전용**이고 결함 표만 낸다.

## 2. 토큰 도구

저장소 루트에서 `py -3 -m Scripts editor-theme ...`(구현 `Scripts/dev/MakeEditorTheme.py`).

- **검사.** 토큰 파일을 고친 뒤 항상 `check Resource/editor/theme/*.tokens.json`. 키 집합이 다크 세트와 다르거나, 색이 대문자 `#RRGGBB` · `#RRGGBBAA` 가 아니거나, 수가 범위를 벗어나면 오류(종료 1). 대비비 미달은 경고(`--fail-on-warning` 으로 실패 처리).
- **Figma 에서 가져오기.** `import-figma <파일> --set <세트> [--mode <모드>] --name <이름>`. Figma REST 응답(`GET /v1/files/:key/variables/local`)이면 `--set` 이 컬렉션, `--mode` 가 모드 이름. DTCG JSON 이면 최상위 키가 세트다.
  - 변수 이름은 `color/accent` 나 `color.accent` 형식이고 `_color._accent` 로 대응한다(`radius/window`, `spacing/windowPaddingX`, `stroke/frame`, `typography/fontSize`, `density` 도 같다, 대소문자 무시). 별칭은 따라간다.
  - 이름 표에 없거나 값 형식이 틀린 변수는 경고로 출력하고 버린다. 출력 파일이 있으면 입력에 없는 키는 기존 값을 유지하고, 없으면 다크 세트를 바탕으로 만들며 다크에서 채운 키를 경고한다. 결과의 키 집합이 다크와 다르면 파일을 쓰지 않고 종료 1.
- **Figma 로 내보내기.** `export-figma <토큰 파일들> --out Saved/figma.dtcg.json`. 테마 이름을 최상위 키로 둔 DTCG JSON(색은 `$type: color`, 나머지 `number`)을 쓴다. 다시 `import-figma` 로 읽으면 바이트까지 같다(CRLF, 들여쓰기 4칸, 대문자 16진).

## 3. UX 체크리스트

사람 리뷰어가 쓰는 기준이다. 판정은 캡처 1장 기준이고 숫자는 실제 픽셀(밀도 1.0, 글자 16px)이다.

| 항목 | 결함으로 보는 기준 |
|---|---|
| 정렬 | 같은 열의 라벨과 입력 위젯의 왼쪽 경계가 1px 넘게 어긋남. 같은 줄 위젯의 세로 중심이 1px 넘게 어긋남. 라벨 열 폭이 같은 패널 안에서 다름 |
| 간격 | 인접 위젯 사이 간격이 토큰 값(`itemSpacing` · `framePadding` · `windowPadding`, 밀도 배율 적용)의 정수배에서 1px 넘게 벗어남. 한 패널 안에서 같은 관계의 간격이 두 가지 이상 |
| 대비 | 본문 글자와 배경 4.5:1 미만. 보조 글자(`textMuted`)도 4.5:1. 큰 글자(18px 이상) · 아이콘 · 포커스 링은 3:1 미만. 고대비 테마는 글자 7:1 미만 |
| 잘림과 클리핑 | 글자나 아이콘이 컨테이너 밖으로 나가 잘림. 말줄임 표시나 툴팁 없이 글자가 사라짐 |
| 겹침 | 서로 다른 위젯의 사각형이 1px 이상 겹침 |
| 일관성 | 같은 역할의 버튼 높이가 다름(기준은 글자 크기 + 2 × `framePaddingY`). 같은 목적의 아이콘 크기가 다름. 같은 동작을 패널마다 다른 용어로 부름 |
| 키보드 접근성 | 포커스 표시(3:1 이상, 2px 이상)가 없음. Tab 순서가 화면 순서와 다름. 단축키 메뉴 항목에 표기가 없음. 클릭 대상이 24×24px 미만 |
| 빈 상태 | 비었을 때 이유와 다음 할 일이 없음 |
| 오류 상태 | 색으로만 구분함. 메시지에 원인이나 해결 방법이 없음. 오류 색이 `error` 토큰이 아님 |
| 하드코딩 색 | 캡처의 색이 어느 테마 팔레트에도 없고 코드의 숫자 색이 출처임 |

우선순위: P0(쓸 수 없음), P1(읽기 어렵거나 오해를 부름), P2(일관성과 정돈), P3(다듬기). 근거: 4.5:1 · 3:1 · 7:1 은 WCAG 2.x AA · AAA(`check` 가 같은 상대 휘도 식), 24×24px 는 WCAG 2.2 의 2.5.8, 1px 허용은 안티앨리어싱과 정수 좌표 반올림 때문이다.

## 4. Figma 연동

인증은 사용자만 할 수 있다. `.mcp.json` 에 `figma` 서버(원격 `https://mcp.figma.com/mcp`, HTTP)가 이미 있고 비밀 값은 없다. Claude Code 를 새로 시작해 `/mcp` 에서 `figma` 를 고르고 OAuth 로 인증한다. 데스크톱 Dev Mode MCP 는 조직 요건이 있을 때의 대안이라 저장소에 넣지 않았다. 인증하지 않으면 `figma` 서버가 연결되지 않을 뿐 토큰 도구 · 린트 · 캡처에는 영향이 없다. Figma 값이 필요하면 변수 JSON 을 내려받아 `import-figma` 에 준다.

- 읽기 도구(문서에서 확인): `get_design_context`, `get_variable_defs`, `get_screenshot`, `get_metadata`, `search_design_system`, `get_libraries`. 이 문서의 절차는 앞의 둘을 쓴다.
- **추정(직접 테스트하지 못함):** REST 응답 모양(`meta.variables`, `valuesByMode`, `VARIABLE_ALIAS`, 색 `{r,g,b,a}` 0 ~ 1)은 REST 문서 지식에 따랐다. `export-figma` 의 DTCG 를 Figma 에 가져오는 길은 DTCG 를 읽는 플러그인(Tokens Studio 계열)을 거칠 것으로 추정한다(안 되면 형식을 플러그인에 맞춘다). 변수 REST 가 모든 요금제에서 열려 있는지와 원격 서버가 링크의 노드 ID 로 프레임을 고르는지는 첫 사용 때 확인한다(막혀 있으면 `get_variable_defs` 결과를 DTCG 모양으로 옮겨 `import-figma`).

## 5. 에이전트 절차

디자인 프레임을 보고 패널을 고칠 때의 순서다. 1과 2는 Figma 인증이 있을 때만 한다.

1. **프레임을 읽는다.** `get_design_context` · `get_variable_defs` · `get_screenshot`.
2. **토큰으로 옮긴다.** 변수가 토큰 이름과 맞으면 `import-figma`. 경고로 나온 모르는 변수는 토큰을 새로 더할지 사람에게 묻고 임의로 키를 만들지 않는다.
3. **패널을 고친다.** 색 · 간격 · 모서리는 `EditorThemeUtil` 접근자로만 읽는다. 값이 토큰에 없으면 토큰 파일 다섯 개와 접근자에 같은 커밋에서 키를 더한다.
4. **캡처로 확인한다.** `App -EnableEditor -gv_screenshotEditor=<경로>.png`(`-gv_screenshotEditorFrame=N`, 기본 60). 자동 시나리오는 `<CaptureEditorUI file="..." maxMagentaFraction="0" maxClippedWindows="N"/>`.
5. **점검을 시킨다.** 캡처 경로와 패널 이름으로 점검한다. 보고는 `우선순위 | 패널 | 캡처 파일 | 결함 | 근거(좌표/영역) | 제안` 표이고 P0 · P1 은 고치고 4로 돌아간다.
6. **마무리한다.** 토큰 검사와 숫자 리터럴 색 린트를 통과시킨다.

## 6. 에디터 UI 캡처의 원리

게임 화면 캡처(`-gv_screenshot` · `<Screenshot>`)는 주 출력만 담고 ImGui 는 담지 못한다. 창을 전경으로 올려 화면을 복사하는 방식은 사용자 화면을 훔치고 다른 창이 섞이므로 쓰지 않는다. 대신 렌더 스레드가 그 프레임의 **최종 ImGui 그리기 목록**을 오프스크린 타깃에 한 번 더 그려 읽는다(`EditorUICapture`).

- **플로팅 창까지 담는다.** 멀티 뷰포트의 정점이 OS 화면 좌표라 메인과 플로팅 뷰포트의 목록을 한 그리기 데이터로 이어 붙이고 표시 영역을 모두를 감싸는 사각형으로 둔다(창 사이 바탕은 검정).
- **네 백엔드가 같다.** 같은 ImGui 렌더러 백엔드 · 백버퍼 포맷이고 OpenGL 은 읽은 뒤 뒤집는다. 창 위치 · 가림 · 포커스와 무관하다.
- **지표 둘.** `maxMagentaFraction`(마젠타 비율 상한 — 빠진 텍스처), `maxClippedWindows`(내용이 창보다 큰데 그 방향 스크롤바가 없는 창의 상한, 이름을 로그에 남김).
- **골든(그림 비교)은 하지 않는다.** 글꼴 래스터와 DPI 가 기계마다 다르기 때문이다. 같은 기계 전후 비교는 PNG 를 사람이 본다.

### 6-1. 린트: 숫자 리터럴 색 차단

`Source/Editor/**` 와 에디터 플러그인의 `.cpp` · `.h` 에서 `IM_COL32(`, 첫 인자가 숫자인 `ImVec4(` · `Color{` · `Color(` · `Color::fromBytes(`, 값에 숫자 리터럴이 든 `PushStyleColor(` · `PushStyleVar(` 를 막는다(주석 · 문자열 제외, 테마 구현 파일 제외). 커밋 훅은 추가한 줄만 검사하고, 전체 훑기는 건수가 기준선보다 늘면 실패한다. 위반을 토큰으로 옮기면 기준선도 낮춘다. 형식으로 못 막는 이유: 값이 숫자 리터럴인지는 호출 형식이 아니라 인자 내용이라 컴파일러가 구분하지 못한다.

## 7. 함정과 주의

- **새 테마를 가져올 때 입력에 없는 키는 다크 값으로 채워진다**(경고 한 줄뿐). 라이트에 다크 색이 섞이면 `check` 의 대비 경고로 드러난다.
- **Figma 의 알파는 `#RRGGBBAA` 로 들어온다.** `tableRowBgAlt` 같은 오버레이는 알파가 있어야 의미가 있으니 알파를 버리는 도구를 거치지 않는다.
- **대비비 검사는 불투명 색 기준이고 세 쌍만 본다.** `text` 대 `windowBg` · `panelBg` · `frameBg` 와 `textMuted` 대 `windowBg`. 알파 배경 위 글자, 강조색 · `headerBg` 위 글자는 캡처로 본다.
- **캡처는 해상도와 DPI 를 적는다.** 조건이 없으면 점검 판정을 믿지 않는다.
- **체크리스트 점검은 눈으로 보는 점검이다.** 1px 판정은 축소 이미지에서 틀리므로 원본 크기 PNG 를 주고 판정 불가 항목은 판정 불가로 적게 한다.
- **토큰 키를 더하면 다섯 곳이 같이 바뀐다.** 토큰 파일 다섯 개, C++ 로더 구조체, `MakeEditorTheme.py` 의 `kSchema`, 접근자(하나만 바꾸면 `check` 가 키 집합 오류로 알린다).

## 8. 감사 결과 — 남은 결함

첫 감사(2026-10-15)는 패널 39 개 + 기본 배치 테마 셋 × 백엔드 넷, 52 장을 점검했다. 마젠타는 모든 장 0, 네 백엔드 기본 배치는 FPS · RHI 이름 · 로그 줄 수 말고 같다. 지역화 키 노출 · 빈 상태 줄바꿈 · Prefab Editor 버튼 폭 · Output Log 오류 칩 색 · 라이트 `_headerBg` 는 그때 고쳤다. 가로로 넘쳐 스크롤바 없이 잘리는 창(Hierarchy · Inspector · Output Log · Profiler · UI Preview 등)이 많다.

남은 큰 일: `maxClippedWindows` 가 메뉴 바 · 플레이 툴바의 작은 넘침(세로 +24, 가로 +8)을 아직 세므로 넘친 양의 문턱을 정해 단언에 쓸 수 있게 한다. 라이트 · 고대비 대비는 `check` 의 대비 쌍을 늘려 잡는다.

| 우선순위 | 패널 | 결함 |
|---|---|---|
| P0 고침 | Output Log · Content Browser(아래 도크) | 아래 도크 높이를 비율이 아니라 글자 높이의 16 배(`kBottomDockTextLines`)로 정함. 8 줄 이상 보임을 시나리오가 지킴(통합에서 측정 확인 필요) |
| P0 고침 | Scene(뷰포트) | HUD 상자 기본값을 껐다(`_bShowHUD`). 켜는 것은 툴바 HUD 단추 |
| P1 | 도킹 탭 줄 · Profiler 하위 탭 | 탭이 많으면 제목이 말줄임으로 뭉개진다. Profiler 표 헤더의 단위가 사라진다 |
| P1 | Hierarchy | 이름이 말줄임 · 툴팁 · 가로 스크롤 없이 잘린다 |
| P1 | Keyboard Shortcuts · Global Variables · Data Table · Project Settings · Modules | 열 폭 · 콤보 폭이 좁아 말줄임 없이 잘린다. Global Variables 는 Reset 열이 창 밖 |
| P1 | Preferences · User Settings | 필드 이름이 내부 변수명이고 `_uiScale = 0.00` 의 뜻 설명이 없다 |
| P1 | Input Map Editor | 경로 상자가 잘리고 라벨과 상자의 세로 중심이 어긋난다 |
| P1 | Audio Mixer | 이벤트 콤보가 잘리고, 채널 슬라이더 높이가 채널마다 다르며, 재생 전 `-inf dB` 에 설명이 없다 |
| P1 | 라이트 테마 | 프리팹 인스턴스 이름(연한 파랑)과 로그의 파란 태그가 흰 바탕에서 대비가 낮다 |
| P1 | 고대비 테마 | 꺼진 토글과 `Error` 칩의 대비가 낮다 |
| P2 | Scene 툴바 | `Rot Snap 15 (` 콤보 값이 잘리고 `5.0` 슬라이더 값이 막대에 가린다. 아래 아이콘 툴바는 켜짐 · 꺼짐 구분이 약하다 |
| P2 | 용어 | 되돌리기가 `Reset` · `Revert` · `Revert All to Default` 로 패널마다 다르다 |
| P3 | History · Catalog Editor · Multiplayer | 빈 영역에 이유와 다음 행동이 없다 |

판정 불가: Tab 순서와 포커스 표시, 메뉴의 단축키 표기, 클릭 대상 24px 실측, 오류 상태(해당 캡처 없음).

## 9. 더 볼 곳

| 무엇 | 어디 |
|---|---|
| 토큰 원본 | `Resource/editor/theme/` |
| 토큰 도구 | `Scripts/dev/MakeEditorTheme.py` |
| Figma MCP 설정 | `.mcp.json` |

## 10. 우클릭과 에디터 입력 · 명령 계층

패널이 ImGui 입력 함수를 저마다 직접 불러서 같은 우클릭이 패널마다 다르게 동작했고, 단축키 처리와 메뉴의 단축키 글자가 따로 놀았고, 끌어 놓기 페이로드 형식이 패널마다 달랐다. 이 절은 지난 모습(10-1, 10-2), 정한 규칙(10-3), 규칙을 형식으로 강제하는 계층(10-4), 구현 단위(10-5)를 적는다. **구현은 끝났고 통합 빌드와 에디터 시나리오 확인 전이다.**

### 10-1. 조사 당시의 우클릭(2026-10-27)

ImGui 의 `BeginPopupContextItem` · `BeginPopupContextWindow` 는 **뗄 때** 열고 끌기 거리는 보지 않는다. 패널별 조사에서 드러난 모순:

- 빈 곳 메뉴는 Hierarchy 하나뿐이었다.
- 선택 밖 항목 위 우클릭이 선택을 바꾸는 곳, 다중 선택을 대상으로 삼는 메뉴는 Console 하나뿐이었다. Hierarchy 는 다중 선택 위에서도 누른 **한 행**에만 작동했다.
- 끌기와 클릭을 가르는 곳은 씬 뷰포트 하나였다(끌기 문턱 6px 이나 WASD 비행이면 안 열고 Alt 면 궤도 줌).
- 메뉴의 단축키 글자(`Ctrl+D` 등)는 모두 손으로 적었고, 실제 처리는 패널이 따로 했다(Duplicate · Delete · Rename · Copy · Paste 같은 대상 명령은 명령 등록부에 없었다).
- 같은 대상인데 메뉴가 다른 곳: Inspector 의 메인 카드와 미리보기 창 카드.
- CurveEditor · PlayToolbar · DebugHUD 는 **누를 때** 메뉴가 열렸고, PlayToolbar 시간 배율은 우클릭이 메뉴가 아니라 1.0 으로 되돌리는 숨은 동작이었다. 씬 뷰포트 메뉴는 코어 항목이 0 이었다(확장이 없으면 메뉴 자체가 없음).

### 10-2. 패널이 입력을 직접 읽던 곳(2026-10-27)

에디터 39 파일 147 곳(`IsKeyPressed` · `IsMouseClicked` · `BeginPopupContextItem` · `OpenPopup` · 끌어 놓기 호출)과, `ImGuiIO` 필드(`io.KeyCtrl` · `io.MouseDown` …)를 직접 읽는 17 파일 75 줄이 있었다(OS 입력을 ImGui 로 넣는 `Common/Backend`, 게임 뷰의 게임 입력 전달은 허용). 모두 10-5 의 I3 단위에서 계층으로 옮겨 0 이 됐다. 끌어 놓기 페이로드는 세 형식(에셋 상대 경로 문자열, 하이어라키 오브젝트 `uint64` id, UI 팔레트 위젯 타입 이름)이고 받는 쪽이 이름 문자열을 하나씩 비교했다. 명령 등록부의 명령은 인자 없는 `void()` 동작이라 "누른 대상" 을 받을 수 없었고, 같은 `Ctrl+D` 를 Hierarchy 와 ContentBrowser 가 포커스 검사로 나눠 가졌다.

### 10-3. 정한 규칙

상용 엔진 비교는 공식 문서를 열어 보지 못해 모두 [기억] 이다(문서 링크로 바꿔야 한다). ImGui 동작만 헤더로 확인했다.

| 번호 | 규칙 | 비교 |
|---|---|---|
| R1 | 우클릭 메뉴는 **뗄 때** 열고, 누른 뒤 끌기 문턱(`io.MouseDragThreshold`, 한 상수)을 넘었거나 그 누름 동안 다른 동작(비행 · 화면 이동)이 있었으면 열지 않는다. 누를 때 여는 곳은 뗄 때로 바꾼다 | ImGui 기본이 뗄 때. 언리얼 뷰포트는 움직이지 않고 뗀 오른쪽 클릭에 메뉴 [기억] |
| R2 | 메뉴가 **열리는 프레임**에 대상이 선택 밖이면 그 대상만 선택하고, 선택 안이면 다중 선택을 그대로 둔다. 빈 곳 우클릭은 선택을 바꾸지 않는다. 누를 때 바꾸지 않는 까닭은 뷰포트에서 누름이 비행이 될 수 있어서다 | 유니티 Hierarchy · 언리얼 아웃라이너 · 고도 씬 트리 모두 선택 밖 항목 우클릭이 그 항목을 고른다 [기억] |
| R3 | 메뉴의 대상은 **선택 전체**다. 하나에만 뜻이 있는 명령(Rename · Open in IDE)은 다중 선택이면 꺼진다 | 유니티 · 언리얼 모두 다중 선택 메뉴가 선택 전체에 작동 [기억] |
| R4 | 메뉴 항목은 **명령 등록부의 명령 id** 만 가리키고 라벨 · 켜짐 · 단축키 글자는 명령이 정한다(사용자 덮어쓰기가 메뉴에 그대로 보인다). 패널이 `"Ctrl+D"` 같은 글자를 적지 않는다 | 언리얼 `FUICommandInfo` 가 메뉴에 단축키를 함께 그림 [기억] |
| R5 | 같은 종류의 대상은 어느 패널에서 열어도 **같은 메뉴**다(메뉴 종류 = 대상 종류). 패널 고유 항목은 그 종류의 한 구역으로만 더한다 | 언리얼 `UToolMenus` 의 메뉴 이름 · 구역 [기억] |
| R6 | 코어 항목은 확장이 없어도 열린다. 켜진 항목이 하나도 없으면 메뉴를 열지 않는다(빈 팝업 금지). 뷰포트 코어 항목은 오브젝트 위: 선택 명령(Frame · Duplicate · Delete …), 빈 곳: 여기에 만들기 · 붙여 넣기 | 언리얼 뷰포트 메뉴는 액터 메뉴를 포함 [기억] |
| R7 | 뷰포트 · 2D 캔버스(TileMap)는 **오른쪽 끌기 = 비행(2D 는 화면 이동), Alt+오른쪽 끌기 = 궤도 줌, 문턱 이하 뗌 = 메뉴**. TileMap 의 문턱 0 이동도 같은 문턱을 쓴다 | 유니티 씬 뷰 오른쪽 끌기 = 플라이스루, 고도 3D 뷰 오른쪽 누름 = 프리룩 [기억] |
| R8 | 우클릭에 메뉴가 아닌 숨은 동작을 두지 않는다(시간 배율 1.0 되돌림은 메뉴 항목 "Reset to 1.0" 으로) | — |
| R9 | 게임 뷰는 오른쪽 단추를 게임에 넘기고 에디터 메뉴를 열지 않는다 | 유니티 Game 뷰 [기억] |

### 10-4. 입력 · 명령 계층의 형식

목표: 패널은 "이 대상에 이 명령들이 있다" 와 "이 위젯은 이 종류를 끌고 이 종류를 받는다" 만 선언하고, 언제 눌렸는지 · 메뉴를 언제 여는지 · 선택을 언제 바꾸는지는 한 곳이 정한다. 그러면 R1 ~ R8 을 패널마다 다시 구현할 수 없다.

**결정: 엔진 입력 맵 위에 둔다.** 첫 안은 `Common/Commands` 에 범위 · 키 · 몸짓 바인딩 형식을 새로 만드는 것이었으나, 엔진에 데이터 주도 입력 맵(`InputMap`, `Resource/engine/input/*.input.xml` — 레이어 · 트리거 · 수정 키 조합 · 더블클릭, 사용자 재배치 파일, `InputMapPanel`)이 이미 있어 같은 일을 하는 두 번째 체계가 되므로 버렸다. **키 · 단추 · 몸짓은 엔진 입력 맵 하나가 맡고, 에디터 어댑터는 입력 맵에 없는 것(ImGui 포커스 · 호버로 정하는 범위, 맥락 메뉴, 끌어 놓기 페이로드)만 맡는다.** 에디터 명령의 단축키 칸도 이 맵으로 옮겨 두 체계를 하나로 만들었다(옛 단축키 형식과 `Saved/Editor/Shortcuts.json` 은 읽지 않는다).

| 필요 | 엔진에 이미 있는 것 | 에디터에서 쓰는 법 |
|---|---|---|
| 범위(포커스 창 → 바깥 → 전역) | 레이어(우선순위 · `blockLower` · `alwaysOn`) | `Resource/editor/input/editor.input.xml`: `Editor.Global`(alwaysOn) · 패널마다 `Editor.Panel.<패널 id>`. 어댑터가 프레임마다 포커스 창의 레이어만 켠다. 같은 `Ctrl+D` 를 두 패널 레이어가 각자 가진다(손 포커스 검사를 대신함). 텍스트 입력 중에는 `Editor.Global` 밖을 끈다 |
| 수정 키 조합 · 더블클릭 | `Shortcut` 바인딩(`modifier`), `DoubleClicked` 트리거, `doubleClickTime` · `doubleClickMaxDistance` | 그대로 |
| 메뉴의 단축키 글자 | `InputMap::getGlyphForAction` | 명령이 가리키는 액션의 글자를 그린다(R4) |
| 사용자 재배치 | `ShellInputActions` 의 저장 · 읽기(기본과 다른 슬롯만 XML) | `Saved/Editor/editorbindings.xml` 이 옛 JSON 을 대신한다 |
| 맵 소유 · 갱신 | 소유자가 만든 `InputMap` 을 `InputManager` 에 붙여 갱신 | 에디터 서브시스템 `EditorInputActions` 가 `editor.input.xml` 맵을 소유 |

**엔진에 더한 것**(게임도 쓰는 일반 기능이라 `Engine/Input` 에 둠):

| 번호 | 모자랐던 것 | 근거 |
|---|---|---|
| E1 | 수정 키 **정확 일치**. `areShortcutModifiersHeld` 가 요구한 키가 눌렸는지만 봐서 Ctrl+Shift+Z 가 Ctrl+Z 도 발동시켰다 | 런타임 UI 도 같은 겹침을 손으로 풀고 있었다 |
| E2 | `Click`(누른 뒤 `dragThreshold` 이하로 움직이고 뗌) · `Drag`(문턱을 넘은 누름) 트리거와 맵 속성 `dragThreshold`(R1 · R7 의 한 상수) | RTS 선택 상자 · 카메라 끌기와 클릭을 가르는 일반 기능 |
| E3 | 맵에 다른 파일의 액션 · 레이어를 더하기(`appendFromResource`). 확장 모듈이 자기 `*.input.xml` 을 더하고, 같은 레이어의 같은 조합은 읽을 때 오류 | 게임 모드 · DLC 가 기본 맵에 액션을 더하는 것과 같다 |
| E4 | 키보드 포커스 주인 하나 더(에디터 · 도구 창). 에디터 패널을 다른 OS 창으로 떼면 그 창의 키는 ImGui 만 받는다. 모든 창의 키를 `InputManager` 에 넣되 에디터 패널이 포커스인 동안 게임 조회는 키를 못 보게 한다 | 주인 값은 엔진 열거형이라 엔진, 키 전달은 에디터 |

**에디터 쪽 형식**

| 형식 | 자리 | 내용 |
|---|---|---|
| `EditorCommandTarget` | `Common/Commands`(ImGui 없음) | 명령이 받는 대상: 종류(`GameObject` · `Component` · `Asset` · `Viewport` · `LogRow` · `Property` · `CurveKey` …), 선택 스냅숏(id 목록), 커서 아래 항목 id, 월드 위치 |
| 명령 정의 | `EditorCommandRegistry.h` | 대상을 받는 동작 `void(const EditorCommandTarget&)` · 켜짐 `bool(...)`, 입력 맵 액션 이름(단축키 칸 대신 — 범위는 액션의 레이어가 정함), 놓일 맥락 메뉴 종류 · 순서. 확장 항목도 같은 등록 줄이다 |
| `EditorDragPayload` · `EditorPayloadKindSet` | `Common/Commands` | 페이로드는 `{종류, uint64 id}` 하나의 POD(에셋은 에셋 id, 오브젝트는 오브젝트 id, 위젯 타입은 타입 번호). 받는 쪽은 받을 종류 집합을 넘기고 집합 밖 종류는 어댑터가 거절한다(문자열 비교 없음) |
| `EditorInputActions` | `Common/Input` | `editor.input.xml` 맵 소유 · 포커스 창 → 레이어 켜기 · 명령 실행 |
| `EditorInputGUI` | `Common/Input` | 입력 맵에 없는 것만: `drawContextMenuFor( 대상, 메뉴 종류 )`(R1 · R2 · R3 · R6 을 이 함수 하나가 함), `beginDragSource( 페이로드 )`, `acceptDrop( 받을 종류 집합 )`. 키 · 단추 · 몸짓 판정은 두지 않는다 |

**막는 방법 — 형식이 먼저, 린트는 형식이 못 닿는 틈만.**

1. **형식(컴파일 오류).** 패널 · 에셋 액션 · 뷰포트 등 에디터 코드 전체에 `EditorInputBan.h` 를 ImGui 의 사용자 헤더 자리(`IMGUI_USER_H_FILENAME`)로 붙인다. 이 헤더가 ImGui 입력 · 팝업 · 끌어 놓기 함수를 `[[deprecated("Common/Input 을 쓴다")]]` 로 다시 선언하고 그 경고를 오류로 올린다(clang-cl · MSVC `/we4996`, clang · gcc `-Werror=deprecated-declarations`). 패널이 부르면 컴파일이 멈춘다. 어댑터 자신은 받지 않는다. 기각한 후보 둘: 패널이 `imgui.h` 를 include 하지 못하게 하는 래퍼, 패널에 액션 핸들만 주는 인터페이스 — 둘 다 패널이 그리기 함수 수백 개를 감싸야 해서 비현실적이다.
2. **린트(형식이 못 막는 틈).** `ImGuiIO` 입력 필드 읽기(`io.KeyCtrl` · `io.MouseDown` · `io.MouseDelta` …)만 센다. 형식으로 못 막는 이유: 같은 `ImGui::GetIO()` 가 입력이 아닌 필드(`DisplaySize` · `Fonts` · `DeltaTime`)에도 쓰여 함수 하나를 막으면 그것까지 막고, 구조체 필드는 `[[deprecated]]` 로 다시 선언할 수 없다. 기준선은 0 이고 하나라도 생기면 실패한다. 허용은 `Common/Backend`(OS 입력을 ImGui 로 넣는 곳)와 `Common/Input`(어댑터 자신)뿐이다.

### 10-5. 구현 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| IE | 엔진 입력 맵 E1 ~ E4(읽기 · 쓰기 · 재배치 · `InputMapPanel` 이 새 속성을 함께 다룸) | 코드 끝, 통합 빌드 전 |
| I0 | 명령 형식: 대상을 받는 동작 · 켜짐, 맥락 메뉴 칸, 페이로드 형식 | 코드 끝, 통합 빌드 전 |
| I1 | 어댑터와 단축키 이전: `Common/Input`, `editor.input.xml`, 명령 ↔ 액션 연결, 옛 단축키 형식 삭제 | 코드 끝, 통합 빌드 전 |
| I2 | 형식으로 막기: `EditorInputBan.h` 와 `ImGuiIO` 필드 린트 | 코드 끝, 통합 빌드 전 |
| I3a ~ g | 장면 · 콘텐츠 · 인스펙터 · 표 · 목록 · 캔버스 · 뷰포트 · 공용 위젯 · 셸 패널을 계층으로 이전 | 코드 끝, 통합 빌드 전 |
| I4 | 형식 켜기: 호출이 0 이 된 티어부터 강제하고 기준선을 지움 | 코드 끝(`SW_EDITOR_INPUT_BAN` 기본 ON — 기존 빌드 폴더는 캐시가 OFF 를 들고 있어 `-DSW_EDITOR_INPUT_BAN=ON` 으로 다시 구성) |

첫 일: 통합 빌드 · 에디터 시나리오(단축키 · 워크플로 · 콘텐츠 브라우저) 확인. 의존 순서는 IE → I0 → I1 → I3 들, I2 는 I0 와 함께, I4 는 I3 뒤였다. U4(장면 오브젝트 명령 공용화)는 대상을 받는 명령 형식이 있어야 한 번에 끝나므로 I0 · I1 뒤, I3a 앞에 두었고, U1(비행)은 뷰포트 파일이 I3f 와 겹쳐 함께 고쳤다.

**구현하며 정한 것**
- `<bind modifier>` 는 왼쪽 수정 키 Chord 가 아니라 수정 키 마스크 Shortcut(`modifier="Ctrl+Shift"`, 정확 일치)이다. 셸 · UI 맵의 수정 키 바인딩도 오른쪽 수정 키에 듣고, 다른 수정 키를 함께 누르면 발화하지 않는다.
- 에디터 맵은 `Resource/editor/input/editor.input.xml`이고 확장은 `SW_EDITOR_INPUT_MAP` 으로 맵을 더한다. 사용자 재배치는 `Saved/Editor/editorbindings.xml`(셸 맵과 같은 형식에 `modifier` 속성 추가).
- **키 길:** 에디터 창이 키보드를 가지면 `ImGuiEditor::processEvent` 는 OS 키 누름을 넘기지 않고, `EditorInputActions` 가 ImGui 가 받은 키를 입력 매니저에 넣으며 키보드 포커스를 `Tool` 로 둔다(주 창 · 떼어 낸 창 · 자동화 키가 한 길). 게임 뷰가 포커스면 OS 키가 그대로 간다. 셸 · UI 맵은 `Tool` 포커스 동안 키를 보지 않는다.
- 포커스 패널 → 레이어는 `EditorInputActions` 가 프레임마다 `isWindowFocused` 로 읽는다(패널 관리자를 고치지 않음).
- 단축키 창의 지우기(Backspace)는 없앴다(재배치 형식은 바인딩마다 키를 바꾸지 없애지 못한다). Exit 의 표시 전용 Alt+F4 글자도 없앴다.
- 확장 맥락 메뉴 항목은 코어와 같은 `SW_EDITOR_CONTEXT_COMMAND` 다(옛 확장 헤더 삭제).
- 메뉴에서 고른 명령은 다음 프레임 첫머리에 실행한다(`queueCommand`) — 그리는 중인 목록이 명령 뒤에 남지 않게.
- **맥락 메뉴는 둘이다.** 대상이 id 로 풀리는 것(오브젝트 · 컴포넌트 · 에셋 · 뷰포트)은 `drawContextMenuFor`, 대상이 그 프레임의 주소인 행 메뉴(속성 행 · 로그 행 · 표 칸 · 시간 배율)는 R1 만 맞춘 `beginItemContextMenu` 다. 메뉴 항목의 시나리오 표식은 `menu.<명령 id>` 다.
- **뷰포트 비행**은 모드 레이어 `Editor.Viewport.Fly`(우선순위 20, 아래를 막음)를 오른쪽 단추를 누른 동안 켜서 가른다. 패널 레이어는 포커스로 켜지므로 W · F · 숫자 키는 씬 뷰가 포커스일 때만 듣는다(예전에는 호버로도 들었다).
- 콘텐츠 브라우저 대상 id 는 절대 경로를 페이로드 이름 표에 올린 값이다. 컴포넌트 메뉴는 메인 · 미리보기 카드 · 계층 창 행이 같다.
- 금지는 패널 티어만이 아니라 에디터 코드 전체다(루트 모듈 · 에디터 확장 모듈 포함). `imgui_internal.h` 의 다른 겹(키 주인 · 입력 플래그)과 `OpenPopupEx` · `Shortcut` · `BeginDragDropTargetCustom` 도 막는다. 받지 않는 곳은 `SW_EDITOR_INPUT_ADAPTER` 를 정의한 두 파일(`EditorInputGUI.cpp` · `ImGuiKeyForward.cpp`)뿐이다.
- **편집 키 예외:** 목록 위젯 · 빠른 실행 창의 화살표 · Enter · Esc 는 글을 치는 동안 듣는 고정 키라 입력 맵(글 입력 중 멈춤)이 아니라 `EditorInputGUI::wasEditKeyPressed` 로 읽는다.
