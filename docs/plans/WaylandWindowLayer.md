# Wayland 창 계층

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: D1-6 · 진행: 1/3

**남은 확인(통합에서 확인):** 매니페스트 id `wayland-ci-job`(W2, push 뒤 CI). main CI 실행 `40180268417` 에서 잡 `Build Linux Debug Wayland (Clang)` 가 success 로 보고돼 W3 의 코어 프로토콜 부분(클립보드 · 배율)에 착수했다 — 그 잡의 다음 실행이 이 코드를 처음 컴파일한다.

`Engine/Window/Linux` 가 X11 뿐이었다. 컴포지터 없이 지을 수 있는 백엔드 코드는 들어갔고(W1) 확인은 CI 리눅스 잡의 headless 컴포지터에서만 한다(결정 기록 5-12 — 로컬 WSL 빌드는 하지 않는다).

## 1. 설계

- **고르기.** 환경 변수 `SW_WINDOW_BACKEND` = `x11`(없을 때도) | `wayland`. 모르는 값 · 짓지 않은 계층은 오류로 멈춘다(`IWindow::createPlatformWindow`).
- **짓기.** `SW_ENABLE_WAYLAND`(기본 끔, `cmake/Config/BuildOptions.cmake`)를 켜면 리눅스 클라이언트가 wayland-client · wayland-cursor · xkbcommon 을 pkg-config 로 찾고 wayland-protocols 의 `xdg-shell.xml` 을 wayland-scanner 로 C 코드로 만들어(`sw_wayland_protocols`, `cmake/Modules/Platform/Linux.cmake`) `SW_WITH_WAYLAND` 를 정의한다. 하나라도 없으면 구성이 선다.
- **표면의 종류는 타입에 있다.** `IRenderSurface::getSurfaceKind`(`NativeSurfaceKind` — Win32 · X11 · Wayland)가 핸들의 뜻을 말하고 스왑체인 서술(`RHISwapChainDesc::_surfaceKind`)이 싣는다. Vulkan 은 Wayland 면 `VK_KHR_wayland_surface` 만 받는다(X11 확장으로 대신하지 않는다). GL 은 GLX 라 X11 이 아닌 표면을 오류로 거절한다(EGL 길이 없다). 핸들은 종류 확인과 값 확인이 한 함수인 `tryGet*` 로만 얻는다(`IWindow::tryGetX11Surface` · `tryGetWin32Handle`, `[[nodiscard]]`, 다른 종류면 nullptr; `X11Window::tryCast` · `WaylandWindow::tryCast`) — 핸들만 꺼내는 `getNativeDisplay` · `getNativeHandle` 과 파생 클래스의 override 는 보호 구역이라 Wayland 의 `wl_display*` 를 `Display*` 로 읽는 호출을 바깥에서 쓸 수 없다. RHI 도 같은 모양이다(`RHISwapChainDesc::tryGetWin32Handle` — DX11 · DX12 · WGL · Windows Vulkan 은 Win32 표면이 아니면 오류로 멈춘다). 에디터 ImGui 뷰포트의 `PlatformHandle` 도 `IImGuiPlatformBackend::tryGetViewportX11Surface` · `tryGetViewportWin32Handle` 로만 읽고, 아니면 Vulkan 은 표면 생성을, OpenGL 은 그 뷰포트의 GL 자원 만들기를 오류로 거절한다(`glXGetCurrentDisplay()` 로 대신하지 않는다). X11 이 아닌 창이 오면 개발 콘솔은 만들지 않고, 에디터 ImGui X11 백엔드는 오류로 멈추고, 클립보드는 `isAvailable` 이 false 이며, 커서 모양은 한 번 알리고 건너뛴다.
- **창.** `WaylandWindow`(`Engine/Window/Linux/Wayland`)가 xdg-shell 최상위 창을 만들고 첫 configure 를 기다린다. 크기는 컴포지터가 준 값(0 이면 클라이언트), 닫기는 `xdg_toplevel.close` → `tryBeginClose`, 전체 화면 · 최소 크기 · 제목도 xdg-shell. 이벤트는 막지 않고 읽는다(prepare_read · poll 0 · read_events). 창 자리 · 숨기기는 Wayland 클라이언트가 할 수 없어 자리 함수는 비고 숨기기는 오류다. 화면 배율은 창이 걸친 `wl_output` 의 정수 배율 최댓값(버퍼 배율은 바꾸지 않는다). 클립보드는 `wl_data_device` 선택 — 쓰기는 마지막 키 · 버튼 입력의 일련번호로 `wl_data_source` 를 내놓고(`send` 는 막지 않는 쓰기로 200 ms 안에 못 쓰면 포기), 읽기는 선택 제안의 `text/plain;charset=utf-8`(없으면 `text/plain`)을 파이프로 받는다. 끌어다 놓기는 받지 않는다.
- **입력.** 리스너가 키보드(xkbcommon) · 포인터 · 포커스를 `WaylandInputEvent` 하나로 모아 `NativeWindowEvent::kMessageWayland` 로 넘기고 입력 관리자(`InputManagerX11.cpp`)가 활성 창 종류를 보고 Wayland 갈래를 탄다(keysym 은 X11 KeySym 과 같은 번호라 키 표를 함께 쓴다). 커서 숨기기는 `wl_pointer_set_cursor`. 포인터 잠금은 pointer-constraints 확장이 없어 오류 한 번으로 알리고 잠그지 않는다.

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| W1 | 위 설계의 코드 — 빌드 옵션 · 프로토콜 생성 · `WaylandWindow` · 입력 갈래 · 표면 종류 · Vulkan WSI · GL 거절 | 코드 완료(빌드 안 함) |
| W2 | CI 리눅스 잡에서 확인(3절). CI 쪽은 넣었다: 매트릭스 잡 `Build Linux Debug Wayland (Clang)`(`configureArgs: -DSW_ENABLE_WAYLAND=ON`, `reportOnly`) · 단계 `Wayland window layer smoke (Linux)`(weston headless 위 Vulkan 기동 · GL 거절 · 모르는 값 거절) · `setup-build-env` 패키지 · vcpkg `vulkan-loader[wayland]` | CI 첫 실행을 기다림 |
| W3 | 코어 프로토콜만으로 되는 클립보드(`WaylandWindow::readClipboardText` · `writeClipboardText`)와 화면 배율은 코드 완료(CI 미컴파일). 남음: 소수 배율(wp_fractional_scale_v1)과 포인터 잠금(zwp_pointer_constraints_v1 · zwp_relative_pointer_v1)은 `sw_wayland_protocols` XML 목록에 프로토콜을 더해야 해 CMake 소유와 겹쳐 미뤘다, 에디터 ImGui 플랫폼 백엔드(지금 `ImGuiX11PlatformBackend` 뿐), 스플래시 · 개발 콘솔 창(X11 뿐), EGL 로 GL(RHI 영역). 클립보드 왕복 시나리오는 weston headless 에 `wl-copy`/`wl-paste` 가 있어야 만들 수 있다 | 일부 완료 |

## 3. CI 에서 확인할 목록(W2)

1. 패키지: `.github/actions/setup-build-env` 가 `libwayland-dev libxkbcommon-dev wayland-protocols` 를 깔고(weston 은 스모크 단계가 깐다) vcpkg `vulkan-loader` 는 `wayland` 기능으로 짓는다.
2. 구성 · 빌드: `-DSW_ENABLE_WAYLAND=ON` — `[Linux] Wayland window layer enabled` 줄, `xdg-shell-client-protocol.h` 생성, Engine 링크(`ldd libEngine.so | grep wayland`).
3. 기동: `weston --backend=headless-backend.so --socket=wayland-ci &` 뒤 `WAYLAND_DISPLAY=wayland-ci SW_WINDOW_BACKEND=wayland ./App -gv_rhiBackend=Vulkan -gv_profileFrames=120` — `Window backend: Wayland` · `Native Wayland Window created` 줄과 종료 코드 0(`Vulkan WSI: VK_KHR_wayland_surface` 는 TRACE 라 스모크가 보지 않는다).
4. 거절: `-gv_rhiBackend=OpenGL` 은 `OpenGL on Linux needs an X11 window` 오류로 기동 실패(조용히 X11 로 가지 않는다), `SW_WINDOW_BACKEND=foo` 는 `is not a window backend` 오류.
5. 입력: headless 에서 `wl_seat` 가 없을 수 있어 리스너 없이도 창이 서는지가 확인 대상이다(가상 입력은 `AutomationVirtualInput`).
6. 기본 길이 그대로인지: `SW_WINDOW_BACKEND` 없이 X11 잡이 전과 같이 통과한다.

## 남은 것

- W1 코드와 CI 단계는 모두 들어가 있고 남은 W2 는 push 뒤 CI 첫 실행, W3 은 W2 초록이 조건이다. 서버 빌드에 Wayland 코드가 새지 않게 하는 정적 게이트(`CheckX11Isolation` 4번 규칙)는 더했다.
- W2 의 CI 잡 첫 실행을 보고 실패를 고친다(`py -3 -m Scripts ci-jobs --summary`). 초록이 이어지면 잡의 `reportOnly` 를 지우고 `configureArgs` 를 `Build Linux Debug (Clang)` 로 옮겨 잡을 하나로 합친다(ci.yml 주석). 리스너 구조체는 값 초기화한 뒤 이름으로 채웠다(설치된 wayland 헤더 판이 바인드한 판보다 새로우면 남는 칸은 비어 그 이벤트는 오지 않는다).
