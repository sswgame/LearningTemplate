# Wayland 창 계층

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: D1-6 · 진행: 1/3

**남은 확인(통합에서 확인):** 매니페스트 id `wayland-ci-job`(W2, push 뒤 CI). main CI 실행 `40160506131` 이후 실행(`40180268417`)에서 잡 `Build Linux Debug Wayland (Clang)` 가 success 로 보고돼 W3 의 코어 프로토콜 부분(클립보드 · 배율)에 착수했다 — 그 잡의 다음 실행이 이 코드를 처음 컴파일한다.

`Engine/Window/Linux` 가 X11 뿐이었다. 컴포지터 없이 지을 수 있는 백엔드 코드는 들어갔고(W1), 확인은 CI 리눅스 잡의 headless 컴포지터에서만 한다(결정 기록 5-12 — 로컬 WSL 빌드는 하지 않는다).

## 1. 설계

- **고르기.** 창 계층은 환경 변수 하나가 고른다 — `SW_WINDOW_BACKEND` = `x11`(없을 때도) | `wayland`. 모르는 값 · 짓지 않은 계층은 오류로 멈춘다(`IWindow::createPlatformWindow`). XWayland 가 있는 데스크톱의 기본은 지금까지의 X11 이다.
- **짓기.** `SW_ENABLE_WAYLAND`(기본 끔, `cmake/Config/BuildOptions.cmake`)를 켜면 리눅스 클라이언트가 wayland-client · wayland-cursor · xkbcommon 을 pkg-config 로 찾고, wayland-protocols 의 `xdg-shell.xml` 을 wayland-scanner 로 C 코드로 만들어(`sw_wayland_protocols`, `cmake/Modules/Platform/Linux.cmake`) `SW_WITH_WAYLAND` 를 정의한다. 하나라도 없으면 구성이 선다.
- **표면의 종류는 타입에 있다.** `IRenderSurface::getSurfaceKind`(`NativeSurfaceKind` — Win32 · X11 · Wayland)가 핸들의 뜻을 말한다. 스왑체인 서술(`RHISwapChainDesc::_surfaceKind`)이 그것을 싣고, Vulkan 은 Wayland 면 `VK_KHR_wayland_surface` 만 받아 `vkCreateWaylandSurfaceKHR` 로 표면을 만든다(X11 확장으로 대신하지 않는다). GL 은 GLX 라 X11 이 아닌 표면을 오류로 거절한다(EGL 길이 없다). X11 호출에 쓸 서버 연결과 창 ID 는 `IWindow::tryGetX11Surface` 로만 얻는다 — 종류 확인과 값 확인이 한 함수에 있고 연결만 꺼내는 `getNativeDisplay` 는 보호 구역이라, Wayland 의 `wl_display*` 를 `Display*` 로 읽는 호출을 바깥에서 쓸 수 없다(`X11Window*` 가 필요하면 `X11Window::tryCast`, `WaylandWindow*` 는 `WaylandWindow::tryCast`). Win32 `HWND` 도 같은 모양이다 — `IWindow::tryGetWin32Handle` 이 종류 확인과 값을 한 함수로 주고(`[[nodiscard]]`, X11 · Wayland 창이면 nullptr) 핸들만 꺼내는 `getNativeHandle` 은 보호 구역이고 `Win32Window` · `X11Window` · `WaylandWindow` 의 override 도 보호 구역에 있어 파생 클래스로 다시 열 수 없다. RHI 는 `getSurfaceHandle` 을 종류와 함께 읽는다 — 스왑체인 서술(`RHISwapChainDesc::tryGetWin32Handle`)이 종류 확인을 한 함수로 주고, DX11 · DX12 · WGL · Windows Vulkan 은 Win32 표면이 아니면 `HWND` 로 읽지 않고 오류로 멈춘다. 에디터 ImGui 뷰포트의 `PlatformHandle` · `PlatformHandleRaw` 도 플랫폼 백엔드마다 뜻이 다른 `void*` 라서, 렌더러와 에디터 자체 테스트는 `IImGuiPlatformBackend::tryGetViewportX11Surface` · `tryGetViewportWin32Handle`(활성 플랫폼 백엔드가 그 종류인지와 값을 함께 확인)로만 `Display*` · 창 ID · `HWND` 를 얻고, 아니면 Vulkan 은 표면 생성을, OpenGL 은 그 뷰포트의 GL 자원 만들기를 오류로 거절한다(OpenGL 렌더러는 GL 디바이스에 `Display*` 가 없으면 `glXGetCurrentDisplay()` 로 대신하지 않고 초기화를 실패시킨다). 이 길에 X11 이 아닌 창이 오면 개발 콘솔은 만들지 않고, 에디터 ImGui X11 백엔드는 오류로 멈추고, 클립보드는 `isAvailable` 이 false 이며, 커서 모양은 한 번 알리고 건너뛴다.
- **창.** `WaylandWindow`(`Engine/Window/Linux/Wayland`)가 xdg-shell 최상위 창을 만들고 첫 configure 를 기다린다. 크기는 컴포지터가 준 값(0 이면 클라이언트), 닫기는 `xdg_toplevel.close` → `tryBeginClose`, 전체 화면은 `xdg_toplevel_set_fullscreen`, 최소 크기 · 제목도 xdg-shell 이다. 이벤트는 막지 않고 읽는다(prepare_read · poll 0 · read_events). 창 자리 · 숨기기는 Wayland 클라이언트가 할 수 없어 자리 함수는 비고 숨기기는 오류다. 화면 배율은 컴포지터가 `wp_fractional_scale_v1.preferred_scale`(1/120 단위)을 알렸으면 그 값이고, 그 프로토콜이 없는 컴포지터는 정상이라 창이 걸친 `wl_output` 의 정수 배율 중 최댓값이 그 길이다(`wl_surface.enter/leave` 로 걸친 출력을 알고 버퍼 배율은 바꾸지 않는다 — 알려 주기만 하고 컴포지터가 확대한다). 선택 확장 셋(소수 배율 · pointer-constraints · relative-pointer)은 판 1 로 바인드하고 기동 로그 한 줄(`Wayland extensions: …`)에 있는지 없는지를 남긴다. 판이 오르면 바인드 상수와 리스너를 함께 올려야 한다(컴파일러가 알려 주지 않는다). 클립보드는 `wl_data_device` 선택이다 — 쓰기는 마지막 키 · 버튼 · 들어옴의 일련번호로 `wl_data_source` 를 내놓고(포인터 움직임 이벤트에는 일련번호가 없다. 글 입력 칸의 복사는 Ctrl+C 나 버튼으로 시작하므로 그 일련번호가 있고, 입력 없이 복사를 시키는 자동화 가상 입력만 일련번호가 없어 `PlatformClipboard::writeText` 가 경고를 남기고 쓰지 않는다 — 시나리오가 그 경고를 처리해야 한다. `wl_data_source.send` 는 막지 않는 쓰기로 200 ms 안에 못 쓰면 포기), 읽기는 선택 제안의 `text/plain;charset=utf-8`(없으면 `text/plain`)을 파이프로 받는다. 끌어다 놓기는 받지 않는다.
- **입력.** 리스너가 키보드(xkbcommon keymap · keysym · UTF-8) · 포인터(움직임 · 버튼 · 축 · 들어옴/나감) · 포커스를 `WaylandInputEvent` 하나로 모아 `NativeWindowEvent::kMessageWayland` 로 처리기에 넘긴다. 입력 관리자(`InputManagerX11.cpp`)는 활성 창의 종류를 보고 Wayland 갈래를 탄다 — keysym 은 X11 KeySym 과 같은 번호라 키 표를 함께 쓴다. 커서 숨기기는 `wl_pointer_set_cursor`(보이기는 커서 테마의 `left_ptr`). 포인터 잠금은 `InputManager::applyMouseLockMode` 가 `WaylandWindow::setPointerLock` 으로 청한다 — `ConfinedToWindow` 는 `zwp_confined_pointer_v1`, `LockedInCenter` 는 `zwp_locked_pointer_v1` 이고(둘 다 영속 수명이라 포커스가 돌아오면 컴포지터가 다시 켠다) 잠긴 동안 `zwp_relative_pointer_v1` 의 가속 없는 이동량이 `PointerRelative` → `makeMouseRawDelta` 로 간다(잠긴 포인터는 위치가 서 있어 `PointerMove` 가 오지 않고, 가운데로 되돌리는 일도 없다). 컴포지터에 확장이 없거나 포인터 장치가 없으면 요청이 false 를 돌려주고 입력 관리자가 오류를 한 번 남기며 잠그지 않는다(조용히 다른 길로 가지 않는다 — 게임이 청한 잠금이 안 걸리는 것은 정상 경로가 아니다).

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| W1 | 위 설계의 코드 — 빌드 옵션 · 프로토콜 생성 · `WaylandWindow` · 입력 갈래 · 표면 종류 · Vulkan WSI · GL 거절 | 코드 완료(빌드 안 함 — Windows 빌드에서는 빈 몸체만 컴파일된다) |
| W2 | CI 리눅스 잡에서 확인 — 아래 목록. CI 쪽은 넣었다(2026-10-22): 매트릭스 잡 `Build Linux Debug Wayland (Clang)`(`configureArgs: -DSW_ENABLE_WAYLAND=ON`, `reportOnly`) · 단계 `Wayland window layer smoke (Linux)`(weston headless 위 Vulkan 기동 · GL 거절 · 모르는 값 거절) · `setup-build-env` 의 패키지 · vcpkg `vulkan-loader[wayland]` | CI 첫 실행을 기다림 |
| W3 | 남은 기능 — 코드 완료(CI 가 아직 컴파일하지 않았다): 클립보드(`wl_data_device` 선택 — `WaylandWindow::readClipboardText` · `writeClipboardText`, `PlatformClipboard` 가 창 종류별 `tryCast` 로 잇는다), 화면 배율(소수 배율 `wp_fractional_scale_v1` 이 있으면 그 값, 없으면 `wl_output.scale` 의 정수 배율 중 걸친 출력의 최댓값 — 버퍼 배율은 바꾸지 않는다), 포인터 잠금 · 가둠(`zwp_pointer_constraints_v1` · `zwp_relative_pointer_v1` — `setPointerLock`, 입력 관리자가 잇는다). 세 프로토콜 XML 은 `sw_wayland_protocols` 의 목록(`cmake/Modules/Platform/Linux.cmake`, wayland-protocols 1.31 이상)에 더했다. 남음: 에디터 ImGui 플랫폼 백엔드(지금 `ImGuiX11PlatformBackend` 뿐 — 에디터 영역), 스플래시 · 개발 콘솔 창(X11 뿐), EGL 로 GL(RHI 영역). 클립보드 왕복(쓴 글을 다른 클라이언트가 읽는지) 시나리오는 weston headless 에 `wl-copy`/`wl-paste` 를 깔아야 만들 수 있다 | 일부 완료 |

## 3. CI 에서 확인할 목록(W2)

1. 패키지: `.github/actions/setup-build-env` 가 `libwayland-dev libxkbcommon-dev wayland-protocols` 를 깔고(weston 은 스모크 단계가 깐다), vcpkg `vulkan-loader` 는 `wayland` 기능으로 짓는다 — 첫 실행에서 구성 로그로 확인한다.
2. 구성 · 빌드: `-DSW_ENABLE_WAYLAND=ON` 으로 `WSL-Debug`(또는 CI 리눅스 프리셋) 구성 — `[Linux] Wayland window layer enabled` 줄, `xdg-shell-client-protocol.h` 생성, Engine 링크(`ldd libEngine.so | grep wayland`).
3. 기동: `weston --backend=headless-backend.so --socket=wayland-ci &` 뒤 `WAYLAND_DISPLAY=wayland-ci SW_WINDOW_BACKEND=wayland ./App -gv_rhiBackend=Vulkan -gv_profileFrames=120` — `Window backend: Wayland` · `Native Wayland Window created` · `Vulkan WSI: VK_KHR_wayland_surface` 줄과 종료 코드 0.
4. 거절: 같은 실행을 `-gv_rhiBackend=OpenGL` 로 — `OpenGL on Linux needs an X11 window` 오류로 기동 실패(조용히 X11 로 가지 않는다). `SW_WINDOW_BACKEND=foo` 는 `is not a window backend` 오류.
5. 입력: weston 의 가상 입력이 없으면 자동화 시나리오의 가상 입력(`AutomationVirtualInput`)으로 대신한다 — 키보드 · 포인터 리스너 자체는 headless 에서 seat 가 없을 수 있어(`wl_seat` 없음) 리스너 없이도 창이 서는지가 확인 대상이다.
6. 기본 길이 그대로인지: `SW_WINDOW_BACKEND` 없이 X11 잡이 전과 같이 통과한다.

## 남은 것

- 포인터 잠금 · 소수 배율 코드(W3)도 W2 잡의 같은 첫 컴파일이 처음 본다 — 실패하면 생성 헤더 이름(`<XML 이름>-client-protocol.h`)과 판 상수부터 본다. 잠금 왕복(잠그면 `PointerMove` 가 멎고 `PointerRelative` 가 오는지)은 weston headless 에 가상 포인터가 있어야 볼 수 있어 시나리오로 만들지 못했다.
- `X11Window::getContentScale` 은 `Xft.dpi` 를 연결을 연 때의 값으로 읽는다(`X11Window.h` 설명). 실행 중 배율 변경을 따르려면 루트 창의 `RESOURCE_MANAGER` 속성 변경 알림을 받아 캐시해야 한다 — 지금은 하지 않는다.

- W2 의 CI 잡 첫 실행을 보고 실패를 고친다(`py -3 -m Scripts ci-jobs --summary`). 초록이 이어지면 잡의 `reportOnly` 를 지우고 `configureArgs` 를 `Build Linux Debug (Clang)` 로 옮겨 잡을 하나로 합친다(ci.yml 주석). 로그 줄 `Vulkan WSI: VK_KHR_wayland_surface` 는 TRACE 라 스모크가 보지 않는다 — 창 생성 줄과 종료 코드 0 으로 갈음한다. 리스너 구조체를 값 초기화한 뒤 이름으로 채웠다 — 설치된 wayland 헤더의 판이 바인드한 판(자리 5 · xdg_wm_base 1)보다 새로우면 남는 칸은 비어 있고 그 이벤트는 오지 않는다.
