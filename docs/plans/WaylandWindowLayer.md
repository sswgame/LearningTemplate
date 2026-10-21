# Wayland 창 계층

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: D1-6 · 진행: 1/3

`Engine/Window/Linux` 가 X11 뿐이었다. 컴포지터 없이 지을 수 있는 백엔드 코드는 들어갔고(W1), 확인은 CI 리눅스 잡의 headless 컴포지터에서만 한다(결정 기록 5-12 — 로컬 WSL 빌드는 하지 않는다).

## 1. 설계

- **고르기.** 창 계층은 환경 변수 하나가 고른다 — `SW_WINDOW_BACKEND` = `x11`(없을 때도) | `wayland`. 모르는 값 · 짓지 않은 계층은 오류로 멈춘다(`IWindow::createPlatformWindow`). XWayland 가 있는 데스크톱의 기본은 지금까지의 X11 이다.
- **짓기.** `SW_ENABLE_WAYLAND`(기본 끔, `cmake/Config/BuildOptions.cmake`)를 켜면 리눅스 클라이언트가 wayland-client · wayland-cursor · xkbcommon 을 pkg-config 로 찾고, wayland-protocols 의 `xdg-shell.xml` 을 wayland-scanner 로 C 코드로 만들어(`sw_wayland_protocols`, `cmake/Modules/Platform/Linux.cmake`) `SW_WITH_WAYLAND` 를 정의한다. 하나라도 없으면 구성이 선다.
- **표면의 종류는 타입에 있다.** `IRenderSurface::getSurfaceKind`(`NativeSurfaceKind` — Win32 · X11 · Wayland)가 핸들의 뜻을 말한다. 스왑체인 서술(`RHISwapChainDesc::_surfaceKind`)이 그것을 싣고, Vulkan 은 Wayland 면 `VK_KHR_wayland_surface` 만 받아 `vkCreateWaylandSurfaceKHR` 로 표면을 만든다(X11 확장으로 대신하지 않는다). GL 은 GLX 라 X11 이 아닌 표면을 오류로 거절한다(EGL 길이 없다).
- **창.** `WaylandWindow`(`Engine/Window/Linux/Wayland`)가 xdg-shell 최상위 창을 만들고 첫 configure 를 기다린다. 크기는 컴포지터가 준 값(0 이면 클라이언트), 닫기는 `xdg_toplevel.close` → `tryBeginClose`, 전체 화면은 `xdg_toplevel_set_fullscreen`, 최소 크기 · 제목도 xdg-shell 이다. 이벤트는 막지 않고 읽는다(prepare_read · poll 0 · read_events). 창 자리 · 숨기기는 Wayland 클라이언트가 할 수 없어 자리 함수는 비고 숨기기는 오류다. 화면 배율은 1(wl_output 배율을 읽지 않는다).
- **입력.** 리스너가 키보드(xkbcommon keymap · keysym · UTF-8) · 포인터(움직임 · 버튼 · 축 · 들어옴/나감) · 포커스를 `WaylandInputEvent` 하나로 모아 `NativeWindowEvent::kMessageWayland` 로 처리기에 넘긴다. 입력 관리자(`InputManagerX11.cpp`)는 활성 창의 종류를 보고 Wayland 갈래를 탄다 — keysym 은 X11 KeySym 과 같은 번호라 키 표를 함께 쓴다. 커서 숨기기는 `wl_pointer_set_cursor`(보이기는 커서 테마의 `left_ptr`). 포인터 잠금은 pointer-constraints 확장이 없어 오류 한 번으로 알리고 잠그지 않는다.

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| W1 | 위 설계의 코드 — 빌드 옵션 · 프로토콜 생성 · `WaylandWindow` · 입력 갈래 · 표면 종류 · Vulkan WSI · GL 거절 | 코드 완료(빌드 안 함 — Windows 빌드에서는 빈 몸체만 컴파일된다) |
| W2 | CI 리눅스 잡에서 확인 — 아래 목록 | 남음 |
| W3 | 남은 기능 — 포인터 잠금(zwp_pointer_constraints_v1 · zwp_relative_pointer_v1), 화면 배율(wp_fractional_scale_v1 · wl_output.scale), 클립보드(wl_data_device — `UiInputRemaining`), 에디터 ImGui 플랫폼 백엔드(지금 `ImGuiX11PlatformBackend` 뿐), 스플래시 · 개발 콘솔 창(X11 뿐), EGL 로 GL | 남음 |

## 3. CI 에서 확인할 목록(W2)

1. 패키지: `.github/actions/setup-build-env` 에 `libwayland-dev libxkbcommon-dev wayland-protocols weston` 을 더하고, vcpkg `vulkan-loader` 가 `wayland` 기능으로 지어졌는지 본다.
2. 구성 · 빌드: `-DSW_ENABLE_WAYLAND=ON` 으로 `WSL-Debug`(또는 CI 리눅스 프리셋) 구성 — `[Linux] Wayland window layer enabled` 줄, `xdg-shell-client-protocol.h` 생성, Engine 링크(`ldd libEngine.so | grep wayland`).
3. 기동: `weston --backend=headless-backend.so --socket=wayland-ci &` 뒤 `WAYLAND_DISPLAY=wayland-ci SW_WINDOW_BACKEND=wayland ./App -gv_rhiBackend=Vulkan -gv_profileFrames=120` — `Window backend: Wayland` · `Native Wayland Window created` · `Vulkan WSI: VK_KHR_wayland_surface` 줄과 종료 코드 0.
4. 거절: 같은 실행을 `-gv_rhiBackend=OpenGL` 로 — `OpenGL on Linux needs an X11 window` 오류로 기동 실패(조용히 X11 로 가지 않는다). `SW_WINDOW_BACKEND=foo` 는 `is not a window backend` 오류.
5. 입력: weston 의 가상 입력이 없으면 자동화 시나리오의 가상 입력(`AutomationVirtualInput`)으로 대신한다 — 키보드 · 포인터 리스너 자체는 headless 에서 seat 가 없을 수 있어(`wl_seat` 없음) 리스너 없이도 창이 서는지가 확인 대상이다.
6. 기본 길이 그대로인지: `SW_WINDOW_BACKEND` 없이 X11 잡이 전과 같이 통과한다.

## 남은 것

- W2 를 돌리고 실패를 고친다. 리스너 구조체를 값 초기화한 뒤 이름으로 채웠다 — 설치된 wayland 헤더의 판이 바인드한 판(자리 5 · xdg_wm_base 1)보다 새로우면 남는 칸은 비어 있고 그 이벤트는 오지 않는다.
