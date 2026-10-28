# Wayland 창 계층

> 상태: 구현중 · 선행: 없음 · 영역: engine · 진행: 1/2

`Engine/Window/Linux` 가 X11 뿐이었다. Wayland(리눅스의 현대 창 시스템) 백엔드를 컴포지터 없이 지을 수 있는 코드까지 모두 넣었다. 리눅스 빌드는 이 PC 에서 하지 않아 **한 번도 컴파일되지 않았고**, 남은 구현은 EGL 로 GL 을 여는 일(RHI 영역)뿐이다.

## 진행

| 단계 | 내용 | 상태 |
|---|---|---|
| W1 | 빌드 옵션 · 프로토콜 생성 · `WaylandWindow` · 입력 갈래 · 표면 종류 · Vulkan WSI · GL 거절 | 코드 끝, 컴파일 확인 전 |
| W3 | 클립보드 · 화면 배율 · 포인터 잠금 · 커서 모양, 에디터 ImGui 백엔드 · 스플래시 · 개발 콘솔 | 코드 끝, 컴파일 확인 전. EGL 로 GL 은 남음 |

첫 일: 리눅스에서 컴파일한다. 실패하면 생성 헤더 이름(`<XML 이름>-client-protocol.h`, 커서 모양은 `tablet-unstable-v2` 도 링크돼야 한다)과 바인드 판 상수부터 본다.

## 1. 설계

- **고르기.** 환경 변수 `SW_WINDOW_BACKEND` = `x11`(없을 때도) | `wayland`. 모르는 값 · 짓지 않은 계층은 오류로 멈춘다.
- **짓기.** `SW_ENABLE_WAYLAND`(기본 끔)를 켜면 wayland-client · wayland-cursor · xkbcommon 을 pkg-config 로 찾고, wayland-protocols 의 XML 을 wayland-scanner 로 C 코드로 만들어(`sw_wayland_protocols`, 1.32 이상) `SW_WITH_WAYLAND` 를 정의한다. 하나라도 없으면 구성이 선다. 서버 빌드에 Wayland 코드가 새지 않게 하는 정적 게이트는 `CheckX11Isolation` 4번 규칙이다.
- **표면의 종류는 타입에 있다.** `IRenderSurface::getSurfaceKind`(Win32 · X11 · Wayland)가 핸들의 뜻을 말하고 스왑체인 서술이 싣는다.
  - Vulkan 은 Wayland 면 `VK_KHR_wayland_surface` 만 받는다(X11 확장으로 대신하지 않는다). GL 은 GLX 라 X11 이 아닌 표면을 오류로 거절한다(EGL 길이 없다).
  - 핸들은 종류 확인과 값 확인이 한 함수인 `tryGet*`(`[[nodiscard]]`, 다른 종류면 nullptr)로만 얻는다. 핸들만 꺼내는 함수는 보호 구역이라 `wl_display*` 를 `Display*` 로 읽는 호출을 바깥에서 쓸 수 없다. RHI · 에디터 ImGui 뷰포트도 같은 모양이다.
  - X11 이 아닌 창이 오면: X11 개발 콘솔은 만들지 않고(Wayland 는 서브표면 판), 에디터 ImGui X11 백엔드는 오류로 멈추며(Wayland 는 팩토리가 `ImGuiWaylandPlatformBackend` 를 고른다), 클립보드는 지원 안 함, 커서 모양은 한 번 알리고 건너뛴다.
- **창.** `WaylandWindow` 가 xdg-shell 최상위 창을 만들고 첫 configure 를 기다린다. 크기는 컴포지터가 준 값(0 이면 클라이언트), 닫기는 `xdg_toplevel.close`. 이벤트는 막지 않고 읽는다. 창 자리 · 숨기기는 Wayland 클라이언트가 할 수 없어 자리 함수는 비고 숨기기는 오류다.
  - 화면 배율은 `wp_fractional_scale_v1` 이 있으면 그 값, 없으면(정상) 창이 걸친 `wl_output` 의 정수 배율 최댓값이다. 버퍼 배율은 바꾸지 않고 컴포지터가 확대한다.
  - 선택 확장 넷(소수 배율 · 커서 모양 · pointer-constraints · relative-pointer)은 판 1 로 바인드하고 기동 로그 한 줄(`Wayland extensions: …`)에 유무를 남긴다. 판이 오르면 바인드 상수와 리스너를 함께 올려야 한다(컴파일러가 알려 주지 않는다). 리스너 구조체는 값 초기화한 뒤 이름으로 채워, 헤더 판이 더 새로우면 남는 칸의 이벤트는 오지 않는다.
  - 클립보드는 `wl_data_device` 선택이다. 쓰기는 마지막 키 · 버튼 · 들어옴의 일련번호가 필요해 입력 없이 복사를 시키는 자동화 가상 입력은 경고와 false 를 돌려받는다(시나리오가 그 경고를 처리해야 한다). 읽기는 `text/plain;charset=utf-8` 을 파이프로 받는다. 끌어다 놓기는 받지 않는다.
- **입력.** 리스너가 키보드(xkbcommon) · 포인터 · 포커스를 `WaylandInputEvent` 하나로 모아 넘긴다. 입력 관리자는 창 종류를 가르지 않는다. X11 서버 호출은 `tryGetX11Surface` 가 true 일 때만 하고, 창이 스스로 하는 동작은 `IWindow` 의 가상 질의 · 동작(`supportsCursorShape`/`setCursorShape` · `supportsPointerLock`/`setPointerLock` · `setCursorVisible`, 기본 지원 안 함)으로 가른다. 청했는데 안 되면 오류를 한 번 남긴다(조용히 다른 길로 가지 않는다).
  - 커서 모양은 `wp_cursor_shape_manager_v1` 의 `set_shape`(포인터가 들어올 때마다 다시 정함). 확장이 없으면 기본 화살표 외 요청에 오류 한 번.
  - 포인터 잠금: `ConfinedToWindow` 는 `zwp_confined_pointer_v1`, `LockedInCenter` 는 `zwp_locked_pointer_v1` 이고 잠긴 동안 `zwp_relative_pointer_v1` 의 가속 없는 이동량이 `PointerRelative` 로 간다. 확장이나 포인터 장치가 없으면 잠그지 않고 오류 한 번.
- **에디터 · 스플래시 · 콘솔.** `ImGuiWaylandPlatformBackend`(키 · 포인터 · 휠 · 포커스 · 클립보드 · 커서 모양, 멀티 뷰포트는 꺼서 패널은 메인 창 안에서만 도킹), `WaylandSplashWindow`(자기 연결 · xdg 창 · 공유 메모리 버퍼), `WaylandDevConsoleWindow`(게임 창의 `wl_subsurface` 에 줄을 그림). 글은 `WaylandTextPainter`(엔진 SDF 글리프 + `SystemFontLocator`)로 그리고 공유 메모리 버퍼 묶음 `WaylandShmBufferPool` 을 둘이 쓴다.

## 2. 남은 것과 한계

- **EGL 로 GL**(RHI 영역): 에디터의 GL 렌더러는 EGL 이 없는 한 Wayland 에서 못 쓴다(Vulkan 만).
- **컴파일에서 볼 곳:** `WaylandShmBuffer.cpp`(`memfd_create` 는 glibc 2.27 이상), `WaylandSplashWindow.cpp` · `WaylandDevConsoleWindow.cpp`(리스너 값 초기화, `wl_subcompositor` 판 1), 헤더에 선언만 둔 `struct wl_*`.
- **확인 시설 없음:** 클립보드 왕복(weston headless 에 `wl-copy`/`wl-paste`)과 포인터 잠금 왕복(가상 포인터)은 시나리오로 만들지 못했다.
- 글꼴 후보 이름 목록이 `WaylandTextPainter.cpp` 에 있다(`editortooldefaults.json` 과 같은 이름 — 에디터 설정을 엔진이 못 읽어 따로 둔다, 바뀌면 함께 고친다). 라틴 고정폭 글꼴이 없는 기계에서는 콘솔을 세우지 않고 스플래시는 글 없이 그린다.
- 스플래시 창 자리는 컴포지터가 정한다. Wayland 에디터는 패널을 OS 창으로 떼어 낼 수 없다(xdg-shell 이 창 자리를 못 정한다).
- `X11Window::getContentScale` 은 `Xft.dpi` 를 연결을 연 때의 값으로 읽는다. 실행 중 배율 변경을 따르려면 루트 창 `RESOURCE_MANAGER` 변경 알림을 받아 캐시해야 하지만 지금은 하지 않는다.
