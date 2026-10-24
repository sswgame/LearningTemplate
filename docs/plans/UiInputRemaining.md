# 런타임 UI 입력의 남은 것

> 상태: 측정대기 · 선행: 없음 · 영역: ui · 옛번호: D6-2 · 진행: 3/3

**남은 확인(통합에서 확인):** 매니페스트 id `ui-input-real-device`, 매니페스트 id `panel-id-literals-p3`(`Test/QA/VerifyBatch.json`). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

런타임 UI 입력 중 둘이 남아 있었다. 에디터 게임 뷰(Game 패널) 안의 포인터 좌표와 리눅스의 운영체제 클립보드다. 코드(I1 · I2)와 Windows 빌드 · 단위 테스트는 끝났고, I3 에서 사람 손으로 보려던 확인을 테스트로 바꿨다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| I3 | 실기 확인을 테스트로: (1) 에디터 게임 뷰의 포인터 — 이미지 자리가 패널 창 안에 있고 런타임 UI 의 포인터 원점 · 입력의 게임 화면 원점에 같이 닿는지 에디터 자체 테스트 `gameView.pointerOriginReachesTheGame`, 원점 · 배율을 뺀 포인터가 화면 위젯 · 월드 단추를 맞히는지 `UIInputTest.PointerPositionSubtractsViewportOrigin` · `MouseClickOnWorldWidgetThroughCameraRay`. (2) 리눅스 클립보드 — 두 X11 창(각자 디스플레이 연결)이 한글 · 일본어 글과 X 요청 한 번보다 큰 글(INCR)을 두 방향으로 주고받는 `WindowTest.X11ClipboardRoundTripsBetweenWindowsIncludingIncr`(CI 리눅스 xvfb 의 swrender 묶음). 리눅스 빌드는 CI 가 짓는다([작업 방식](../11_Workflow.md) 8절). 떼어 낸 창의 키보드는 [Candidates-ui](Candidates-ui.md) `DetachedGameViewKeyboard` | 끝남(빌드 미확인) |

## 2. 끝난 단위의 확인 목록

- I1 게임 뷰 포인터: 그린 프레임마다 패널이 이미지 자리를 알린다(`EditorRenderViews::markViewDrawn` 이 자리를 인자로 받는다 — 자리 없이 그렸다고 알릴 수 없다).
  자리는 `HostViewTarget::_windowX` · `_windowY` 로 호스트에 가고(`EditorAPI::getGameViewport` 가 `HostViewTarget` 하나를 채운다), `Engine::update` 가 `UIViewport::_pointerOrigin` 에 넣어 `UIInputRouter::processPointer` 가 창 픽셀에서 뺀다. 게임 창(백버퍼)은 빈 출력이라 0 이다.
  패널을 다른 OS 창(ImGui 보조 뷰포트)으로 떼면 그 창의 마우스 메시지는 ImGui 만 받는다. 그래서 `GameViewPanel::forwardDetachedPointer` 가 ImGui 의 마우스(이동 · 단추 · 휠)를 주 창 클라이언트 좌표로 옮겨 게임 입력에 넣는다.
  입력 장치의 좌표 약속("주 창 클라이언트 픽셀")과 이미지 자리의 기준이 같아 원점 계산은 그대로다. 패널 안에서 누른 단추는 밖에서 떼도 뗌을 넣는다.
  키보드도 같다 — 떼어 낸 창이 포커스면 키는 ImGui 만 받으므로 `GameViewPanel::forwardDetachedKeyboard` 가 `ImGuiKeyForward`(ImGui 키 ↔ 엔진 키 표 한 곳, 입력 맵 패널의 키 바인딩 잡기도 쓴다)로 그 프레임의 눌림 · 반복 · 뗌과 글자를 게임 입력 사건으로 바꿔 넣는다. 넣은 누름의 뗌은 포커스를 잃어도 넣는다. IME 조합 중인 글은 넘기지 않는다(조합이 확정된 글만 간다).
- I2 리눅스 클립보드: 선택 소유 · `SelectionRequest`(TARGETS · UTF8_STRING) · `SelectionClear` 는 `X11Window` 가 맡고, 주인인지는 표시 없이 서버에 묻는다(`XGetSelectionOwner`). 읽기는 `XConvertSelection` 뒤 `SelectionNotify` 만 골라 최대 200 ms 기다린다.
  큰 글은 INCR(ICCCM 2.7.2)로 받는다 — 창이 PropertyChangeMask 를 늘 받고, 조각마다 PropertyNotify(새 값)를 기다려 읽고 지우며, 길이 0 조각이 끝이다. 내줄 때도 X 요청 한 번(`XExtendedMaxRequestSize`)보다 큰 글은 INCR 로 보낸다:
  요청에 INCR 표지(예상 길이)를 적고 상대 창의 PropertyChangeMask 를 받아 두었다가(`beginClipboardIncr`), 상대가 속성을 지울 때마다(`PropertyNotify` 삭제) 다음 조각(요청 한계의 절반)을 적고 길이 0 조각으로 끝낸다. 건마다 글 사본을 쥐어 보내는 도중에 새로 복사해도 처음 글이 끝까지 가고,
  상대가 10 초 동안 다음 조각을 청하지 않으면 건을 버린다(`expireClipboardIncr`). Wayland 는 옛번호 `D1-6` 이다.
