# 런타임 UI 툴팁과 커서 모양

> 상태: 구현중 · 선행: 없음 · 영역: ui · 옛번호: 없음 · 진행: 3/4

호버 경로 추적과 `:hover` 스타일은 있었지만, 머물면 설명을 띄우는 툴팁과 호버한 위젯에 따라 커서 모양을 바꾸는 길이 없었다. 입력 쪽 커서는 보이기 · 숨기기와 가상 커서뿐이었다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| T1 | 위젯이 선언한다 — `Widget::_tooltip`(글) · `_cursorShape`(`UICursorShape`, `Inherit` 는 부모를 따름, 루트까지 `Inherit` 면 화살표) PROPERTY | 끝남(빌드 미확인) |
| T2 | `UITooltipService`(`UISystem::getTooltips`) — 호버 경로에서 주인 · 모양을 고르고(`findTooltipOwner` · `resolveCursorShape`), `gv_uiTooltipDelay` 초 머물면 오버레이 문서(`EngineDefaultAssets::_uiTooltip` → `engine/ui/tooltip.ui.xml`, 위젯 `Body` · `Text`)를 포인터 옆에 띄운다. 테스트 `UITooltipTest.HoverPathPicksTheNearestCursorAndTooltip` | 끝남(빌드 미확인) |
| T3 | OS 커서 — `CursorShape`(`Engine/Input/CursorShape.h`) · `InputManager::setCursorShape`, Win32 창 클래스 커서 + `SetCursor`, X11 글꼴 커서(`XDefineCursor`), 헤드리스 빈 구현. 숨긴 동안은 적용을 미루고 다시 보일 때 적용 | 끝남(빌드 미확인) |
| T4 | 빌드 · 실기 확인(툴팁 위치 · 깜빡임, 에디터 게임 뷰에서 ImGui 커서와의 겹침), 월드 위젯 호버는 [WorldWidgetInput](WorldWidgetInput.md) 에 들어갔다(입력을 켠 World 위젯의 호버 · 커서는 그 위젯의 `UIPointerState` 가 갖는다 — 툴팁 서비스는 화면 트리의 호버만 본다) | 남음 |

## 남은 것

- T4. 에디터에서는 ImGui 가 커서를 쥐므로 게임 뷰 밖에서 덮인다(창 클래스 커서를 바꾸므로 ImGui 백엔드가 WM_SETCURSOR 를 처리하지 않으면 에디터 패널에도 보일 수 있다 — 실기로 본다).
- 툴팁 글의 현지화는 문서의 키 바인딩이 `_tooltip` 칸에 쓰는 길이다(따로 키 칸을 두지 않았다).
