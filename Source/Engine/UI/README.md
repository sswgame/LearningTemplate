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
- `update` — 게임 틱 뒤, 렌더 패킷 앞. 애니메이션 → 바인딩 → 스타일 → 레이아웃 → 그리기.

## 히트 테스트 · 사건 경로 (`Core/UiEventRouter` · `Core/UiPointerState`)

- **히트 테스트**는 그리기 역순(뒤에 붙인 자식이 위)으로 내려가며 점이 든 가장 깊은 위젯 경로(뿌리 → 잎)를 찾습니다. 보임 다섯을 따르고, 렌더 변환은
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
- 자르는 패널 밖으로 완전히 나간 위젯은 후보가 아니다(스크롤이 "보이게 하기" 를 맡는다 — 레이아웃 단계).

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
