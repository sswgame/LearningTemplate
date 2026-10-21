# 런타임 UI 입력의 남은 것

> 상태: 구현중 · 선행: 없음 · 영역: ui · 옛번호: D6-2 · 진행: 2/3

런타임 UI 입력 중 둘이 남아 있었다. 에디터 게임 뷰(Game 패널) 안의 포인터 좌표와 리눅스의 운영체제 클립보드다. 둘 다 코드가 들어갔고(I1 · I2, 빌드 미확인) 빌드 · 실기 확인이 남았다.
단위가 끝나면 그 절을 지운다.

## 1. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| I3 | 빌드 · 테스트와 실기 확인 | 없음 |

### I3 빌드 · 테스트와 실기 확인

코드로 할 수 있는 몫(테스트 · 떼어 낸 패널 · INCR)은 들어갔다. 남은 것은 짓고 돌려 보는 일이다.

- 빌드: `EditorAPI` 표의 모양이 바뀌어 `kModuleAbiVersion` 이 8 이다 — App · EditorModule · 게임 모듈을 함께 다시 짓는다. 리눅스 클라이언트 빌드(`X11Window.cpp` · `X11Clipboard.cpp`)는 이 PC 에서 짓지 못했다.
- 테스트: `EngineTest --test_filter=UIInputTest.PointerPositionSubtractsViewportOrigin:UIInputTest.PointerPositionIsInUIUnits`.
- 게임 뷰 포인터(Windows 에디터): `-EnableEditor` 로 메뉴가 있는 게임(Shooter3D 일시정지 메뉴)을 Play 하고 게임 뷰 단추를 눌러 반응하는지 본다. 도크 가운데 · 레터박스(종횡비 고정) · 패널을 떼어 다른 OS 창에 둔 경우 셋을 본다.
- 리눅스 클립보드: 글 입력 칸에서 복사한 글을 다른 프로그램(터미널 · 편집기)에 붙여넣고, 반대로도 한다. 한글이 깨지지 않는지, 수 MB 글(INCR)을 붙여넣을 수 있는지 본다.

## 2. 끝난 단위의 확인 목록

- I1 게임 뷰 포인터: 그린 프레임마다 패널이 이미지 자리를 알린다(`EditorRenderViews::markViewDrawn` 이 자리를 인자로 받는다 — 자리 없이 그렸다고 알릴 수 없다).
  자리는 `HostViewTarget::_windowX` · `_windowY` 로 호스트에 가고(`EditorAPI::getGameViewport` 가 `HostViewTarget` 하나를 채운다), `Engine::update` 가 `UIViewport::_pointerOrigin` 에 넣어 `UIInputRouter::processPointer` 가 창 픽셀에서 뺀다. 게임 창(백버퍼)은 빈 출력이라 0 이다.
  패널을 다른 OS 창(ImGui 보조 뷰포트)으로 떼면 그 창의 마우스 메시지는 ImGui 만 받는다. 그래서 `GameViewPanel::forwardDetachedPointer` 가 ImGui 의 마우스(이동 · 단추 · 휠)를 주 창 클라이언트 좌표로 옮겨 게임 입력에 넣는다.
  입력 장치의 좌표 약속("주 창 클라이언트 픽셀")과 이미지 자리의 기준이 같아 원점 계산은 그대로다. 패널 안에서 누른 단추는 밖에서 떼도 뗌을 넣는다. 키보드는 넘기지 않는다(떼어 낸 창이 포커스면 키는 ImGui 만 받는다).
- I2 리눅스 클립보드: 선택 소유 · `SelectionRequest`(TARGETS · UTF8_STRING) · `SelectionClear` 는 `X11Window` 가 맡고, 주인인지는 표시 없이 서버에 묻는다(`XGetSelectionOwner`). 읽기는 `XConvertSelection` 뒤 `SelectionNotify` 만 골라 최대 200 ms 기다린다.
  큰 글은 INCR(ICCCM 2.7.2)로 받는다 — 창이 PropertyChangeMask 를 늘 받고, 조각마다 PropertyNotify(새 값)를 기다려 읽고 지우며, 길이 0 조각이 끝이다. 내줄 때는 X 요청 한 번(`XExtendedMaxRequestSize`)보다 큰 글을 거절하고 경고한다(보내는 쪽 INCR 은 없다). Wayland 는 옛번호 `D1-6` 이다.
