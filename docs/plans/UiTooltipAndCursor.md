# 런타임 UI 툴팁과 커서 모양

> 상태: 측정대기 · 선행: 없음 · 영역: ui · 옛번호: 없음 · 진행: 4/4

**남은 확인(통합에서 확인):** 매니페스트 id `ui-input-real-device`(`Test/QA/VerifyBatch.json`). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

호버 경로 추적과 `:hover` 스타일은 있었지만, 머물면 설명을 띄우는 툴팁과 호버한 위젯에 따라 커서 모양을 바꾸는 길이 없었다. 입력 쪽 커서는 보이기 · 숨기기와 가상 커서뿐이었다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| T1 | 위젯이 선언한다 — `Widget::_tooltip`(글) · `_cursorShape`(`UICursorShape`, `Inherit` 는 부모를 따름, 루트까지 `Inherit` 면 화살표) PROPERTY | 끝남(빌드 미확인) |
| T2 | `UITooltipService`(`UISystem::getTooltips`) — 호버 경로에서 주인 · 모양을 고르고(`findTooltipOwner` · `resolveCursorShape`), `gv_uiTooltipDelay` 초 머물면 오버레이 문서(`EngineDefaultAssets::_uiTooltip` → `engine/ui/tooltip.ui.xml`, 위젯 `Body` · `Text`)를 포인터 옆에 띄운다. 테스트 `UITooltipTest.HoverPathPicksTheNearestCursorAndTooltip` | 끝남(빌드 미확인) |
| T3 | OS 커서 — `CursorShape`(`Engine/Input/CursorShape.h`) · `InputManager::setCursorShape`, Win32 창 클래스 커서 + `SetCursor`, X11 글꼴 커서(`XDefineCursor`), 헤드리스 빈 구현. 숨긴 동안은 적용을 미루고 다시 보일 때 적용 | 끝남(빌드 미확인) |
| T4 | 툴팁 위치 · 깜빡임 · 에디터 커서 — (1) 자리를 `UITooltipService::computePlacement` 가 정한다: 포인터 오른쪽 아래가 기본, 화면 오른쪽 · 아래를 넘으면 포인터 왼쪽 · 위로 뒤집고 그래도 밖이면 화면 안으로 민다. 크기는 지난 배치의 것이라 처음 띄운 프레임은 `Hidden` 으로 두어 변에서 번쩍이지 않는다. (2) 에디터에서는 ImGui 가 OS 커서를 쥐어(`ImGui_ImplWin32` 가 `WM_SETCURSOR` 를 처리한다) 엔진이 창 클래스에 건 커서가 덮이므로, `GameViewPanel` 이 게임 뷰 이미지 위 호버에서 `InputManager::getCursorShape` 를 `ImGui::SetMouseCursor` 로 낸다. 테스트 `UITooltipTest.PlacementFlipsAtScreenEdgesAndClamps` · `UIInputTest.TooltipShowsAfterDelayStaysPutAndClosesOffTheWidget`. 월드 위젯 호버는 [WorldWidgetInput](WorldWidgetInput.md) 에 들어갔다(입력을 켠 World 위젯의 호버 · 커서는 그 위젯의 `UIPointerState` 가 갖는다 — 툴팁 서비스는 화면 트리의 호버만 본다) | 끝남(빌드 미확인) |

## 남은 것

- 실행: 빌드 뒤 `EngineTest --test_filter=UITooltipTest.*:UIInputTest.*`. 에디터 게임 뷰에서 손 모양이 나오는지는 OS 커서 그림이 프레임버퍼에 찍히지 않아 화면 캡처로 못 본다 — `GameViewPanel` 이 ImGui 커서를 정하는 한 줄(`toImGuiCursor` 표)이 정본이라 코드 읽기로 갈음한다.
- 툴팁 글의 현지화는 문서의 키 바인딩이 `_tooltip` 칸에 쓰는 길이다(따로 키 칸을 두지 않았다).
