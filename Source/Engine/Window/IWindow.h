/**
 * @file IWindow.h
 * @brief 플랫폼 독립적인 창(OS 디스플레이 창) 생성과 메시지 처리 인터페이스입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/DelegateSlot.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Window/CursorShape.h"
#include "Engine/Window/IRenderSurface.h"
#include "Engine/Window/WindowTextInput.h"

namespace sw
{
    struct NativeWindowEvent;
    struct WindowMonitor;
    struct WindowMonitorRect;

    class CommandLineManager;

    /**
     * @brief 표면 · 앱 수명 이벤트입니다. 데스크톱은 오지 않고, 모바일 · 웹의 플랫폼 창이 냅니다(`IWindow::notifyLifecycleEvent`).
     * @details Android 는 `onPause` 에서 프로세스가 산 채 표면이 사라지고 브라우저도 탭을 숨기면 프레임이 멈춘다. 언리얼 `FAppEventManager`(기억) 자리다.
     */
    enum class WindowLifecycleEvent : uint8
    {
        SurfaceLost = 0, ///< 네이티브 표면이 사라진다. 이 통보가 돌아오면 더는 그 표면에 그리면 안 된다
        SurfaceRestored, ///< 네이티브 표면이 다시 생겼다
        AppSuspended,    ///< 앱이 배경으로 갔다
        AppResumed,      ///< 앱이 앞으로 왔다
    };

    SW_DECLARE_DELEGATE( void, WindowLifecycleDelegate, WindowLifecycleEvent event );
    SW_DECLARE_DELEGATE( bool, WindowMessageHandlerDelegate, const NativeWindowEvent& event );
    SW_DECLARE_DELEGATE( void, WindowResizeDelegate, uint32 width, uint32 height );
    SW_DECLARE_DELEGATE( bool, WindowCloseQueryDelegate, void );
    SW_DECLARE_DELEGATE( bool, WindowAccessibilityQueryDelegate, uintptr_t wParam, intptr_t lParam, intptr_t& outResult );

    /**
     * @brief 창을 화면에 놓는 방식입니다.
     * @details 전용 전체 화면(디스플레이 모드 변경)은 두지 않는다. 플립 모델과 티어링 스왑체인을 함께 쓰면 Present 규칙이 달라지고 Alt+Tab 이
     *          모드를 되돌리므로, 전체 화면은 모니터를 덮는 테두리 없는 창 하나로 처리한다(Unreal `WindowedFullscreen`, Unity `FullScreenWindow`).
     */
    enum class WindowDisplayMode : uint8
    {
        Windowed = 0,         ///< 테두리와 제목 줄이 있는 창입니다. 클라이언트 크기를 그대로 씁니다.
        BorderlessFullscreen, ///< 창이 놓인 모니터를 덮는 테두리 없는 창. 크기는 모니터 크기입니다.
    };

    /**
     * @brief 포인터를 창에 묶는 방식입니다(`IWindow::setPointerLock`).
     * @details `Released` 는 `None` 이라 쓰지 않는다 — 입력 관리자가 X11 헤더(`None` 매크로) 뒤에 이 헤더를 읽는다.
     */
    enum class WindowPointerLock : uint8
    {
        Released = 0, ///< 묶지 않는다.
        Confined,     ///< 포인터가 창 밖으로 나가지 못한다. 움직임은 그대로 온다.
        Locked,       ///< 포인터가 제자리에 서고 상대 이동만 온다.
    };

    /**
     * @brief 창 틀(비클라이언트 영역 — 제목 줄 · 테두리)이 클라이언트 영역 네 변 밖으로 나온 두께(물리 픽셀)입니다. `IWindow::tryGetFrameExtents` 가 채웁니다.
     * @details 틀 왼쪽 위(`setFramePosition` · `-WinX` 가 놓는 점)에서 클라이언트 왼쪽 위까지가 (`_left`, `_top`)이고, 틀 전체 크기는 클라이언트 + 양쪽 두께다.
     *          SDL `SDL_GetWindowBordersSize` · GLFW `glfwGetWindowFrameSize` 와 같은 자리다.
     */
    struct WindowFrameExtents
    {
        int32 _left{ 0 };
        int32 _top{ 0 };
        int32 _right{ 0 };
        int32 _bottom{ 0 };
    };

    /** @brief 창 바둑판의 한 칸입니다(`-WinTile` · `-WinTileColumns`). */
    struct WindowTileSlot
    {
        int32 _index{ 0 };       ///< 칸 번호(0 부터, 행 우선)
        int32 _columnCount{ 0 }; ///< 열 수(1 이상)
    };

    /** @brief X11 창의 서버 연결과 창 ID 입니다. `IWindow::tryGetX11Surface` 가 X11 창일 때만 채웁니다. */
    struct X11SurfaceHandles
    {
        void*  _pDisplay{ nullptr }; ///< X11 `Display*`
        uint64 _window{ 0 };         ///< X11 `Window`
    };
} // namespace sw

namespace sw
{
    /**
     * @class IWindow
     * @brief 애플리케이션의 주 화면이나 보조 화면을 추상화하는 기본 인터페이스입니다.
     * @details 플랫폼별(Windows, Linux) 구체 클래스가 이 인터페이스를 상속해 구현합니다.
     *          `IRenderSurface` 를 구현하므로 RHI 는 창을 **표면으로만** 봅니다. `Graphics` 가 `Window` 를
     *          include 하지 않는 이유입니다(Engine/Window/IRenderSurface.h).
     */
    class SW_API IWindow : public IRenderSurface
    {
    public:
        /** @brief 빈 창 인터페이스로 만듭니다. */
        IWindow();
        /** @brief 가상 소멸자입니다. 활성 창이면 전역 포인터도 끊습니다. */
        ~IWindow() override;
        /** @brief 복사를 금지합니다. */
        IWindow( const IWindow& ) = delete;
        /** @brief 대입을 금지합니다. */
        IWindow& operator=( const IWindow& ) = delete;
        /** @brief 이동을 금지합니다. */
        IWindow( IWindow&& ) = delete;
        /** @brief 이동 대입을 금지합니다. */
        IWindow& operator=( IWindow&& ) = delete;

        /**
         * @brief 창을 만듭니다. Win32 와 X11 은 여기서 띄우지 않으므로 `showWindow( true )` 를 불러야 보입니다.
         * @param pTitle 창 캡션(제목 줄)
         * @param width 클라이언트 영역 가로 크기
         * @param height 클라이언트 영역 세로 크기
         * @return 만들었으면 true 입니다.
         */
        [[nodiscard]] virtual bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) = 0;

        /** @brief 창 리소스를 놓고 화면에서 없앱니다. */
        virtual void destroy() = 0;

        /**
         * @brief 같은 크기, 제목, 위치, 표시 상태로 네이티브 창을 다시 만듭니다(OpenGL↔DXGI 핫스왑용).
         *
         * @details **절차는 여기 한 벌뿐이다.** `_bRecreating` 을 켜서 다시 만드는 동안의 리사이즈와 닫기 통보를 막고,
         *          **보이던 창을 다시 보이게 한다.** 백엔드 교체(`RHI::recreateDevice` → `recreateSurface`)가 이 함수를 부른다.
         *          플랫폼마다 정말로 다른 두 가지는 아래 훅으로 분리했다.
         *
         * @note 플랫폼이 이 기능을 지원하지 않으면 이 함수를 재정의해 `false` 를 반환합니다.
         *       재정의해서 **절차를 다시 적지는 마십시오** — 벌마다 조용히 어긋납니다.
         */
        [[nodiscard]] virtual bool recreate();

        /**
         * @brief 플랫폼 메시지 루프를 한 번 돕니다. 매 프레임 불러야 합니다.
         * @return 계속 실행해야 하면 true, 종료 요청(WM_CLOSE 등)이 있으면 false 입니다.
         */
        virtual bool processMessages() = 0;

        /**
         * @brief 창 메시지가 오거나 @p timeoutMilliseconds 가 지날 때까지 이 스레드를 재웁니다. 메시지를 꺼내지는 않는다(다음 `processMessages` 가 처리한다).
         * @details 최소화된 동안 앱 루프가 헛돌지 않게 `FramePacer` 가 부른다(Win32 `MsgWaitForMultipleObjects`, X11 · Wayland 는 연결 소켓의 `poll`).
         */
        virtual void waitForMessages( uint32 timeoutMilliseconds ) = 0;

        /**
         * @brief 창이 최소화되어 있는지입니다. `processMessages` 가 창 이벤트(Win32 `WM_SIZE`, X11 `UnmapNotify` · `MapNotify`)로 갱신합니다.
         * @details Wayland 는 클라이언트에 최소화를 알리지 않는다(xdg-shell 판 1) — 그 창에서는 늘 false 다.
         */
        [[nodiscard]] bool isMinimized() const { return _bMinimized == SW_TRUE; }
        /** @brief 표면이 없거나 앱이 멈춰 프레임을 돌면 안 되는 때입니다. 최소화와 다르다 — 최소화는 프레임을 돌되 늦춘다(`FramePacer`). */
        [[nodiscard]] bool isRenderPaused() const { return _bSurfaceLost == SW_TRUE || _bAppSuspended == SW_TRUE; }
        /** @brief `isRenderPaused` 인 채 지난 메인 루프 틱의 누적입니다(`advanceLifecycleTick`). */
        [[nodiscard]] uint32 getRenderPausedTickCount() const { return _renderPausedTickCount; }
        /**
         * @brief 플랫폼 창이 수명 이벤트를 받으면 부릅니다. 상태가 실제로 바뀔 때만 구독자에게 알립니다(같은 이벤트가 겹쳐 와도 한 번).
         * @details 창 스레드(메인 스레드)에서 부른다. `SurfaceLost` 는 구독자가 돌아온 뒤에 표면이 사라진다.
         */
        void notifyLifecycleEvent( WindowLifecycleEvent event );
        /** @brief 메인 루프가 틱마다 한 번 부릅니다. 멈춘 틱을 세고, 흉내 낸 멈춤(`simulateAppInterruption`)의 끝을 냅니다. */
        void advanceLifecycleTick();
        /**
         * @brief 표면을 잃고 앱이 멈췄다가 @p tickCount 틱 뒤 돌아오는 순서를 흉내 냅니다. 플랫폼 이벤트가 없는 데스크톱에서 멈춤 경로를 시나리오로 태우는 입구입니다(`SimulateAppSuspend`).
         * @details 멈춘 동안에는 시나리오 단계도 돌지 않으므로 끝나는 틱을 미리 받는다.
         */
        void simulateAppInterruption( uint32 tickCount );
        /**
         * @brief 이 프로세스가 입력 포커스를 가졌는지입니다(Win32 `WM_ACTIVATEAPP`, X11 `FocusIn` · `FocusOut`, Wayland 키보드 enter · leave).
         * @details 처음에는 false 다 — 활성화 통보가 와야 true 가 된다. Win32 는 창 하나가 아니라 앱 단위로 본다(에디터의 떠 있는 ImGui 창으로 옮겨도 배경이 아니다).
         */
        [[nodiscard]] bool hasFocus() const { return _bFocused == SW_TRUE; }

        /**
         * @brief Win32 창이면 `HWND` 를 `void*` 로 반환하고, 아니면(X11 · Wayland) 또는 핸들이 아직 없으면 nullptr 입니다. 호출처가 `static_cast<HWND>` 로 읽는다.
         * @details 핸들 포인터는 창 시스템마다 뜻이 다르다(X11 은 창 ID, Wayland 는 `wl_surface*`). `HWND` 로 읽는 호출이 종류를 확인하지 않으면 다른 창 시스템의 값을 Win32 호출에 넘긴다.
         *          그래서 핸들만 꺼내는 길(`getNativeHandle`)은 보호 구역에만 두고, Win32 호출은 종류 확인과 값 확인이 함께 있는 이 함수로만 얻는다(`tryGetX11Surface` 와 같은 모양).
         *          반환값을 버리면 안 되고, nullptr 이면 호출처가 건너뛰거나 오류로 멈춘다(다른 핸들로 대신하지 않는다).
         */
        [[nodiscard]] void* tryGetWin32Handle() const;
        /**
         * @brief X11 창이면 서버 연결(`Display*`)과 창 ID 를 @p outHandles 에 담고 true 입니다.
         * @details 연결 포인터는 창 시스템마다 다른 것이다(Wayland 는 `wl_display*`). 이를 `Display*` 로 읽으면 `DefaultScreen` 에서 죽는다(CI Wayland 스모크 SIGSEGV).
         *          그래서 연결만 따로 꺼내는 길(`getNativeDisplay`)은 보호 구역에만 두고, X11 호출은 종류 확인과 값 확인이 함께 있는 이 함수로만 얻는다.
         * @return X11 창이 아니거나(Wayland · Win32) 연결 · 창 ID 가 아직 없으면 false 이며 @p outHandles 는 비어 있다.
         */
        [[nodiscard]] bool tryGetX11Surface( X11SurfaceHandles& outHandles ) const;
        /**
         * @brief 창 틀(비클라이언트 영역)의 두께를 @p outExtents 에 담습니다. 창을 만든 뒤(`initializeWindow`)라면 띄우기 전에도 부를 수 있습니다.
         * @details Win32 는 `GetWindowRect` 와 화면 좌표의 클라이언트 사각형 차이다 — 위치 API(`CreateWindowExW` · `SetWindowPos`)가 그 사각형을 받기 때문이다
         *          (`DWMWA_EXTENDED_FRAME_BOUNDS` 의 보이는 틀은 보이지 않는 크기 조절 테두리를 빼서 위치와 단위가 맞지 않는다).
         *          X11 은 창 관리자가 적는 `_NET_FRAME_EXTENTS` 이고, 띄우기 전이면 `_NET_REQUEST_FRAME_EXTENTS` 로 청해 기다린다. Wayland 는 장식을 그리지 않으니 0 이다.
         * @return 두께를 알아내지 못하면(창이 없다 · 창 관리자가 알리지 않는다) 오류를 남기고 false 입니다 — 대략 값으로 대신하지 않는다.
         */
        [[nodiscard]] virtual bool tryGetFrameExtents( WindowFrameExtents& outExtents ) const = 0;
        /**
         * @brief 클라이언트가 창 자리를 정할 수 있는 창 시스템인지입니다(`setFramePosition` · `-WinX` · `-WinTile`).
         * @details Wayland(xdg-shell)는 자리를 컴포지터가 정한다 — 그 창에서는 위치 요청을 하지 않는다(GLFW `glfwSetWindowPos` 도 Wayland 에서는 하지 않는다).
         */
        [[nodiscard]] virtual bool canPlaceWindow() const = 0;
        /**
         * @brief 창 틀의 왼쪽 위를 화면 좌표 (@p x, @p y)로 옮깁니다. 크기는 그대로다. **창 스레드(메인 스레드)에서만 부릅니다.**
         * @return 창이 없거나 `canPlaceWindow` 가 아니면 false 입니다.
         */
        [[nodiscard]] virtual bool setFramePosition( int32 x, int32 y ) = 0;
        /**
         * @brief 바둑판 칸 @p slot 에 놓을 틀 왼쪽 위 좌표입니다. 원점은 주 모니터의 작업 영역 @p workArea 왼쪽 위다(작업 표시줄 · 패널 밖).
         * @details 칸 간격은 클라이언트 크기 + 틀 양쪽 두께라 이웃 창의 틀이 겹치지 않는다. 작업 영역이 모자라면 창 크기를 줄이지 않고(시나리오 그림 해상도다)
         *          가로 간격을 열 수에 맞게 좁혀 겹침을 고르게 나누고, 아래로 넘치는 행은 작업 영역 아래끝에 붙인다 — 모든 창이 작업 영역 안에 보인다.
         */
        static void computeTilePosition( const WindowTileSlot& slot, uint32 clientWidth, uint32 clientHeight, const WindowFrameExtents& extents, const WindowMonitorRect& workArea, int32& outX,
                                         int32& outY );
        /**
         * @brief 명령줄로 주 창을 만듭니다 — 플랫폼 창을 고르고(`createPlatformWindow`), 위치(`-WinX` · `-WinY`)를 건 뒤 만들고(`initializeWindow`), 바둑판 칸(`-WinTile`)이면
         *        자기 틀 두께(`tryGetFrameExtents`)로 자리를 정해 옮깁니다. 띄우지는 않는다(`showWindow`).
         * @details 주 창을 만드는 곳(RHI 기동 · 헤드리스 App)이 모두 이 길로 만든다 — 위치 인자를 읽는 규칙이 한 곳이다.
         * @return 인자가 틀렸거나(반쪽 좌표 · 칸과 좌표를 함께 · 열 수 없음) 창을 만들지 못하거나 틀 두께를 모르면 오류를 남기고 nullptr 입니다.
         */
        static unique_ptr<IWindow> createMainWindow( const utf8* pTitle, uint32 width, uint32 height, const CommandLineManager& commandLine );
        /** @brief 클라이언트 너비를 반환합니다. */
        virtual uint32 getWidth() const { return _width; }
        /** @brief 클라이언트 높이를 반환합니다. */
        virtual uint32 getHeight() const { return _height; }
        /**
         * @brief OS 가 이 창에 권하는 배율입니다(96 DPI = 1). Win32 는 `GetDpiForWindow`(PerMonitorV2 매니페스트이며 모니터를 옮기면 따라갑니다)를,
         *        X11 은 `Xft.dpi` 리소스를 씁니다. 모르면 1 입니다.
         * @details 게임 UI 는 기본으로 곱하지 않는다(해상도 규칙이 이미 창 크기를 따른다. `UIScaleSettings::_bApplyContentScale` 참고).
         */
        virtual float32 getContentScale() const { return 1.0f; }
        /**
         * @brief 그리면 가려지는 가장자리(왼쪽, 위, 오른쪽, 아래, 물리 픽셀)입니다 — 노치 · 둥근 모서리 · 시스템 바. 데스크톱 창은 0 이다.
         * @details UI 의 안전 영역(`UIScaleSettings::makeViewport` → `SafeZonePanel`)이 읽는다. 회전하면 변이 바뀌므로 플랫폼 창이 크기 · 회전 통보 때 다시 읽는다.
         *          언리얼 `FDisplayMetrics::TitleSafePaddingSize` · 유니티 `Screen.safeArea` 자리다(기억).
         */
        virtual float4 getSafeAreaInsets() const { return float4{}; }

        /** @brief 배율 1 에 해당하는 DPI 입니다(Windows 와 X11 의 기준). */
        static constexpr float32 kReferenceDPI = 96.0f;

        /**
         * @brief 창 시스템의 모니터를 @p outListMonitor 에 담습니다(주 모니터가 0 번). OS 에 묻는 일이라 프레임마다 부르지 않는다 - `getMonitorGeneration` 이 바뀔 때 다시 읽는다.
         * @details Win32 는 `EnumDisplayMonitors`, X11 은 RandR(없으면 X screen 하나 - `X11Monitors.h`), Wayland 는 `wl_output` 이다.
         * @return 창이 없어 물을 곳이 없으면 false 입니다. true 인데 목록이 비면 그 창 시스템에 지금 출력이 없는 것이다(모니터 없는 Wayland 컴포지터는 정상이다). 받는 쪽은 값을 지어내지 않는다.
         */
        [[nodiscard]] virtual bool tryGetMonitors( vector<WindowMonitor>& outListMonitor ) const;
        /** @brief 모니터 값이 바뀔 때마다(모니터 연결 · 해상도 · 작업 영역 · 배율) 올라가는 수입니다. 값을 들고 있는 쪽은 이것이 달라질 때 `tryGetMonitors` 를 다시 부릅니다. */
        [[nodiscard]] uint32 getMonitorGeneration() const { return _monitorGeneration; }

        // ---- 창이 스스로 하는 포인터 동작(리눅스 입력 관리자가 읽는다) ----
        // 입력 관리자는 창 종류가 아니라 이 질의로 가른다. 커서 모양 · 숨기기는 두 창 모두 스스로 한다(커서는 창의 Display 리소스라 창이 소유하고 창과 함께 내린다).
        // 포인터 묶기는 X11 서버 호출을 입력 관리자가 직접 하므로(`tryGetX11Surface`) X11 창은 하지 않고(기본: 지원 안 함) Wayland 만 컴포지터 확장으로 한다.
        // 새 확장이 더해져도 입력 관리자는 질의 하나만 읽는다.
        // "지원 안 함"이 정상인 곳(해당 없는 창)과 청했는데 안 되는 곳(오류)은 호출 쪽이 가른다 — 여기서는 대신하지 않고 false 로 알린다.

        /** @brief 창이 커서 모양을 스스로 바꾸는지(`setCursorShape` 가 되는지)입니다. */
        /**
         * @brief 이 창에 입력 초점을 요청합니다. 키 · 마우스를 전제하는 시나리오가 시작할 때 부릅니다(앞 단계의 다른 창 · 자식 프로세스 창이 초점을 가져갔을 수 있다).
         * @details 창 관리자가 초점을 돌려주지 않는 곳(X11 의 창 관리자 없는 화면)만 뜻이 있어 기본은 아무 일도 하지 않습니다 — Windows 는 다른 창이 닫히면 OS 가 돌려주고, 쓰고 있는 사람의 전경을 뺏지 않습니다.
         */
        virtual void               requestInputFocus() {}
        [[nodiscard]] virtual bool supportsCursorShape() const { return false; }
        /** @brief 커서 모양을 바꿉니다. `supportsCursorShape` 가 아니면 아무것도 하지 않고 false 입니다. */
        [[nodiscard]] virtual bool setCursorShape( CursorShape shape )
        {
            (void)shape;
            return false;
        }
        /** @brief 창이 포인터를 스스로 묶는지(`setPointerLock` 의 Locked 가 되는지)입니다. 컴포지터 확장이 없으면 false 입니다. */
        [[nodiscard]] virtual bool supportsPointerLock() const { return false; }
        /**
         * @brief 포인터를 묶거나 풉니다. 같은 방식을 다시 부르면 아무것도 하지 않는다.
         * @return 요청대로 되었으면(`Released` 는 항상) true, 못 했으면 false 입니다.
         */
        [[nodiscard]] virtual bool setPointerLock( WindowPointerLock lock ) { return lock == WindowPointerLock::Released; }
        /**
         * @brief 창이 운영체제 글 클립보드를 스스로 잇는지입니다(X11 은 CLIPBOARD 선택의 주인, Wayland 는 `wl_data_device` 선택 — 데이터 장치가 있어야 한다).
         *        아니면 `PlatformClipboard::isAvailable` 이 false 라서 UI 가 엔진 안 클립보드를 쓴다.
         */
        [[nodiscard]] virtual bool supportsClipboard() const { return false; }
        /** @brief 클립보드의 글을 @p outText 에 담습니다. 글이 없거나 시간 안에 답이 없거나 `supportsClipboard` 가 아니면 false 입니다. */
        [[nodiscard]] virtual bool readClipboardText( string& outText )
        {
            (void)outText;
            return false;
        }
        /** @brief 클립보드에 글을 씁니다. 못 쓰면(선택 주인이 못 되거나 컴포지터가 일련번호를 거절) false 입니다. */
        [[nodiscard]] virtual bool writeClipboardText( string_view text )
        {
            (void)text;
            return false;
        }
        /** @brief 커서를 숨기거나 보입니다. 창이 스스로 하지 않으면 false 입니다. */
        [[nodiscard]] virtual bool setCursorVisible( bool bVisible )
        {
            (void)bVisible;
            return false;
        }

        // ---- 입력기(IME). 모양은 `WindowTextInput.h` 머리말. 글은 `NativeWindowEvent::kMessageTextInput` 으로 외부 처리기에 온다 ----
        /**
         * @brief @p client 가 글 입력을 원하는지 알립니다. 어느 쪽이든 원하면 입력기가 켜집니다. 가상이 아니다 - 원하는 쪽을 세는 일을 플랫폼이 빠뜨릴 수 없게 하고, 실제 켜고 끄기만 `applyTextInputWanted` 로 내려간다.
         * @details 꺼진 동안 입력기는 키를 가져가지 않는다(게임의 WASD 가 한글 조합에 먹히지 않는다). 키가 낸 글자는 입력기가 꺼져도 확정 글로 온다. **창 스레드(메인 스레드)에서만 부릅니다.**
         */
        void requestTextInput( WindowTextInputClient client, bool bWanted );
        /** @brief 입력기를 원하는 쪽이 있는지입니다. */
        [[nodiscard]] bool isTextInputWanted() const { return _textInputClientMask != 0; }
        /** @brief 글 커서의 자리를 알립니다. 같은 자리면 아무것도 하지 않는다. 다시 만든 창에도 그대로 적용된다. **창 스레드(메인 스레드)에서만 부릅니다.** */
        void setTextInputArea( const WindowTextInputArea& area );

        /**
         * @brief 창 방식과 (창 모드일 때의) 클라이언트 크기를 바꿉니다.
         * @details 크기가 바뀌면 플랫폼의 크기 통보가 `bindResizeCallback` 의 콜백을 부른다. 스왑체인은 이 경로(Engine::onMainWindowResize 가 렌더 스레드를
         *          기다린 뒤 resize)로만 바뀐다. Win32 는 이 호출 안에서 같은 스레드로 통보하고, X11 은 다음 `processMessages` 에서 통보한다.
         *          **창 스레드(메인 스레드)에서만 부른다.** `BorderlessFullscreen` 이면 @p width 와 @p height 는 무시하고 모니터 크기를 쓴다.
         * @return 지원하지 않는 플랫폼이면 false 입니다.
         */
        virtual bool setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height )
        {
            (void)mode;
            (void)width;
            (void)height;
            return false;
        }
        /** @brief 이 데스크톱에서 창을 최소화할 수 있으면 true 입니다. 창 관리자가 없는 X 서버(Xvfb)는 요청해도 아무 일도 없어 false 입니다. */
        [[nodiscard]] virtual bool canMinimize() const { return false; }

        /**
         * @brief 창을 최소화하거나 되돌립니다. 최소화 상태(`isMinimized`)는 플랫폼의 통보가 바꾼다.
         * @details 자동화 시나리오(`MinimizeWindow` · `RestoreWindow`)가 최소화 중의 프레임 · 스왑체인 경로를 볼 때 쓴다. **창 스레드(메인 스레드)에서만 부른다.**
         * @return 지원하지 않는 플랫폼이면 false 입니다(Wayland 는 되돌리기 요청이 프로토콜에 없다).
         */
        virtual bool setMinimized( bool bMinimized )
        {
            (void)bMinimized;
            return false;
        }
        /**
         * @brief 자기 창에 포커스를 잃거나(@p bFocused false) 얻는 OS 이벤트를 보내 실제 이벤트 경로를 타게 합니다. OS 포커스는 그대로입니다. **창 스레드(메인 스레드)에서만 부른다.**
         * @details 자동화 시나리오(`PostWindowMessage` 의 `KillFocus` · `SetFocus`)가 부른다. Win32 는 이 호출 안에서 처리기가 돌고(`SendMessageW`), X11 은 다음 `processMessages` 가 받는다.
         *          언리얼 `FGenericWindow` · Godot `DisplayServer` 처럼 창 시스템 일은 창의 가상 함수에 두어 자동화 층이 플랫폼을 모른다.
         * @return 지원하지 않는 플랫폼이면 false 입니다(Wayland 는 클라이언트가 자기 키보드 enter · leave 를 보낼 길이 없다).
         */
        [[nodiscard]] virtual bool sendFocusEvent( bool bFocused )
        {
            (void)bFocused;
            return false;
        }
        /** @brief 마지막 `setDisplayMode` 가 고른 창 방식입니다. */
        WindowDisplayMode getDisplayMode() const { return _displayMode; }
        /**
         * @brief 사용자가 창을 줄일 수 있는 가장 작은 클라이언트 크기(물리 픽셀)를 정합니다. 0 이면 제한하지 않습니다.
         * @details 창 모드에만 적용한다(Win32 `WM_GETMINMAXINFO`, X11 `WM_NORMAL_HINTS` 최소 크기). 지금 창이 이보다 작으면 이 크기로 키운다.
         *          다시 만든 창(`recreate`)에도 그대로 적용된다. **창 스레드(메인 스레드)에서만 부른다.**
         */
        void setMinimumClientSize( uint32 width, uint32 height );
        /** @brief `setMinimumClientSize` 의 가로 크기입니다. 0 이면 제한이 없습니다. */
        uint32 getMinimumClientWidth() const { return _minClientWidth; }
        /** @brief `setMinimumClientSize` 의 세로 크기입니다. 0 이면 제한이 없습니다. */
        uint32 getMinimumClientHeight() const { return _minClientHeight; }

    private:
        /** 제목을 거는 길을 하나로 만든다 — 호출할 수 있는 곳은 `MainWindowSubsystem`(엔진의 제목 추적기가 `IWindowTitleTarget` 으로 부른다) 뿐이다. 다른 곳은 컴파일 오류다. */
        friend class MainWindowSubsystem;
        /**
         * @brief 창 제목을 바꿉니다(UTF-8). 같은 제목이거나 빈 제목이면 아무것도 하지 않습니다. **창 스레드(메인 스레드)에서만 부릅니다.**
         * @details 제목은 `_arrTitle` 에 남아 `recreate` 가 같은 제목으로 다시 만듭니다(빈 제목은 `recreate` 가 거절하므로 받지 않습니다).
         *          `kTitleCapacity` 를 넘는 제목은 오류를 남기고 받지 않습니다(지금 제목이 그대로 남습니다).
         */
        void setTitle( const utf8* pTitle );

    public:
        /**
         * @brief 다음 `initializeWindow` 가 창 틀의 왼쪽 위를 놓을 화면 좌표를 정합니다. 부르지 않으면 플랫폼이 놓습니다(Win32 `CW_USEDEFAULT`).
         * @details 다시 만들기(`recreate`)는 그때의 창 위치를 다시 잡으므로 이 값은 처음 만들 때만 뜻이 있다.
         */
        void setInitialPosition( int32 x, int32 y )
        {
            _restoreX = x;
            _restoreY = y;
        }
        /** @brief 지금 제목입니다(UTF-8, 널 종료). 창이 직접 들고 있어 힙을 쓰지 않습니다. */
        const utf8* getTitle() const { return _arrTitle; }
        /** @brief 제목이 담기는 최대 바이트(널 포함)입니다. 넘는 제목은 `setTitle` · `initializeWindow` 가 오류로 거절합니다. */
        static constexpr uint32 kTitleCapacity = constant::buffer::kMax256;

        // ------------------------------------------------------------------------------
        // IRenderSurface: RHI 가 창 시스템을 모르는 채로 묻는 다섯 가지
        // ------------------------------------------------------------------------------
        void*              getSurfaceHandle() const override { return getNativeHandle(); }
        void*              getSurfaceDisplay() const override { return getNativeDisplay(); }
        uint32             getSurfaceWidth() const override { return getWidth(); }
        uint32             getSurfaceHeight() const override { return getHeight(); }
        [[nodiscard]] bool recreateSurface() override { return recreate(); }
        /**
         * @brief 창을 화면에 띄우거나 숨깁니다. **"보이기로 했다" 는 사실을 기억합니다.**
         * @details 이 함수는 가상이 아니다. 의도를 기록하는 일을 플랫폼이 빠뜨릴 수 없게 하기 위해서다.
         *          실제로 창을 띄우고 내리는 일만 `applyWindowVisibility` 로 내려간다.
         */
        void showWindow( bool bShow )
        {
            _bVisibleRequested = bShow ? SW_TRUE : SW_FALSE;
            applyWindowVisibility( bShow );
        }

        /**
         * @brief **지금 화면에 보이는지** 플랫폼에 직접 묻습니다.
         * @warning 이 함수는 `isVisibleRequested()` 와 **다른 질문**입니다. X11 에서는 `XMapWindow` 가
         *          요청일 뿐이고 창 관리자가 실제로 매핑하기 전까지 `IsViewable` 이 아닙니다.
         *          즉 방금 보이라고 한 창도 여기서는 **false** 입니다. 최소화나 다른 워크스페이스도 같습니다.
         *          "다시 만든 뒤 복원할까" 같은 판단에는 쓰지 마십시오(그래서 `recreate` 는 의도를 봅니다).
         */
        virtual bool isVisible() const { return true; }

        /**
         * @brief **보이기로 정해져 있는지** 반환합니다. 마지막 `showWindow()` 의 인자입니다.
         * @details 창 관리자의 사정과 상관없이 바로 답합니다. 창을 다시 만들 때 복원할지를 정하는
         *          것은 이 값입니다. 화면 상태가 아니라 **우리가 원한 상태**가 기준이어야 합니다.
         */
        bool isVisibleRequested() const { return _bVisibleRequested == SW_TRUE; }

        // 아래 네 처리기는 칸마다 주인이 하나다 — 거는 쪽이 구독을 쥐고, 놓으면 칸이 빈다(이미 차 있으면 단언, `DelegateSlot`). 창 스레드(메인 스레드)에서 걸고 부른다.

        /** @brief 외부 이벤트 처리기(예: ImGui)를 겁니다. */
        [[nodiscard]] Subscription bindCustomMessageHandler( WindowMessageHandlerDelegate handler ) { return _customHandler.bind( std::move( handler ) ); }

        /**
         * @brief 접근성 클라이언트(스크린 리더)의 객체 요청에 답하는 처리기를 겁니다(Win32 `WM_GETOBJECT`). X11 은 보내는 메시지가 없어 부르지 않는다.
         * @details 외부 이벤트 처리기(`bindCustomMessageHandler`)와 따로 둔다 — 그쪽은 결과 값을 돌려줄 수 없고(처리하면 늘 1), 접근성 요청은 결과(제공자 핸들)가 답이다.
         *          처리기는 @p outResult 에 창 프로시저가 돌려줄 값을 적고 true 를 돌려준다. false 면 기본 처리로 간다.
         */
        [[nodiscard]] Subscription bindAccessibilityQueryHandler( WindowAccessibilityQueryDelegate handler ) { return _accessibilityQuery.bind( std::move( handler ) ); }

        /** @brief 창 크기가 바뀔 때 부를 콜백을 겁니다. */
        [[nodiscard]] Subscription bindResizeCallback( WindowResizeDelegate callback ) { return _onResize.bind( std::move( callback ) ); }
        /** @brief 수명 이벤트 처리기를 겁니다(`notifyLifecycleEvent`). 엔진이 주 창에 건다. */
        [[nodiscard]] Subscription bindLifecycleCallback( WindowLifecycleDelegate callback ) { return _onLifecycle.bind( std::move( callback ) ); }

        /** @brief 닫기 전에 불리는 처리기를 겁니다. false 를 반환하면 닫기를 보류합니다. */
        [[nodiscard]] Subscription bindCloseQueryHandler( WindowCloseQueryDelegate handler ) { return _closeQuery.bind( std::move( handler ) ); }
        /** @brief 확인 없이 종료 플래그를 켭니다. */
        void requestClose();
        /** @brief 닫기 쿼리를 거쳐 종료를 시도합니다. 허용되면 true 입니다. */
        [[nodiscard]] bool tryBeginClose();

        /** @brief 현재 플랫폼에 맞는 IWindow 인스턴스를 만들어 반환합니다. */
        static unique_ptr<IWindow> createPlatformWindow();

    protected:
        /**
         * @brief 네이티브 윈도우 핸들을 반환합니다. 어느 창 시스템의 것인지는 `getSurfaceKind` 가 말한다(Win32 `HWND` · X11 창 ID · Wayland `wl_surface*`).
         * @details 보호 구역이다 — 바깥에서 `HWND` 나 X11 `Window` 로 읽을 수 있으면 종류를 확인하지 않은 호출이 다시 생긴다. 바깥은 `tryGetWin32Handle` · `tryGetX11Surface` 나 `getSurfaceHandle`(종류와 함께 읽는 RHI) 를 쓴다.
         */
        virtual void* getNativeHandle() const = 0;

        /**
         * @brief 네이티브 디스플레이 연결을 반환합니다. 없으면 nullptr. 어느 창 시스템의 것인지는 `getSurfaceKind` 가 말한다.
         * @details 보호 구역이다 — 바깥에서 `Display*` 로 읽을 수 있으면 Wayland 의 `wl_display*` 를 읽는 호출이 다시 생긴다. 바깥은 `tryGetX11Surface` 나 `getSurfaceDisplay`(종류와 함께 읽는 RHI) 를 쓴다.
         */
        virtual void* getNativeDisplay() const { return nullptr; }

        /** @brief 실제로 창을 띄우거나 내립니다. 의도 기록은 기반이 이미 했습니다. */
        virtual void applyWindowVisibility( bool bShow ) { (void)bShow; }

        /**
         * @brief 다시 만들기 직전의 창 위치를 `_restoreX` 와 `_restoreY` 에 담습니다.
         * @details 플랫폼마다 묻는 API 가 다르다(`GetWindowRect`, `XGetWindowAttributes`).
         *          지원하지 않으면 아무것도 하지 않으면 된다. 그러면 새 창은 기본 위치에 뜬다.
         */
        virtual void captureRestorePosition() {}
        /**
         * @brief 복원 위치를 플랫폼의 "알아서 정해라" 값으로 되돌립니다.
         * @details 그 값이 플랫폼마다 다르다. Win32 는 `CW_USEDEFAULT`, X11 은 좌표 하나다.
         */
        virtual void clearRestorePosition() {}
        /**
         * @brief 최소 크기를 플랫폼에 알립니다. 값은 `_minClientWidth` 와 `_minClientHeight` 에 이미 있습니다.
         * @details 메시지를 받을 때마다 값을 읽는 플랫폼(Win32 `WM_GETMINMAXINFO`)은 할 일이 없다.
         */
        virtual void applyMinimumClientSize() {}
        /** @brief 창 모드 크기를 `setMinimumClientSize` 의 하한 위로 올립니다. OS 가 막아 주지 않는 플랫폼의 `setDisplayMode` 가 부른다(X11 의 힌트는 사용자 조작용이라 프로그램의 크기 바꾸기를 막지 않고, Wayland 는 크기를 클라이언트가 정한다). */
        void clampToMinimumClientSize( uint32& inoutWidth, uint32& inoutHeight ) const
        {
            inoutWidth  = inoutWidth < _minClientWidth ? _minClientWidth : inoutWidth;
            inoutHeight = inoutHeight < _minClientHeight ? _minClientHeight : inoutHeight;
        }
        /** @brief `_arrTitle` 을 플랫폼 창의 제목 줄에 겁니다. 창이 아직 없으면 아무것도 하지 않습니다(만들 때 `_arrTitle` 로 겁니다). */
        virtual void applyTitle() {}
        /** @brief 입력기를 켜거나 끕니다. 값이 바뀔 때만 불린다. 창을 만들 때는 플랫폼의 `initializeWindow` 가 `isTextInputWanted` 로 맞춘다. */
        virtual void applyTextInputWanted( bool bWanted ) { (void)bWanted; }
        /** @brief 입력기에 글 커서의 자리를 알립니다. */
        virtual void applyTextInputArea( const WindowTextInputArea& area ) { (void)area; }
        /** @brief 조합 · 확정 글을 외부 처리기(`bindCustomMessageHandler`)에 넘깁니다. 플랫폼 창만 부른다. */
        void dispatchTextInput( WindowTextInputKind kind, string_view text );

        /**
         * @brief @p pTitle 을 `_arrTitle` 에 담습니다(nullptr 은 빈 제목). 플랫폼의 `initializeWindow` 가 처음에 부릅니다.
         * @return `kTitleCapacity` 를 넘으면 오류를 남기고 false 입니다(제목을 자르지 않는다 — 창을 만들지 않는다).
         */
        [[nodiscard]] bool storeTitle( const utf8* pTitle );

        /** @brief 플랫폼 메시지 처리가 최소화 이벤트를 받으면 부릅니다(`isMinimized`). */
        void setMinimizedState( bool bMinimized ) { _bMinimized = bMinimized ? SW_TRUE : SW_FALSE; }
        /** @brief 플랫폼 메시지 처리가 포커스 이벤트를 받으면 부릅니다(`hasFocus`). */
        void setFocusState( bool bFocused ) { _bFocused = bFocused ? SW_TRUE : SW_FALSE; }
        /** @brief 플랫폼 메시지 처리가 모니터 변화를 받으면 부릅니다(`getMonitorGeneration`). */
        void notifyMonitorsChanged() { ++_monitorGeneration; }

    protected:
        /**
         * @brief `recreate` 가 같은 제목으로 다시 만들 제목입니다.
         * @details 창 안의 고정 버퍼다. 창은 엔진 종료 뒤에 파괴되므로, 힙 문자열이면 기동 뒤에 바꾼 제목(에디터)이 엔진 종료 누수 보고에 그 태그의 증가로 남았다.
         */
        utf8                                           _arrTitle[kTitleCapacity];
        DelegateSlot<WindowMessageHandlerDelegate>     _customHandler;
        DelegateSlot<WindowAccessibilityQueryDelegate> _accessibilityQuery;
        DelegateSlot<WindowResizeDelegate>             _onResize;
        DelegateSlot<WindowLifecycleDelegate>          _onLifecycle;
        DelegateSlot<WindowCloseQueryDelegate>         _closeQuery;
        uint32                                         _width;
        uint32                                         _height;
        uint8                                          _bShouldClose : 1;
        /**
         * @brief 다시 만드는 중인지 나타냅니다.
         * @details 플랫폼 메시지 처리기가 이 플래그를 보고 그 사이의 리사이즈와 닫기 통보를 삼킨다.
         *          다시 만드는 과정의 `destroy()` 가 앱 종료로 오해되면 안 되기 때문이다.
         *          절차가 이 클래스로 올라왔으므로 플래그도 같이 올라왔다.
         */
        uint8                  _bRecreating       : 1;
        uint8                  _bVisibleRequested : 1; /**< 마지막 showWindow() 의 인자. 화면 상태가 아니라 **의도**. */
        uint8                  _bMinimized        : 1; /**< 창 이벤트가 알린 최소화 상태(`isMinimized`). */
        uint8                  _bFocused          : 1; /**< 창 이벤트가 알린 포커스 상태(`hasFocus`). */
        uint8                  _bSurfaceLost      : 1; /**< 표면을 잃은 상태(`isRenderPaused`). */
        uint8                  _bAppSuspended     : 1; /**< 앱이 멈춘 상태(`isRenderPaused`). */
        [[maybe_unused]] uint8 _reserved          : 1;
        WindowDisplayMode      _displayMode;         /**< 마지막 `setDisplayMode` 가 고른 방식. */
        uint8                  _textInputClientMask; /**< 입력기를 원하는 쪽(`WindowTextInputClient` 비트). */
        uint8                  _arrReserved[5];
        WindowTextInputArea    _textInputArea; /**< 마지막 `setTextInputArea` 의 값. */
        /** @brief 다시 만들 때 놓을 위치입니다. 플랫폼 생성자가 자기 "알아서" 값으로 채웁니다. */
        int32  _restoreX;
        int32  _restoreY;
        uint32 _minClientWidth;             /**< 사용자가 줄일 수 있는 가장 작은 클라이언트 가로(물리 픽셀)입니다. 0 이면 제한이 없습니다. */
        uint32 _minClientHeight;            /**< 위와 같습니다(세로). */
        uint32 _monitorGeneration;          /**< `getMonitorGeneration` 의 값입니다. */
        uint32 _renderPausedTickCount;      /**< `getRenderPausedTickCount` 의 값입니다. */
        uint32 _simulatedInterruptionTicks; /**< 흉내 낸 멈춤의 남은 틱입니다(`simulateAppInterruption`). 0 이면 흉내 내지 않습니다. */

    private:
    };
} // namespace sw
