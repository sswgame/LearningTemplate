# Engine/UI — 런타임(게임) UI

게임 화면의 메뉴 · HUD 가 쓰는 **유지형(retained) 위젯 트리**입니다. 티어 8 — 입력(6) · 글자(5) · 사용자 설정(7)을 쓰고, 렌더러와는 서로 include 하지
않습니다(사이의 값은 그리기 목록뿐 — 언리얼 Slate ↔ SlateRHIRenderer 의 선). 에디터의 ImGui(즉시 모드 — 매 프레임 전부 다시 짓는다)와 달리 트리는
남아 있고, 바뀐 것만 **이유를 나눠** 알립니다.

| 이 엔진 | 언리얼 | 유니티 UI Toolkit | Godot |
|---|---|---|---|
| `Widget` · `PanelWidget` · `WidgetTree` | `SWidget` · `SPanel` (UMG `UWidget` · `UPanelWidget` · `UWidgetTree`) | `VisualElement` · 패널 | `Control` · `Container` |
| 무효화 이유(`WidgetDirty`) | `EInvalidateWidgetReason` | `VersionChangeType` | `update_minimum_size` · `queue_redraw` |
| `UiSystem` | `FSlateApplication` + CommonUI 액션 라우터 | `EventSystem` · `PanelSettings` | `Viewport` GUI 입력 |

## 위젯 · 패널 · 트리 (`Core/`)

- **수명**: 부모 패널이 자식을 `unique_ptr` 로 소유합니다. 루트는 `WidgetTree::setRoot` 가 소유합니다. 트리 밖에서 위젯을 오래 가리킬 때는
  **`WidgetId`** 로 들고 `WidgetTree::findWidgetById` 로 풉니다 — 포인터는 그 호출 안에서만(오브젝트 핸들과 같은 규칙). 번호는 프로세스 안에서 다시 쓰지 않습니다.
- **이름표**: `findWidget<T>( "name" )`. 한 트리에 같은 이름이 둘이면 먼저 붙은 것이고 붙일 때 경고합니다.
- **보임** 다섯(`WidgetVisibility` — 언리얼 ESlateVisibility): Visible · Collapsed(자리도 없음) · Hidden(자리만) · HitTestInvisible(자기와 자식 클릭 없음) ·
  SelfHitTestInvisible(자기만 클릭 없음).
- **리플렉션**: 위젯은 리플렉션 타입이고(문서 · 인스펙터 · 핫 리로드가 이것으로 선다), 파생 위젯은 `getTypeInfo()` 를 자기 `StaticType()` 으로 덮어씁니다
  (RTTI 가 없다 — `castTo` 가 동적 타입을 이것으로 안다).
- 스레드: 게임 스레드만.

## 무효화

세터는 값이 같으면 아무것도 하지 않고, 다르면 `invalidate( 이유 )` 입니다.

| 이유 | 뜻 | 번지는 곳 |
|---|---|---|
| `kLayout` | 원하는 크기가 바뀔 수 있다 | 부모 쪽으로 `kChildLayout` 을 올리다 **레이아웃 경계**(`isLayoutBoundary` — 크기가 자식에 기대지 않는 위젯)나 루트에서 멈추고, 거기를 "다시 잴 뿌리" 로 적는다 |
| `kArrange` | 크기는 같고 자식 자리만 | 자기 |
| `kPaint` · `kTransform` | 그림만 · 렌더 변환/불투명도만 | 자기(그리기 목록) |
| `kStyle` | 계산된 스타일(상태 · 클래스) | 자기(스타일 목록) |
| `kVisibility` | 보임 | 보수적으로 레이아웃 + 그리기 |

레이아웃만 부모로 번지는 것이 유지형 UI 의 핵심입니다 — 위젯 1 만 개에서 글 하나가 바뀌어도 경계 안만 다시 잽니다.

## 서비스(`UiSystem`)

`engine::getUiSystem()` · 게임은 `game::getService<UiSystem>()`. 기동 단계 `Ui`(Client 대상 — 전용 서버는 세우지 않는다). 틱은 둘입니다.

- `processInput` — `EngineLoop::beginFrame` 의 입력 갱신 직후, **게임 틱 앞**. UI 가 먹은 입력을 폰이 못 보게.
- `update` — 게임 틱 뒤, 렌더 패킷 앞. 애니메이션 → 바인딩 → 스타일 → 레이아웃 → 그리기. 레이아웃은 화면 트리마다 `UiLayoutPass::update`
  (화면마다 뷰포트 전체가 루트 사각형 — 작은 창은 루트 슬롯의 여백 · 크기 덮어쓰기 또는 루트 패널 안의 자리로).
- 포인터 위치는 창 픽셀을 지난 `update` 의 뷰포트 배율로 나눈 UI 단위입니다(입력은 레이아웃 앞이라 지난 프레임에 그린 화면 기준).

## 히트 테스트 · 사건 경로 (`Core/UiEventRouter` · `Core/UiPointerState`)

- **히트 테스트**는 그리기 역순(뒤에 붙인 자식이 위 — z 순서를 둔 패널은 `PanelWidget::collectPaintOrder` 의 역순)으로 내려가며 점이 든 가장 깊은 위젯 경로(뿌리 → 잎)를 찾습니다. 보임 다섯을 따르고, 렌더 변환은
  역변환으로 풀고(돌린 사각형의 모서리 바깥은 맞지 않는다), 자르는 패널(`setClipChildren`) 밖의 점은 그 자식도 받지 않습니다.
- **경로**로 사건을 보냅니다 — 터널링(뿌리 → 잎, 미리보기) 다음 버블링(잎 → 뿌리). `UiReply::makeHandled()` 에서 멈춥니다(언리얼 FReply · 유니티
  TrickleDown/BubbleUp). 경로는 번호라 사건 중에 떨어진 위젯은 건너뜁니다. 꺼진 위젯은 맞기는 하지만(아래로 클릭이 새지 않게) 사건은 받지 않습니다.
- **포인터 잡기**: Down 에서 `capturePointer()` 한 위젯(슬라이더 끌기)은 포인터가 밖으로 나가도 놓을(`releasePointer()`) 때까지 사건을 받습니다.
- **호버**는 경로를 타지 않는 알림입니다 — 옛 경로와 새 경로의 차이에서 빠진 쪽은 잎부터 Leave, 새로 든 쪽은 뿌리부터 Enter(`onHoverChanged`, 상태 스타일 kStyle).

## 포커스 · 공간 탐색 (`Core/UiFocusManager` · `Core/UiNavigationSolver`)

- 포커스는 사용자(로컬 플레이어)마다 하나입니다(지금은 사용자 0). 포커스 위젯 번호는 그 트리에 적혀 위젯이 떨어지면 트리가 풀고, 트리가 지워지면 관리자에게 알립니다.
  받을 수 있는 위젯 = `supportsFocus` · 자기와 조상이 모두 켜져 있고 보인다.
- 방향 탐색(언리얼 `EUINavigationRule` · Godot `focus_neighbor`): 포커스 위젯에서 조상으로 올라가며 방향 항목(`WidgetNavigation`)을 본다 — 첫 `Explicit` 은 그 이름의
  위젯(받을 수 없으면 다음 규칙), 첫 `Stop` · `Wrap` 위젯이 찾는 범위, 모두 `Escape` 면 화면 뿌리. 범위 안에서 **공간 점수 = 주축 거리 + 2 × 수직축 틈**이 가장 낮은 것
  (같은 줄 · 같은 열을 먼저), 같으면 중심 거리 → 문서 순서(결정적). 없고 `Wrap` 이면 반대쪽 끝, `Next` · `Previous` 는 문서 순서로 돈다.
- 자르는 패널 밖으로 완전히 나간 위젯은 후보가 아니다 — 스크롤 패널(`canScrollIntoView`)이 자른 것은 후보이고, 포커스가 가면 그 패널이 보이게 옮긴다.

## 화면 스택 (`Screen/UiScreen` · `UiSystem`)

| 이 엔진 | 언리얼 CommonUI | 유니티 | Godot |
|---|---|---|---|
| `UiScreen` · `UiLayer` · `UiSystem::pushScreen` | `UCommonActivatableWidget` · 레이어 스택(`UCommonActivatableWidgetStack`) | (직접) | `CanvasLayer` |

- 층(`UiLayer` — Hud · GameMenu · Menu · Modal · Overlay · Loading)마다 스택입니다. 그리기 = 층 → 쌓인 순서, 입력 = 그 역순.
- **활성 화면** = 맨 위의 포커스 받는 화면(HUD · 오버레이 층은 받지 않는다 — 막는 화면 아래로는 내려가지 않는다). 포커스 · UI 행동은 활성 화면만 받습니다.
- **모달 · 로딩**은 아래 화면의 클릭과 게임 입력을 막습니다(`isGameInputBlocked` — 플레이어 조종자가 의도를 만들 때 본다). 오버레이(알림)는 포인터를 받지 않습니다.
- 덮이면 그때의 포커스를 기억했다가 다시 활성이 되면 돌려줍니다. 탐색 방식이면 열 때 `_defaultFocus`(없으면 문서 순서 첫 위젯).
- **닫기는 지연**입니다 — 사건 처리 중에 자기 화면을 닫아도 경로는 끝까지 돌고, `processInput` 끝 · `update` 앞에서 지웁니다.
- `_bPausesGame` 화면이 하나라도 있으면 `GameTimeScale::addPauseRequest` 하나를 걸어 둡니다(`gv_timeScale` 은 그대로).
- 게임이 마우스를 잠가 쥔 동안(1 인칭)은 포인터 사건을 만들지 않습니다.
- 핫 리로드: 내려가는 모듈에 vtable 이 있는 화면 · 위젯이 든 화면은 그 자리에서 닫습니다(경고 "closed for module reload"). 위젯 타입의 정적 상태는 모듈에 두지 않습니다.

## 행동 입력 · 먹은 입력 (`Input/UiInputConsumption` · `UiSystem::processInput`)

UI 는 키 · 버튼을 직접 보지 않고 **행동**을 받습니다 — 키 바인딩 · 패드 · 가상 입력(자동화)이 한 길로 옵니다.

| 이 엔진 | 언리얼 | 유니티 | Godot |
|---|---|---|---|
| UI 행동 맵 `engine/input/ui.input.xml`(레이어 `UI`) | CommonUI 입력 액션 데이터 · `FNavigationConfig` | Input System UI 모듈(`InputSystemUIInputModule`) | `ui_*` 입력 액션 |
| `UiInputConsumption` · `UiSystem::isActionConsumed` | Enhanced Input 입력 소비 · CommonUI 액션 라우터 | `EventSystem.IsPointerOverGameObject` 로 게임이 거른다 | `accept_event()` · `set_input_as_handled()` |

- **UI 행동 맵**은 `UiSystem` 이 소유합니다(`EngineDefaultAssets::_uiInputMap`, Shipping 에도 있다 — 셸 맵은 개발 도구라 쓰지 않는다). 레이어 `UI` 는 활성 화면이
  있을 때만 켭니다 — HUD 만 있으면 패드 A 는 게임의 것입니다. 탐색은 `trigger="Repeat"`(누를 때 한 번 · 지연 뒤 간격마다), 스틱은 큰 축 방향으로 한 칸 뒤 같은 간격으로.
- 순서: 뗀 입력 풀기 → UI 맵 갱신 → 입력 방식(이번 프레임 마지막 장치 — `InputManager::getLastFrameEvents`) → 포인터 → 행동(포커스 경로에 먼저, 아무도 안 먹으면
  포커스 이동 · `UI.Back` 은 화면의 `onBack`) → 닫기 요청 → 글 포커스.
- **먹은 입력**: UI 가 쓴 행동의 물리 입력(키 · 패드 버튼 · 조합 키의 트리거 키 · 기운 스틱)과 위젯이 처리한 마우스 버튼은 **뗀 프레임까지** 먹힌 입력입니다.
  게임 쪽에서 이것을 보는 자리는 **플레이어 조종자 하나**입니다(`PlayerControllerComponent` — 먹힌 행동은 의도에 넣지 않고, 모달 · 로딩이면 의도 0,
  `wantsCursor` 면 마우스 잠금을 쉰다). 입력 맵이 둘이어도(UI 맵 · 게임 맵) 물리 슬롯으로 견주므로 메뉴에서 누른 패드 A 가 점프가 되지 않습니다.
- **글 입력 칸**(`supportsTextInput`)이 포커스를 쥐면 키보드 포커스 `Ui` 를 잡습니다 — 게임은 키를 보지 못하고(칸에 있는 동안 누른 키는 뗄 때까지), 글자 · 조합은
  그 위젯의 `onTextEvent` 로 갑니다. 그 동안 UI 행동은 `UI.Back` · `UI.FocusNext/Previous` 만(스페이스가 확인이 되지 않게). 개발 콘솔이 열리면 UI 는 행동을 받지 않습니다.
## 레이아웃 (`Layout/`)

어느 엔진이나 **두 번 걷기**입니다 — 아래에서 위로 "얼마나 크고 싶나"(measure — Slate `ComputeDesiredSize` · WPF `Measure` · Yoga), 위에서 아래로
"여기에 이 크기로 놓아라"(arrange — Slate `OnArrangeChildren` · Godot `fit_child_in_rect`). `UiLayoutPass::update( tree, context )` 가 돌립니다.

- **measure 는 가용 크기를 받는다**(`computeDesiredSize( context, availableSize )` — WPF · Yoga 와 같고 Slate 와 다르다). 줄 바꿈 글이 Fill 칸에 들어가면 그 칸 너비로
  높이를 정해 **같은 프레임에** 맞는다. 축이 `kUiUnbounded` 면 그 축은 원하는 만큼이고, 비교는 `UiLayoutPass::isUnbounded`(여백을 빼도 무한으로 남게).
- **캐시**: 위젯은 마지막 가용 크기와 결과를 든다. 더럽지 않고(`kLayout` · `kChildLayout` 없음) 같은 가용 크기면 다시 재지 않는다. 한 걷기 안에서 같은 위젯을
  같은 가용 크기로 두 번 재지 않는다(걷기 번호).
- **뿌리만 다시**: 트리의 "다시 잴 뿌리"(레이아웃 경계 · 루트 · `kArrange` 위젯)만 지난 가용 크기 · 지난 슬롯 자리로 다시 잰다 · 놓는다. 크기 덮어쓰기가 두 축 다 있는 위젯이
  레이아웃 경계다(`WidgetLayoutSlot::hasFixedSize`). `kArrange`(스크롤 오프셋)는 위로 번지지 않고 그 위젯만 다시 놓는다(measure 0).
- **접힌(Collapsed) 위젯**은 재지도 놓지도 않는다. 걷기가 끝날 때 그 아래로 내려가지 않고 `kChildLayout` 을 남겨, 다시 보이면 그 아래를 다시 잰다.
- 배율 · 글자 배율이 지난 걷기와 다르면 트리 전체를 다시 잰다(`invalidateAllLayout`).
- **픽셀 맞춤**: 부모 축이 단위(회전 · 기울임 없음)면 사각형의 변을 `round( v × uiScale ) / uiScale` 로 — 1 px 테두리가 번지지 않는다(Slate 픽셀 스냅).
- 기하가 바뀐 위젯만 그리기 더러움(`setArrangedGeometry`).

**슬롯**(`WidgetLayoutSlot`) — Godot 처럼 모든 패널의 칸을 위젯 하나에 모으고 부모는 자기 칸만 읽는다(리플렉션 타입이 하나라 문서 · 인스펙터 · 핫 리로드가 단순하다).
UMG 는 부모 종류마다 슬롯 객체(`UCanvasPanelSlot` · `UHorizontalBoxSlot`)다. 공통: 여백 · 정렬(Fill · Start · Center · End) · 크기 덮어쓰기 · 최소 · 최대.

| 패널 | 규칙 | 언리얼 · 유니티 · Godot |
|---|---|---|
| `BoxPanel` | 주축: Auto 는 원하는 크기, 남은 것은 Fill 에 채우기 비대로 · 교차축: 슬롯 정렬 · 간격 | HorizontalBox/VerticalBox · flex-direction · HBox/VBox |
| `OverlayPanel` | 모든 자식이 패널 전체를 슬롯으로(정렬 · 여백) | Overlay · (겹침) · Container |
| `CanvasPanel` | 변 = 앵커 × 패널 크기 + 오프셋, 자동 크기는 커지는 쪽으로 · z 순서(`collectPaintOrder`) | Canvas Panel 앵커 · position absolute · 앵커/오프셋 |
| `GridPanel` | 열 · 행 트랙 Auto · Fixed · Fill(`UiGridTrack`), 넓이 2 이상은 모자란 만큼을 덮은 Auto 트랙에 고르게 — CSS Grid 의 단순판. 열을 먼저 정하고 그 너비로 다시 재 행을 정한다 | GridPanel · (grid 없음) · GridContainer |
| `WrapPanel` | 줄이 차면 다음 줄, 줄 높이는 그 줄의 최대 · 칸 간격 · 줄 간격 | WrapBox · flex-wrap · FlowContainer |
| `ScrollPanel` | 내용 하나를 스크롤 축으로 무한 measure → `-오프셋` 에 놓고 자른다. 오프셋은 `[0, 내용 − 보이는 크기]`, 바뀌면 `kArrange` 만(measure 0). `scrollIntoView` 는 최소한만 옮긴다 | ScrollBox · ScrollView · ScrollContainer |

결과를 견주는 형식은 `UiLayoutDump::makeDump( tree )` — 줄마다 `<깊이 들여쓰기><이름> x y w h`(소수 둘째 자리).

스크롤 입력은 `ScrollPanel` 이 사건으로 받는다(UMG ScrollBox · Godot ScrollContainer 와 같은 넷):

- **휠**(버블) — 한 칸 `_wheelStep`. 끝에 닿아 못 옮기면 처리하지 않아 바깥 스크롤 패널이 받는다.
- **막대 끌기**(터널 — 막대는 내용 위에 겹쳐 그려 버튼보다 먼저 받는다) — 엄지를 누르면 포인터를 잡고 끈 거리 × 최대 오프셋 / (트랙 − 엄지)만큼, 트랙을 누르면 한 화면.
  막대 사각형은 `computeScrollBar( 축 )`(그리기도 같은 값).
- **패드 오른쪽 스틱**(`UI.Scroll` 행동) — 포커스 경로(포커스가 없으면 포인터가 올라간 경로)로 가고 `_stickSpeed` × 프레임 시간만큼. 쓴 스틱은 먹힌 입력이다.
- **포커스 탐색** — `UiFocusManager::navigate` 가 옮긴 뒤 조상 패널마다 `scrollIntoView`. 스크롤 패널이 자른(완전히 밖인) 항목도 탐색 후보다(`canScrollIntoView`).

### 흐름 방향 · 오른쪽에서 왼쪽 배치 거울

위젯마다 `_flowDirection`(`Inherit` · `LeftToRight` · `RightToLeft` — UMG FlowDirection · Godot `layout_direction`). `Inherit` 은 부모를, 루트는 문화권
(`UiLayoutContext::_bRightToLeft` = `UiLayoutPass::isCultureRightToLeft()` — 의사 문화권 `qps-plocm` 도 RTL)을 따른다. 숫자 입력 칸 · 시계는 `LeftToRight` 로 고정한다.
푼 결과는 arrange 가 위젯에 적고(`isRightToLeft`), 글 위젯은 그것을 문단 방향(`TextLayoutStyle::_paragraphDirection`)으로 넘긴다.

- **두 자리에서만 거울한다.** 패널은 늘 왼쪽에서 오른쪽으로 계산한다. `PanelWidget::arrangeChild` 가 RTL 패널이면 자식 사각형을 패널 너비로 거울(x → 너비 − x − 폭) —
  상자의 가로 순서 · 캔버스의 앵커와 오프셋(`앵커 x → 1 − 앵커 x`, 오프셋 부호, 커지는 쪽까지) · 격자 열 · 흐름 줄이 한 번에 뒤집힌다. `UiLayoutPass::arrange` 는 자기 슬롯을
  **부모의** 방향으로 읽어 RTL 이면 여백 왼 ↔ 오 · 가로 정렬 Start ↔ End. 새 패널에 방향 분기를 넣지 않는다.
- 스크롤 패널은 내용 자리(스크롤 오프셋)를 거울로 놓지 않는다(`mirrorsChildrenInRightToLeft` false) — 내용 안의 패널이 자기 방향으로 거울한다.
- 방향을 바꾸면 `kArrange`(크기는 그대로 — measure 0). 문화권이 바뀌어 루트의 방향이 달라지면 다음 걷기가 트리 전체를 다시 놓는다.
- 포커스 탐색 Left · Right 는 화면 기준이라 그대로다(결과 사각형으로 고르므로 거울 배치에 저절로 맞는다). 히트 테스트도 기하를 보므로 따로 할 일이 없다.

## 배율 · 안전 영역

레이아웃은 **UI 단위**(기준 해상도 1920×1080 의 픽셀)로 하고, 화면에 낼 때 배율을 곱합니다(`UiViewport` — `_size` · `_physicalSize` · `_uiScale` · `_safeInsets`).

- 배율 = clamp( 해상도 규칙( 물리 크기 ), 최소, 최대 ) × `gv_uiScale` — 규칙은 `UiScaleSettings`(`engine/ui/uiscale.xml`, 게임 프리셋 `_uiScaleSettings` 가 덮어쓴다):
  `ShortestSide`(짧은 변 / 기준 짧은 변 — 기본, 가로 · 세로 화면 모두) · `Width` · `Height` · `Fixed`. UMG 의 DPI 곡선(해상도 짧은 변 → 배율) 자리입니다.
- 창의 OS 배율(`IWindow::getContentScale` — Win32 `GetDpiForWindow`, X11 `Xft.dpi`)은 `_bApplyContentScale` 일 때만 곱합니다(데스크톱식 UI). 게임 창은 해상도 규칙이
  이미 창 크기를 따르므로 둘 다 곱하면 두 번 곱한다(UMG 기본과 같다).
- `gv_uiTextScale` 은 글 측정에만 곱합니다(`UiLayoutContext::_textScale`) — 글이 커지면 그 상자도 커진다. 배율 · 글자 배율이 바뀌면 트리 전체를 다시 잽니다.
- 안전 영역: `SafeZonePanel` 이 뷰포트 안전 영역과 겹치는 만큼 자식을 안쪽으로 민다. PC 는 0 이고 `gv_uiDebugSafeZone`(0..0.1 — 각 변 비율, 언리얼
  `r.DebugSafeZone.TitleRatio`)으로 흉내 낸다. 화면 문서의 기본 루트는 `SafeZonePanel > CanvasPanel` 입니다.
- `UiSystem::computeViewport( 물리 크기, 창 배율 )` 이 뷰포트를, `makeLayoutContext()` 가 레이아웃 문맥을 만든다 — `EngineLoop` 가 프레임마다 앞의 것을 `update` 에 넘긴다.
