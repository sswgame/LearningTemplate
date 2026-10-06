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
  문서로 연 기본 화면(화면 클래스가 그 모듈 것이 아닌 것)은 다음 `update` 가 같은 문서로 다시 엽니다(새 이미지의 위젯 타입으로).

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

## 그리기 (`Render/UiPaintPass` · `Widgets/`)

| 이 엔진 | 언리얼 | 유니티 UI Toolkit | Godot |
|---|---|---|---|
| 위젯 그림 캐시 · `UiPaintPass` | Slate 캐시된 요소 목록 · 전역 무효화 | UIR 더러운 요소 다시 칠하기 | CanvasItem 명령 캐시 |
| `TextWidget` · `ImageWidget` · `BorderPanel` · `UiBrush` | `STextBlock` · `SImage` · `SBorder` · `FSlateBrush` | Label · Image · VisualElement 배경 | Label · TextureRect · PanelContainer + StyleBox |
| `ButtonWidget` · `CheckBoxWidget` · `SliderWidget` · `ProgressBarWidget` | Button · CheckBox · Slider · ProgressBar | Button · Toggle · Slider · ProgressBar | Button · CheckBox · HSlider · ProgressBar |
| `ComboBoxWidget` · `TextInputWidget` · `ListViewWidget` | ComboBox · EditableTextBox · ListView | DropdownField · TextField · ListView | OptionButton · LineEdit · ItemList |

- 위젯은 `paint( painter, context )` 에 **자기 로컬 (0, 0) ~ 크기**로 칠합니다(칠하기 도구의 변환이 위젯 기하다). 결과는 위젯의 **그림 캐시**(물리 픽셀 사각형 — 조상의
  자르기 · 불투명도가 구워져 있다)에 남고, 그리기 걷기는 그리기 순서(z 순서 패널은 `collectPaintOrder`)로 트리를 걸으며 캐시를 프레임 목록에 **이어 붙입니다**
  (`CanvasDrawList::appendDrawList` — 가위 · 텍스처가 맞으면 일괄을 합친다).
- 다시 칠하는 위젯: `kPaint` · `kStyle`(스타일 걷기 전에는 상태 겉모습이 위젯 칸이라) · `kVisibility` · 처음 · **조상의 `kTransform`**(불투명도 · 렌더 변환은 자손 캐시에
  구워져 있다) · UI 배율 변화(전부) · 글리프 아틀라스 세대 변화(글 위젯만 — 비운 페이지의 사각형이 다른 글자를 그린다). 흐름 방향이 바뀌면 레이아웃이 `kPaint` 를 건다.
- 자르는 패널은 자기 사각형을 가위로 쌓고, 자식 위 그림(스크롤 막대)은 `paintOverChildren` — 자르기 밖이다.
- `UiSystem::update` 가 화면을 그리기 순서로 칠하고(구간 `GT.Ui.Paint` · 카운터 `Ui.PaintWidgets`), **탐색 입력 방식이면** 포커스 위젯 둘레에 테두리를 그 화면 위에
  얹는다(포인터 방식이면 숨긴다 — CommonUI 와 같다). 목록 내용이 지난 프레임과 같으면 내용 번호(`getCanvasRevision`)를 올리지 않아 렌더러가 사각형을 다시 올리지 않는다.
  `EngineLoop` 가 목록을 렌더 패킷의 주 출력 캔버스에 싣는다(`-gv_canvasTestPattern` 은 그 위). 게임 창의 뷰포트 0 은 백버퍼 크기다.
- 개발 시험 화면 `-gv_uiDemo=1`(`Debug/UiDemoScreen` — 글 · 리치 텍스트 · 버튼 다섯 · 슬라이더 · 체크 · 진행 · 콤보 · 입력 칸)로 네 백엔드 스크린샷을 본다.
  입력 방식은 커서가 실제로 움직일 때만 포인터로 바뀐다(창이 뜰 때 OS 가 보내는 같은 자리 이동은 세지 않는다).
- 글 위젯: 원하는 크기 = 측정(글자 배율을 곱한 크기, 줄 바꿈이면 가용 너비 안), 칠하기는 위젯 너비로 배치하고 결과를 캐시한다. 글 · 스타일은 `kLayout`, 색은 `kPaint`.
  문단 방향 = 위젯의 흐름 방향. 리치 텍스트(`_bRichText`)는 `RichTextParser` 표기.
- 그림 위젯: 브러시(색 · 둥근 모서리 · 9-슬라이스) × 그림, 그림이 없으면 단색 상자. `_bMirrorInRtl` 이면 오른쪽에서 왼쪽에서 좌우로 뒤집는다.
  그림은 텍스처 객체(`setImage`) 또는 **경로**(`_imagePath` — 문서 · `setImagePath`)다. 게임 스레드에는 디바이스가 없어 경로를 풀지 않고 그리기 목록에
  경로로 싣는다(`CanvasTextureRef::_texturePath`) — 렌더 스레드의 `CanvasRenderer::prepareFrame` 이 머티리얼 텍스처와 같은 `TextureCache` 에서 빌린다.
  경로 그림은 게임 스레드가 크기를 모르므로 원하는 크기는 `_imageSize`(또는 슬롯 크기 덮어쓰기)다.
- **버튼 클릭 = 같은 버튼 위에서 누르고 뗌**(누른 동안 포인터를 잡는다 — 밖에서 떼면 취소) 또는 포커스 상태의 `UI.Accept`. 누르면 포커스가 버튼으로 온다. 상태(보통 · 호버 ·
  누름 · 꺼짐)별 배경은 위젯 칸(스타일 시트 5-2 가 채울 자리).
- **슬라이더**는 포커스 상태에서 `UI.NavigateLeft/Right` 를 먹는다(값 한 칸 — 포커스는 위 · 아래로만 떠난다). 오른쪽에서 왼쪽이면 최소가 오른쪽.
- **콤보 상자**는 위젯이 든 화면의 `UiSystem` 에 팝업 화면(Modal 층 · 막지 않음)을 올린다 — 항목 버튼이 상자 아래, 밖 클릭 · `UI.Back` 은 닫기. 팝업은 상자를 화면 핸들 ·
  위젯 번호로 다시 찾는다(`WidgetTree::getScreen` — 화면이 트리를 소유한다).
- **글 입력 칸**은 포커스를 쥐면 키보드 포커스 `Ui` — 글자 사건은 끝에 붙고, IME 조합 글은 확정 전까지 따로 보이고, `UI.TextBackspace`(칸이 키보드를 쥔 동안만 UiSystem 이
  보낸다)는 끝 코드 포인트 하나, Enter 는 확정 알림. 커서는 글 끝.
- **가상 목록**(`ListViewWidget`)은 보이는 줄 + 1 개만 줄 위젯을 만들고(`setRowFactory`) 항목 k 를 늘 줄 k % 줄 수에 묶는다(`setRowBinder`) — 한 줄 스크롤에 한 위젯만
  다시 묶고, 보이는 동안 항목과 위젯이 바뀌지 않아 포커스가 항목을 따라간다. 줄 위젯은 레이아웃 안에서 만들어진다(곧바로 놓인다).

## 월드에 붙는 UI (`World/WidgetComponent`)

| 이 엔진 | 언리얼 | 유니티 | Godot |
|---|---|---|---|
| `WidgetComponent` Screen · World | `UWidgetComponent` Space Screen · World | World Space Canvas | SubViewport → ViewportTexture → 3D 메시 |

- **Screen**: 위젯을 `UiSystem` 의 Hud 층 **마커 화면**(캔버스 패널 루트 · 클릭을 막지 않는다 — 처음 붙일 때 만들고 마지막을 뗄 때 닫는다)에 자식으로 붙인다.
  `UiSystem::update` 가 레이아웃 앞에서(게임 틱 · 트랜스폼 적용 뒤 — 병렬 틱 밖) 등록된 컴포넌트마다 오브젝트의 월드 점을 게임 카메라로 투영해 슬롯을 옮긴다
  (`computeMarkerPlacement` — 순수 함수). `_drawSize` 가 있으면 그 크기로 고정해 레이아웃 경계가 되고, 없으면 원하는 크기에 피벗을 맞춘다. 카메라 뒤 · 화면 밖은 숨기거나
  `_bClampToScreenEdge` 면 가장자리(여백 24)에 붙이고(카메라 뒤는 나누기 전 클립 방향 — 오른쪽 뒤면 오른쪽 변), `_maxDistance` 밖은 숨긴다. `_bScaleWithDistance` 는
  기준 거리 / 거리(0.25 ~ 2)를 피벗 둘레 렌더 변환으로. 2D(직교) · 3D 가 같은 코드다.
- **World**: 위젯을 컴포넌트 자기 트리(화면 스택 밖 — 포커스 · 입력 없음)에 놓고, `UiSystem::update` 가 렌더 텍스처 크기(`_drawSize`, 배율 1)로 놓고 칠해
  내용이 바뀔 때만 번호를 올린다. `EngineLoop` 가 목록(`rendertarget/widget_<컴포넌트 id>`)을 렌더 패킷 캔버스의 대상 목록에 싣고, 렌더러는 장면 뷰보다 먼저(프리패스 리스트)
  그 텍스처를 지우고 그린다(번호 · 텍스처가 그대로면 건너뛴다 — 텍스처가 그림을 지킨다). 시작할 때 오브젝트 밑에 사각형 자식(스프라이트 사각형 메시 · sprite2d 머티리얼 인스턴스
  — albedoMap = 그 렌더 텍스처, `premultipliedTexture` 로 곧은 알파로 되돌린다, 크기 `_worldSize` m)을 만들고 끝날 때 지운다. 렌더 텍스처 크기는 먼저 알린다
  (`declareRenderTarget` — 먼저 빌리는 쪽이 크기를 정한다).
- 컴포넌트는 시작할 때 엔진의 UI 시스템에 묶이고(`bindUiSystem` — 서버처럼 없으면 아무것도 하지 않는다) 끝날 때 풀린다. UI 시스템이 먼저 내려가면 `forgetUiSystem`.
- 렌더 변환(`setRenderTransform`)은 기하에 얹히는 값이라 그 위젯을 배치 뿌리로 적는다(`kArrange` — 재기 없이 다시 놓는다). 불투명도는 그리기만.

## 배율 · 안전 영역

레이아웃은 **UI 단위**(기준 해상도 1920×1080 의 픽셀)로 하고, 화면에 낼 때 배율을 곱합니다(`UiViewport` — `_size` · `_physicalSize` · `_uiScale` · `_safeInsets`).

- 배율 = clamp( 해상도 규칙( 물리 크기 ), 최소, 최대 ) × `gv_uiScale` — 규칙은 `UiScaleSettings`(`engine/ui/uiscale.xml`, 게임 프리셋 `_uiScaleSettings` 가 덮어쓴다):
  `ShortestSide`(짧은 변 / 기준 짧은 변 — 기본, 가로 · 세로 화면 모두) · `Width` · `Height` · `Fixed`. UMG 의 DPI 곡선(해상도 짧은 변 → 배율) 자리입니다.
- 창의 OS 배율(`IWindow::getContentScale` — Win32 `GetDpiForWindow`, X11 `Xft.dpi`)은 `_bApplyContentScale` 일 때만 곱합니다(데스크톱식 UI). 게임 창은 해상도 규칙이
  이미 창 크기를 따르므로 둘 다 곱하면 두 번 곱한다(UMG 기본과 같다).
- `gv_uiTextScale` 은 글 측정에만 곱합니다(`UiLayoutContext::_textScale`) — 글이 커지면 그 상자도 커진다. 배율 · 글자 배율이 바뀌면 트리 전체를 다시 잽니다.
  배율이 줄여도 글은 12 UI 단위 밑으로 가지 않는다(`UiScaleUtil::computeScaledFontSize` — Xbox 접근성 지침의 최소 글 크기). 그보다 작게 적은 글은 적은 크기가 하한이다.
  배율 2 에서 넘치지 않게 하는 것은 문서의 몫이다 — 긴 이름은 `_overflow="Ellipsis"`(줄 바꿈 없이), 늘어난 줄은 `ScrollPanel` 로(`UiAccessibilityTest.OptionsMenuFitsAtDoubleTextScale`).
- 안전 영역: `SafeZonePanel` 이 뷰포트 안전 영역과 겹치는 만큼 자식을 안쪽으로 민다. PC 는 0 이고 `gv_uiDebugSafeZone`(0..0.1 — 각 변 비율, 언리얼
  `r.DebugSafeZone.TitleRatio`)으로 흉내 낸다. 화면 문서의 기본 루트는 `SafeZonePanel > CanvasPanel` 입니다.
- `UiSystem::computeViewport( 물리 크기, 창 배율 )` 이 뷰포트를, `makeLayoutContext()` 가 레이아웃 문맥을 만든다 — `EngineLoop` 가 프레임마다 앞의 것을 `update` 에 넘긴다.

## 문서 (`Document/` · `*.ui.xml`)

| 이 엔진 | 언리얼 | 유니티 UI Toolkit | Godot |
|---|---|---|---|
| `*.ui.xml` · `UiDocumentLoader` · `UiDocumentCache` | UMG 위젯 블루프린트 | UXML · `VisualTreeAsset` | `.tscn` |
| `UserWidget`(조각) | 위젯 블루프린트 안의 위젯 | UXML `Template` 인스턴스 | 씬 인스턴스 |
| `_command` → `UiScreen::onCommand` · `registerCommand` | 버튼 `OnClicked` 바인딩 | 컨트롤러가 `Q<Button>( "name" ).clicked` | 시그널 연결 |

화면은 데이터로 짓습니다 — 구조(문서)와 겉모습(스타일 시트, 5-2)을 나눕니다(유니티 UXML · USS 와 같은 나눔, 형식은 둘 다 엔진 XML 하나).

```xml
<UiDocument _schemaVersion="1">
	<UiScreenDesc _bPausesGame="true" _defaultFocus="Resume" />     <!-- 없으면 기본값 -->
	<SafeZonePanel>                                                <!-- 루트 위젯 정확히 하나 -->
		<CanvasPanel>
			<ButtonWidget _name="Resume" _command="Resume">          <!-- 원소 = 위젯 타입, 속성 = PROPERTY -->
				<_slot _offsetMin="100,100" _offsetMax="300,160" />  <!-- 구조체 칸 = 자식 원소 -->
				<TextWidget _text="Resume" />                        <!-- 위젯 타입인 자식 원소 = 자식 위젯 -->
			</ButtonWidget>
			<UserWidget _name="Hint" _document="engine/ui/parts/inputhint.ui.xml" />
		</CanvasPanel>
	</SafeZonePanel>
</UiDocument>
```

- **읽기**(`UiDocumentLoader::parse` — 파일마다 한 번, `UiDocumentCache` 가 경로로 든다): 위젯 원소 이름은 리플렉션 타입(`Widget` 파생), 속성 · 구조체 자식 원소는
  그 타입의 PROPERTY 입니다(씬 파일과 같은 `XmlSerializer`). 모르는 원소 · 속성 · 열거자 · 읽지 못한 값, 위젯 타입이 아닌 원소, 패널이 아닌 위젯의 자식은
  **로드 오류**입니다 — 문구는 `<경로>:<줄>: <이유>`. 옛 형식 리더는 없습니다(`_schemaVersion` 1).
- **바인딩 식**: `{` 로 시작하는 속성 값(`_text="{bind:_health}"`, 구조체 칸 안도)은 값으로 읽지 않고 `UiBindingDesc`(위젯 번호 · 프로퍼티 경로 `_slot._widthOverride` ·
  식 원문 · 줄)로 뗍니다. 화면이 들고(`UiScreen::getBindings`) 바인딩 단계가 겁니다(아래 "데이터 바인딩").
- **짓기**(`UiDocumentLoader::instantiate` — 화면을 열 때마다): 위젯은 리플렉션 기본 생성자(`$ctor`)로 짓습니다 — 위젯 타입은 `Widget` 하나만 상속하는 사슬이고
  `getTypeInfo()` 를 덮어써야 합니다(아니면 짓기 오류).
- **조각**(`UserWidget`): `_document` 문서의 루트 위젯을 자식으로 끼우고, 조각 안의 이름을 `"<조각 위젯 이름>.<안쪽 이름>"` 으로 감쌉니다 — 같은 조각을 두 번 써도
  `A.Label` · `B.Label` 로 갈립니다(조각 안의 조각은 `C.Inner.Label`). 조각이 돌고 돌아 자기를 다시 부르면 짓기 오류입니다. `UserWidget` 원소에 자식 위젯을 적으면 로드 오류.
- **명령**: 버튼(`ButtonWidget` — 누르고 같은 버튼 위에서 떼기, 또는 포커스 + `UI.Accept`)의 `_command` 는 화면의 `onCommand( 이름, 위젯 )` 으로 갑니다. 기본은
  `registerCommand` 로 건 함수, C++ 화면 클래스는 덮어씁니다(`UiSystem::openScreen<내 화면>( 경로 )`). 아무도 처리하지 않은 명령은 경고 한 줄.
- **열기**: `UiSystem::openScreen( 경로 )` — 문서의 `UiScreenDesc` 로 화면을 지어 올립니다. 실패하면 무효 핸들과 오류 로그.
- **쓰기**(`UiDocumentWriter::write` — 에디터 미리보기의 저장): 읽고 다시 쓰면 같은 문서입니다. 기본값과 같은 칸은 쓰지 않고(위젯 타입의 기본값 인스턴스와 글로 견준다),
  조각은 원소만, 바인딩 식은 식 그대로 씁니다. 속성 순서는 리플렉션 순서(기반 타입 먼저) — 손으로 쓰는 문서도 그 순서로 적습니다.
- 데이터 검사: `ResourceDataSchemaTest` 가 저장소의 모든 `*.ui.xml` 을 읽어 위젯 트리까지 짓습니다. 글 수집은 엔진 현지화 프로젝트의 `assetRoots` 에 `engine/ui`
  (`TextWidget::_text` 가 `Meta = "Localizable"`, 바인딩 식은 수집하지 않는다).
- 캐시는 `UiSystem` 이 소유하고 기동 단계 `Ui` 가 에셋 캐시 등록부(종류 `UiDocument`)에 올립니다.

## 스타일 · 테마 (`Style/` · `*.uistyle.xml`)

| 이 엔진 | 언리얼 | 유니티 UI Toolkit | Godot |
|---|---|---|---|
| `*.uistyle.xml` · `UiStyleSheetLoader` · 선택자 · 특정도 | Slate 스타일 세트(코드) · UMG 스타일 에셋 | USS(선택자 · 특정도 · 변수 · 의사 클래스) | `Theme`(타입 · 변형) |
| `WidgetStyle` · `UiComputedStyle` · `UiStyleSet` · `UiStylePass` | `FSlateWidgetStyle` | 계산된 스타일(`resolvedStyle`) | 테마 덮어쓰기 |
| `UiThemeCatalog` · `UiSystem::setTheme` | 스타일 세트 바꾸기 | 테마 스타일 시트(TSS) | `Control.theme` |

```xml
<UiStyleSheet _schemaVersion="1">
	<Variable _name="accent" _value="0.25,0.5,1,1" />                          <!-- 이 시트의 $변수(적는 자리는 어디든) -->
	<Rule _selector="ButtonWidget.primary:hover" _backgroundColor="$accent" />  <!-- 속성 이름 = WidgetStyle PROPERTY -->
	<Rule _selector="BorderPanel.window TextWidget" _textColor="0.92,0.93,0.95,1" />
	<Rule _selector="TextWidget.title" _fontSize="32">
		<_font _weight="Bold" />                                                <!-- 구조체 칸은 자식 원소 — 적은 안쪽 칸만 -->
	</Rule>
</UiStyleSheet>
```

- **칸**(`WidgetStyle`): 배경색 · 모서리 · 테두리 두께/색 · 그림자 색/밀림/흐림 · 여백 · 글꼴 · 글자 크기 · 글 색 · 외곽선 색/두께 · 포커스 테두리 색 · 불투명도 · 전환(7-2).
  칸 번호(`UiStyleField`)와 칸 표(`UiStyleFieldTable` — 레이아웃에 닿는가 · 자손 그림에 구워지는가 · 물려받는가)가 PROPERTY 순서와 짝입니다(`UiStyleTest.FieldTableMatchesReflection`).
- **선택자**: `타입? .클래스* #이름? :상태*` 를 빈 칸(자손 결합자 — 아무 조상)으로 잇습니다. 타입은 **정확한 타입**만 맞습니다(파생 아님 — USS 와 같다).
  상태는 `hover` · `pressed` · `focus` · `focus-visible`(포커스 + 탐색 입력 방식) · `disabled`(자기나 조상이 꺼짐) · `checked` · `selected` — 위젯이
  `Widget::computeStyleStates` 로 답합니다(버튼이 누름, 체크 상자가 켜짐을 더한다). `>` · `+` · `*` · `[속성]` 은 쓰지 않습니다(로드 오류).
- **특정도**: (#이름 수, .클래스 + :상태 수, 타입 수) 사전 순, 같으면 테마 시트 → 문서 시트 순서, 시트 안에서는 뒤가 이깁니다.
- **상속**: 글 칸(글꼴 · 크기 · 글 색 · 외곽선)은 부모가 정한 값을 물려받고 자기 규칙이 덮습니다. 나머지는 규칙이 정한 칸만.
- **위젯 칸과의 관계**: 계산된 스타일은 "정한 칸" 비트를 듭니다. 위젯은 **스타일이 정한 칸이면 그것, 아니면 자기 칸**(코드 세터 · 문서 속성 · 버튼의 상태 브러시 —
  시트 없는 화면의 기본 겉모습)을 씁니다. 겉모습을 데이터로 바꾸는 길은 시트이고, 위젯 칸은 그 기본값입니다.
- **나눠 쓰기**: 계산된 스타일 = f(맞은 규칙, 부모의 계산된 스타일) 이라 같은 조건의 위젯은 한 객체를 나눠 씁니다(버튼 100 개 → 1).
- **걷기**(`UiStylePass` — 레이아웃 앞, 구간 `GT.Ui.Style` · 카운터 `Ui.StyleWidgets`): `kStyle` 인 위젯(상태 · 클래스 · 이름 · 트리에 붙음)만 다시 맞춥니다.
  자손으로는 계산된 스타일이 바뀌었거나 그 위젯이 맞는 "조상 쪽 선택자 조각" 이 바뀌었을 때만 내려갑니다 — 호버 하나에 트리 전체가 돌지 않습니다.
  바뀐 칸이 여백 · 글꼴 · 크기면 `kLayout`, 불투명도면 `kTransform`(자손 그림까지), 그 밖은 `kPaint` 입니다.
- **테마**(`UiThemeCatalog` — `engine/ui/uithemes.xml`, 게임 프리셋 `_uiThemes` 가 덮어쓴다): 이름 → 시트들. 모든 화면에 테마 시트 → 문서(와 조각) 시트 순서로 겁니다.
  `UiSystem::setTheme( 이름 )` 은 모든 화면을 다시 맞춥니다. 엔진 기본 테마 `default` = `engine/ui/styles/default.uistyle.xml`.
- **사용자 설정 `accessibility.uiTheme`**(`gv_uiTheme` — `default` · `highcontrast`): 테마 목록을 걸 때 그 이름이 목록에 있으면 그 테마, 없으면 목록의 기본이고,
  실행 중에 바뀌면 다음 `update` 가 바꾼다. 게임 테마 목록이 설정을 받으려면 같은 이름(`default` · `highcontrast`)의 테마를 둔다.
- **고대비 테마** `highcontrast` = 기본 시트 + `engine/ui/styles/highcontrast.uistyle.xml`(색 · 테두리만 덮는다 — 기본 시트와 같은 선택자로 덮어 특정도가 같고 뒤 시트가 이긴다):
  불투명한 검정 바탕 · 흰 글(21:1) · 노랑 강조 · 상호작용 위젯 테두리 2(`UiStyleTest.HighContrastThemeLoads` 가 7:1 이상을 본다).
- **색각 안전 팔레트**: 두 시트 모두 경고 `warning`(주황) · 성공 `success`(파랑) 변수 — 빨강 · 초록을 짝으로 쓰지 않고, 색만이 아니라 모양으로도 다르다
  (`BorderPanel.warning` 두꺼운 테두리 · 각진 모서리, `BorderPanel.success` 얇은 테두리 · 둥근 모서리).
- **색각 보정**(`gv_colorVisionMode` — accessibility.colorVision): UI 는 톤맵 뒤에 그려 톤맵 쪽 보정이 닿지 않으므로 캔버스 셰이더가 직접 건다
  (`Graphics/Renderer` 의 Canvas 패스, `colorvision.hlsli` — Machado 2009 흉내 + 오차 재분배). 스타일 시트와 무관하다.
- 문서는 `<_listStyleSheet>` 로 자기 시트를 겁니다(조각 문서의 시트도 모인다). 시트는 `UiStyleSheetCache`(종류 `UiStyleSheet`)가 경로로 듭니다.

## 핫 리로드 (문서 · 스타일)

- 에디터의 파일 감시(`AssetHotReload` — `*.ui.xml` · `*.uistyle.xml` 은 "그 경로를 든 캐시" 길)가 캐시의 `reload` 를 부릅니다. 캐시는 **다시 읽어 파싱이 되면** 바꾸고
  `UiSystem` 에 알리고, 실패하면 **옛 것을 그대로 두고** 오류만 남깁니다(실패가 화면을 지우지 않는다 — 현지화 `reloadChangedFile` 과 같은 규칙).
- 문서가 바뀌면 그 문서(또는 조각 사슬로 그것을 쓰는 문서)로 연 화면마다 트리를 새로 짓고 바인딩 · 시트를 다시 겁니다. 포커스 위젯 · 덮였을 때의 포커스 ·
  스크롤 패널 오프셋은 **이름으로** 이어 갑니다(새 트리를 바로 한 번 맞추고 재 둔 뒤 — 오프셋은 내용 크기 안으로 묶인다). 화면 서술(층 · 모달)은 그대로입니다.
- 스타일 시트가 바뀌면 그 시트를 쓰는 화면의 묶음만 다시 걸고 위젯을 다시 맞춥니다(트리 · 위젯 번호 그대로).
- Shipping 은 파일 감시가 없을 뿐 길은 같습니다(캐시의 다시 읽기 함수 하나).

## 데이터 바인딩 (`Binding/`)

| 이 엔진 | 언리얼 | 유니티 UI Toolkit | Godot |
|---|---|---|---|
| `UiViewModel` + `notifyFieldChanged` · `setField` | UMG MVVM `UMVVMViewModelBase` + FieldNotify(`UE_MVVM_SET_PROPERTY_VALUE`) | 런타임 바인딩 데이터 소스 + 변경 추적 | 시그널 |
| `{bind:필드}` · `mode=TwoWay` | MVVM 바인딩(OneWay · TwoWay) | `DataBinding` `bindingMode` | 시그널 양쪽 연결 |
| `converter=` · `UiBindingConverterRegistry` | 변환 함수(Conversion Function) | `ConverterGroup` | (코드) |
| `{poll:필드}`(개발 편의) | UMG 옛 속성 바인딩(매 프레임 함수) | `updateTrigger = EveryUpdate` | `_process` 에서 읽기 |
| `{setting:id}` | Lyra `GameSettingRegistry` 의 설정 ↔ 위젯 | (직접) | `ProjectSettings` 를 코드로 |

게임 상태를 위젯에 넣는 길은 **뷰모델**입니다 — 게임 코드는 위젯을 찾지 않고 뷰모델 필드에 값을 넣어 알리고, 문서의 식이 그 필드를 위젯 칸에 잇습니다.

```xml
<SliderWidget _value="{bind:_volume, mode=TwoWay}" />              <!-- 사용자가 움직이면 뷰모델에 되쓴다 -->
<TextWidget _text="{bind:_ammo, format=Hud.Ammo}" />               <!-- 현지화 패턴 "Ammo: {value}" -->
<TextWidget _text="{bind:_health, converter=Percent}" />           <!-- 0.75 → "75%" -->
<ProgressBarWidget _percent="{bind:_health}" />
```

- **뷰모델**(`UiViewModel`): 필드는 리플렉션 PROPERTY 입니다. 파생은 `REFLECT()` 를 달고 `getTypeInfo()` 를 덮어씁니다(위젯과 같다 — 기반은 `UiViewModel` 하나만,
  칸 오프셋이 객체 시작 기준이다). 세터는 `setField( _health, value, "_health" )` — 같은 값이면 알리지 않습니다. 알림은 번호로 남아 한 프레임에 여러 번 알려도
  칸은 한 번 쓰고, 화면 여럿이 한 뷰모델을 볼 수 있습니다. 소유는 게임 — `UiScreen::setViewModel` 로 걸고, 뷰모델이 먼저 지워지면 바인딩이 놓습니다.
- **바인딩 단계**(`UiSystem::update` 의 레이아웃 앞, 구간 `GT.Ui.Bind`): 화면마다 `UiBindingSet` 이 걸리지 않았으면 식을 풀어 걸고 모든 칸을 쓰며, 걸려 있으면
  알린 필드에 묶인 칸만 리플렉션으로 씁니다. 쓴 뒤 위젯의 `onBoundPropertyChanged` 가 그 칸의 세터와 같은 무효화를 합니다(글 · 크기 칸은 레이아웃, 색 칸은 그리기만).
  칸 종류를 PROPERTY 메타로 적지 않습니다 — 커스텀 메타는 Shipping 에서 지워진다.
- **타입 검사는 걸 때**: 갈래(불리언 · 숫자 · 글 · 그 밖)가 맞지 않고 변환기도 없으면 오류(`<문서>:<줄>: binding '<식>' on <칸>: <이유>` — 로그 + `getErrors`)이고 그 식은
  걸리지 않습니다. 불리언 ↔ 숫자는 바꾸고, 무엇이든 글로는 바꾸며(리플렉션 글 표기), 글 → 숫자는 바꾸지 않습니다. 색 · 벡터 · 열거형은 같은 타입끼리만.
- **양방향**: 슬라이더 · 체크 · 콤보 · 글 입력 칸이 사용자 입력 길에서 `notifyValueEdited` 를 부르면 소스에 되쓰고 알립니다. 그 알림으로 같은 칸에 다시 쓰지 않습니다
  (같은 필드의 다른 바인딩은 받는다). 변환기 · 형식이 든 식은 양방향일 수 없습니다.
- **변환기**: 이름으로 찾습니다(`UiSystem::getBindingConverters`). 엔진 기본 `Percent` · `Invert` · `NotEmpty` · `IsZero` · `Seconds`(m:ss). 게임이 더한 변환기는
  그 모듈이 내려갈 때 걷히고 화면은 다시 겁니다.
- **폴링**(`{poll:필드}`): 알림 없이 매 프레임 값을 견줍니다 — 개발 편의입니다. 견준 수가 프로파일 카운터 `Ui.PollBindings` 로 보이고, Shipping 에서 걸면 경고 한 줄.

### 현지화 글

- 글 위젯의 `_text`(`Meta = "Localizable"`)는 **현지화 키 또는 글 그대로**입니다. 보이는 글은 측정 · 칠하기 때 문맥의 문화권으로 `getStringByText( 키, 키 )` —
  표에 없으면 글 그대로(문서의 영어 원문 `"Paused"` 도 그대로 보인다). 푼 글은 글 판(`getTextRevision`)과 함께 위젯이 캐시합니다(`getDisplayText`).
- 바인딩 단계가 프레임마다 글 판을 견줍니다(정수 하나). 바뀌면 글꼴 대체 사슬을 비우고(`FontSystem::invalidateFaceChains`) 모든 위젯에 `onTextRevisionChanged` —
  글 위젯은 다시 풀고 kLayout(글 길이가 바뀐다). 형식 바인딩(`format=`)도 같은 프레임에 다시 포맷합니다. 오른쪽에서 왼쪽 문화권이면 레이아웃이 거울로 놓습니다.
- 코드는 **키를 넣습니다**(`setText( "Menu.Start" )`). `SW_LOCTEXT` 로 미리 푼 글을 넣으면 언어를 바꿔도 그대로입니다. 플레이어가 친 글처럼 풀면 안 되는 글은
  `setLocalized( false )` — 글 입력 칸은 친 글을 풀지 않고 힌트만 풉니다.

### 사용자 설정

`{setting:설정 id}` 는 `UserSettingsManager`(옵션 메뉴 백엔드)를 소스로 씁니다 — 기본 양방향입니다(옵션 줄은 값을 바꾸는 것이 일이다).

```xml
<SliderWidget _value="{setting:audio.master}" />          <!-- 값 + 범위 · 눈금(_minValue · _maxValue · _step)은 설정 정의에서 -->
<ComboBoxWidget _selectedIndex="{setting:video.mode}" />  <!-- 선택지(_listOption) = 선택지의 글 키(없으면 값) -->
<CheckBoxWidget _bChecked="{setting:video.vsync}" />      <!-- enabledWhen 이 거짓이면 위젯을 끈다 -->
```

- 읽기: `getValue`(보류 값 우선). 칸이 `_value` 이고 위젯에 `_minValue` · `_maxValue` · `_step` 이 있으면 정의의 범위를, 칸이 `_selectedIndex` 이고 `_listOption` 이
  있으면 선택지를 값보다 먼저 씁니다(칸 이름이 같은 위젯이면 모두 — 슬라이더 · 콤보를 따로 알지 않는다). `isSettingEnabled` 는 위젯 사용 가능으로.
- 쓰기(사용자 입력): 설정 타입에 맞는 `setPending*Value`. 결과는 보지 않고 다음 바인딩 단계가 설정 값을 다시 읽습니다 — 눈금으로 고쳐 받았으면(Clamped) 고친 값이,
  거절됐으면(Rejected · Disabled) 원래 값이 보입니다. 키 바인딩 겹침(Conflict)은 옵션 메뉴의 키 바인딩 창(8-2) 몫입니다.
- 다른 곳의 변경(되돌리기 · 기본값 · 확인 카운트다운의 자동 되돌림)은 변경 통보(`registerEventListener`)로 받습니다 — 사용 가능이 다른 설정에 기대므로 통보가 오면
  그 화면의 설정 바인딩을 모두 다시 읽습니다. 리스너는 바인딩 집합(= 화면)이 지워질 때 뗍니다. 시험은 `UiSystem::setUserSettings` 로 자기 매니저를 넘깁니다.

## 애니메이션 · 트윈 (`Animation/`)

| 이 엔진 | 언리얼 | 유니티 UI Toolkit | Godot |
|---|---|---|---|
| `UiAnimation`(트랙 · 키 · 사건) · `UiAnimationPlayer` | UMG 위젯 애니메이션(시퀀서 트랙) | (애니메이션 클립 · 코드) | `AnimationPlayer` |
| `UiSystem::tween` · `UiAnimationPlayer::tween` | (블루프린트 타임라인) | `experimental.animation` | `Tween` |

**움직이기는 렌더 변환 · 불투명도 · 색으로** — 이 칸들은 `kTransform` · `kPaint` 라 레이아웃이 돌지 않습니다. 크기 · 여백(`_slot._widthOverride` …)을 움직이면
매 프레임 그 위젯부터 레이아웃 경계까지 다시 잽니다(`UiAnimationTest.TransformTrackDoesNotRelayout` · `SizeTrackRelayouts`).

```xml
<UiDocument _schemaVersion="1">
	<_listAnimation>
		<UiAnimation _name="Open">                                          <!-- Open · Close 는 화면 스택이 튼다 -->
			<_listTrack>
				<UiAnimationTrack _widget="Window" _property="_opacity">   <!-- 위젯 이름(조각 안이면 조각.이름) · 프로퍼티 경로 -->
					<_listKey>
						<UiAnimationKey _time="0" _value="0" _curve="Linear" />
						<UiAnimationKey _time="0.2" _value="1" _curve="EaseOut" /> <!-- 곡선은 이 키까지 가는 것(Animation/BlendCurve) -->
					</_listKey>
				</UiAnimationTrack>
			</_listTrack>
			<_listEvent><UiAnimationEvent _time="0.2" _command="Opened" /></_listEvent>  <!-- 화면 onCommand 로 -->
		</UiAnimation>
	</_listAnimation>
	…
</UiDocument>
```

- **값**: 키의 `_value` 는 그 칸의 글 표기입니다(XML 속성과 같다). 실수 · `float2` · `float3` · `float4` 칸은 키 사이를 곡선(`BlendCurve` — 카메라 · 시퀀서와 같은 곡선)으로
  보간하고, 그 밖(정수 · 불리언 · 열거형 · 글)은 계단입니다(`_visibility` 를 `Collapsed` → `Visible`). 경로는 틀 때 한 번 풀고, 쓰기는 리플렉션으로 한 뒤
  `Widget::onBoundPropertyChanged`(바인딩과 같은 자리)가 그 칸의 세터와 같은 무효화를 겁니다. 못 푼 트랙(위젯 · 경로 · 값)은 경고하고 그 트랙만 뺍니다.
- **재생기**(`UiAnimationPlayer` — 화면마다 하나, `UiScreen::getAnimationPlayer`): `play( 이름, 배속, 바퀴 수(0 = 끝없이) )` · `playReverse` · `reverse`(재생 중이면 그 자리에서
  돌아선다) · `stop` · `isPlaying`. 틀 때 첫 프레임 값을 바로 씁니다(올린 화면이 한 프레임 끝 값으로 번쩍이지 않게). 사건은 재생이 그 시각을 지날 때 한 바퀴에 한 번,
  진행이 끝난 뒤 보냅니다(명령 처리가 애니메이션을 틀거나 화면을 닫아도 된다).
- **열기 · 닫기**: 이름 `Open` 은 `pushScreen` 이, `Close` 는 `closeScreen` 이 틉니다. 닫기는 `Close` 가 끝난 뒤 실제로 지우고, 그동안 화면은 그려지기만 합니다
  (입력 · 포커스 · 활성에서 빠진다).
- **트윈**: `UiSystem::tween( 위젯 번호, 경로, 끝값, 길이, 곡선 )` — 지금 값에서 끝값으로(코드 한 줄, Godot `Tween`). 같은 위젯 · 경로의 새 트윈은 옛 것을 대신합니다.
- **시간**: `UiSystem::update` 의 애니메이션 단계(구간 `GT.Ui.Animate` — 스타일 · 레이아웃 앞)에서 실제 프레임 시간으로 진행합니다(게임 정지 메뉴도 움직인다).
- **움직임 줄이기**: 사용자 설정 `accessibility.reduceMotion`(`gv_uiReduceMotion`)이면 애니메이션 · 트윈 · 닫기가 바로 끝 값입니다(사건은 모두 보낸다).

### 스타일 전환 (`Animation/UiStyleTransition`)

유니티 USS · CSS 의 `transition` 자리입니다. 시트 규칙의 `_transition` 칸에 적습니다.

```xml
<Rule _selector="ButtonWidget" _transition="_backgroundColor 0.12 EaseOut, _opacity 0.2 Linear" />   <!-- 칸 길이(초) 곡선(없으면 EaseOut) -->
<Rule _selector="ButtonWidget:hover" _backgroundColor="$accentHover" />
```

- 계산된 스타일이 바뀔 때(상태 · 클래스 · 테마 — 스타일 걷기가 그 프레임에 **한 번** 계산) 새 스타일의 `_transition` 이 적은 칸만 **지금 보이는 값**에서 새 값으로
  옮겨 갑니다. 진행 중에 다시 바뀌면 그 자리에서 새 목표로(처음 값으로 튀지 않는다). 그 뒤 프레임마다는 값만 보간합니다(애니메이션 단계 — 칸 종류대로 `kPaint` ·
  `kLayout` · `kTransform`). `all` 은 보간되는 모든 칸, 뒤 항목이 앞을 덮습니다.
- 보간되는 칸 = 실수 칸(색 · 모서리 · 두께 · 그림자 · 여백 · 글자 크기 · 외곽선 · 불투명도). 글꼴 · `_transition` 자신 · 모르는 칸 · 읽지 못한 길이 · 모르는 곡선은 시트
  로드 오류입니다.
- 바로 바뀌는 것: 적지 않은 칸, 처음 맞추는 위젯, 옛 · 새 스타일 중 한쪽만 정한 칸(위젯 자기 칸 ↔ 시트 값), `gv_uiReduceMotion`.
- 보이는 값은 위젯이 든 복사본(`UiStyleTransitionState`)이고 `Widget::getComputedStyle` 이 그것을 돌려줍니다 — 나눠 쓰는 계산된 스타일은 바꾸지 않습니다. 끝나면 복사본을 지웁니다.
- 물려받는 글 칸은 부모의 **목표** 값을 물려받습니다(보간 값을 자손에 내리지 않는다 — 나눠 쓰기 열쇠가 프레임마다 바뀌지 않게). 자식 글이 함께 움직이려면 자식 규칙에도
  `_transition` 을 적습니다(`ButtonWidget TextWidget` — 자식의 물려받은 값이 바뀐 것도 자식에게는 바뀐 칸이다).

## 자막 (`Screen/UiSubtitleService`)

`UiSystem::getSubtitles().post( 화자, 글, 길이 )` — 언리얼 `FSubtitleManager` 의 자리입니다. 글은 현지화한 것을 받습니다(대화 러너 · 음성 이벤트가 푼다).

- **보이는 시간**: 길이 0 이면 읽기 시간 = max( 2 초, 글자(코드 포인트) 수 × 0.06 초 ). 줄은 **둘까지** 동시에 보이고(오래된 것이 위), 넘치는 줄은 대기열에서
  기다렸다가 자리가 나면 그때부터 시간을 센다.
- **화면**: 오버레이 층(포커스 · 포인터 없음) 문서 `engine/ui/subtitles.ui.xml` — 화면 아래 가운데, 줄 자리는 이름 `Line<n>`(바탕) · `Speaker<n>` · `Text<n>`.
  줄이 있을 때만 열고 없으면 닫는다. 화자 이름 색은 시트 `engine/ui/styles/subtitles.uistyle.xml` 의 변수 `speaker`.
- **사용자 설정**(매 프레임 읽는다): `gv_subtitles` 를 끄면 화면을 닫지만 줄은 계속 받고 시간도 흐른다(켜면 지금 줄부터), `gv_subtitleSize` 0 · 1 · 2 는 문서에 적힌
  글 크기에 0.85 · 1 · 1.3 을 곱하고(글자 배율 · 하한은 그 위에), `gv_subtitleBackgroundOpacity` 는 줄 바탕의 알파다.
- **보내는 쪽**: GF `DialogueRunnerComponent::_bPostSubtitles`(기본 끔 — 대화 UI 가 글을 따로 보이면 켜지 않는다).
