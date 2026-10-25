/**
 * @file IWindow.h
 * @brief 플랫폼 독립적인 창(OS 디스플레이 창) 생성과 메시지 처리 인터페이스입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Module/ModuleUnloadListener.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/IRenderSurface.h"
#include "Engine/Window/CursorShape.h"

namespace sw
{
    struct NativeWindowEvent;

    class CommandLineManager;

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
     *          include 하지 않는 이유입니다(Engine/Graphics/RHI/IRenderSurface.h).
     */
    class SW_API IWindow : public IRenderSurface, public IModuleUnloadListener
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
        virtual bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) = 0;

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

        /** @brief 배율 1 에 해당하는 DPI 입니다(Windows 와 X11 의 기준). */
        static constexpr float32 kReferenceDpi = 96.0f;

        // ---- 창이 스스로 하는 포인터 동작(리눅스 입력 관리자가 읽는다) ----
        // 입력 관리자는 창 종류가 아니라 이 질의로 가른다. X11 은 서버 호출을 입력 관리자가 직접 하므로(`tryGetX11Surface`) 이 동작을 하지 않고(기본: 지원 안 함),
        // X11 이 아닌 창(Wayland)만 컴포지터 확장으로 이 동작을 한다. 새 확장이 더해져도 입력 관리자는 질의 하나만 읽는다.
        // "지원 안 함"이 정상인 곳(해당 없는 창)과 청했는데 안 되는 곳(오류)은 호출 쪽이 가른다 — 여기서는 대신하지 않고 false 로 알린다.

        /** @brief 창이 커서 모양을 스스로 바꾸는지(`setCursorShape` 가 되는지)입니다. */
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

        /**
         * @brief 창 방식과 (창 모드일 때의) 클라이언트 크기를 바꿉니다.
         * @details 크기가 바뀌면 플랫폼의 크기 통보가 `setResizeCallback` 의 콜백을 부른다. 스왑체인은 이 경로(App::onResize 가 렌더 스레드를
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
        /**
         * @brief 명령줄 `-WinX` · `-WinY` 를 `setInitialPosition` 으로 겁니다. `initializeWindow` 앞에서 부릅니다.
         * @return 둘 다 없거나 둘 다 있으면 true 입니다. 하나만 있으면 오류를 남기고 false 입니다 — 반쪽 좌표를 플랫폼 값으로 채우지 않습니다.
         */
        [[nodiscard]] bool applyCommandLinePosition( const CommandLineManager& commandLine );
        /** @brief 지금 제목입니다(UTF-8, 널 종료). 창이 직접 들고 있어 힙을 쓰지 않습니다. */
        const utf8* getTitle() const { return _arrTitle; }
        /** @brief 제목이 담기는 최대 바이트(널 포함)입니다. 넘는 제목은 `setTitle` · `initializeWindow` 가 오류로 거절합니다. */
        static constexpr uint32 kTitleCapacity = constant::kMaxBuffer256;

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

        /** @brief 외부 이벤트 처리기(예: ImGui)를 연결합니다. */
        void setCustomMessageHandler( WindowMessageHandlerDelegate handler ) { _customHandler = std::move( handler ); }

        /**
         * @brief 접근성 클라이언트(스크린 리더)의 객체 요청에 답하는 처리기를 설정합니다(Win32 `WM_GETOBJECT`). X11 은 보내는 메시지가 없어 부르지 않는다.
         * @details 외부 이벤트 처리기(`setCustomMessageHandler`)와 따로 둔다 — 그쪽은 결과 값을 돌려줄 수 없고(처리하면 늘 1), 접근성 요청은 결과(제공자 핸들)가 답이다.
         *          처리기는 @p outResult 에 창 프로시저가 돌려줄 값을 적고 true 를 돌려준다. false 면 기본 처리로 간다. 창 스레드(메인 스레드)에서 불린다. 빼려면 빈 델리게이트를 넘긴다.
         */
        void setAccessibilityQueryHandler( WindowAccessibilityQueryDelegate handler ) { _accessibilityQuery = std::move( handler ); }

        /** @brief 창 크기가 바뀔 때 부를 콜백을 설정합니다. */
        void setResizeCallback( WindowResizeDelegate callback ) { _onResize = std::move( callback ); }

        /** @brief 닫기 전에 불리는 처리기를 설정합니다. false 를 반환하면 닫기를 보류합니다. */
        void setCloseQueryHandler( WindowCloseQueryDelegate handler ) { _closeQuery = std::move( handler ); }
        /** @brief 확인 없이 종료 플래그를 켭니다. */
        void requestClose();
        /** @brief 닫기 쿼리를 거쳐 종료를 시도합니다. 허용되면 true 입니다. */
        [[nodiscard]] bool tryBeginClose();
        /**
         * @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 처리기(메시지, 크기, 닫기)를 해제하고, 해제한 개수를 반환합니다.
         * @details 창은 App 이 소유해 모듈보다 오래 산다. 핫 리로드가 모듈 이미지를 언로드하기 전에 부른다. 에디터는 닫기 처리기를 등록한다.
         */
        uint32 removeCodeWithin( const void* pBegin, const void* pEnd );

        /** @brief 언로드 리스너 목록의 이름입니다. */
        const utf8* getModuleUnloadListenerName() const override { return "window handlers"; }
        /** @brief `removeCodeWithin` 입니다. 활성 창만이 아니라 살아 있는 모든 창이 훑깁니다. */
        uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override;

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
        /** @brief `_arrTitle` 을 플랫폼 창의 제목 줄에 겁니다. 창이 아직 없으면 아무것도 하지 않습니다(만들 때 `_arrTitle` 로 겁니다). */
        virtual void applyTitle() {}

        /**
         * @brief @p pTitle 을 `_arrTitle` 에 담습니다(nullptr 은 빈 제목). 플랫폼의 `initializeWindow` 가 처음에 부릅니다.
         * @return `kTitleCapacity` 를 넘으면 오류를 남기고 false 입니다(제목을 자르지 않는다 — 창을 만들지 않는다).
         */
        [[nodiscard]] bool storeTitle( const utf8* pTitle );

    protected:
        /**
         * @brief `recreate` 가 같은 제목으로 다시 만들 제목입니다.
         * @details 창 안의 고정 버퍼다. 창은 엔진 종료 뒤에 파괴되므로, 힙 문자열이면 기동 뒤에 바꾼 제목(에디터)이 엔진 종료 누수 보고에 그 태그의 증가로 남았다.
         */
        utf8                             _arrTitle[kTitleCapacity];
        WindowMessageHandlerDelegate     _customHandler;
        WindowAccessibilityQueryDelegate _accessibilityQuery;
        WindowResizeDelegate             _onResize;
        WindowCloseQueryDelegate         _closeQuery;
        uint32                           _width;
        uint32                           _height;
        uint8                            _bShouldClose : 1;
        /**
         * @brief 다시 만드는 중인지 나타냅니다.
         * @details 플랫폼 메시지 처리기가 이 플래그를 보고 그 사이의 리사이즈와 닫기 통보를 삼킨다.
         *          다시 만드는 과정의 `destroy()` 가 앱 종료로 오해되면 안 되기 때문이다.
         *          절차가 이 클래스로 올라왔으므로 플래그도 같이 올라왔다.
         */
        uint8                  _bRecreating       : 1;
        uint8                  _bVisibleRequested : 1; /**< 마지막 showWindow() 의 인자. 화면 상태가 아니라 **의도**. */
        [[maybe_unused]] uint8 _reserved          : 5;
        WindowDisplayMode      _displayMode; /**< 마지막 `setDisplayMode` 가 고른 방식. */
        uint8                  _arrReserved[6];
        /** @brief 다시 만들 때 놓을 위치입니다. 플랫폼 생성자가 자기 "알아서" 값으로 채웁니다. */
        int32  _restoreX;
        int32  _restoreY;
        uint32 _minClientWidth;  /**< 사용자가 줄일 수 있는 가장 작은 클라이언트 가로(물리 픽셀)입니다. 0 이면 제한이 없습니다. */
        uint32 _minClientHeight; /**< 위와 같습니다(세로). */
    };
} // namespace sw
