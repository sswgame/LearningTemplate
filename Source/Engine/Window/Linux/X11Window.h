/**
 * @file X11Window.h
 * @brief Linux X11 네이티브 창입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Window/IWindow.h"

namespace sw
{
    /**
     * @struct X11ClipboardIncrTransfer
     * @brief 큰 글을 INCR(ICCCM 2.7.2)로 붙여넣는 프로그램에 조각으로 보내는 중인 한 건입니다.
     * @details 글은 건마다 복사해 든다 — 보내는 도중에 새로 복사해도 이 건은 처음 글을 끝까지 보낸다. 상대가 사라지면 `_deadline` 이 건을 치운다.
     */
    struct X11ClipboardIncrTransfer
    {
        X11ClipboardIncrTransfer( uint64 requestor, uint64 property, string text, Deadline deadline )
            : _requestor{ requestor }
            , _property{ property }
            , _text{ std::move( text ) }
            , _offset{ 0 }
            , _deadline{ deadline }
            , _bFinished{ false }
        {
        }

        uint64   _requestor; ///< 붙여넣는 프로그램의 창
        uint64   _property;  ///< 조각을 적는 그 창의 속성
        string   _text;      ///< 보낼 글(UTF-8)
        size_t   _offset;    ///< 다음에 보낼 바이트 자리
        Deadline _deadline;  ///< 상대가 이 시각까지 다음 조각을 청하지 않으면 건을 버린다
        bool     _bFinished; ///< 길이 0 조각(끝 신호)을 적었다. 상대가 그것을 지우면 건이 끝난다
    };
} // namespace sw

namespace sw
{
    /// @brief X11 네이티브 창입니다.
    class X11Window : public IWindow
    {
    public:
        /** @brief Display/Window 없이 시작합니다. */
        X11Window();
        /** @brief X11 창을 파괴합니다. */
        virtual ~X11Window() override;

        /**
         * @brief @p pWindow 가 X11 창이면 `X11Window*`, 아니면(nullptr · Wayland · Win32) nullptr 입니다.
         * @details 엔진은 RTTI 를 끄므로 종류(`getSurfaceKind`) 확인과 캐스트를 이 함수 한 곳에 둔다 — 다른 창 시스템의 창을 `X11Window*` 로 읽는 `static_cast` 를 바깥에 두지 않는다.
         */
        static X11Window* tryCast( IWindow* pWindow ) { return pWindow != nullptr && pWindow->getSurfaceKind() == NativeSurfaceKind::X11 ? static_cast<X11Window*>( pWindow ) : nullptr; }

        /** @brief X11 창을 만듭니다. 화면에 띄우는 것은 showWindow() 입니다. */
        [[nodiscard]] bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) override;
        /** @brief X11 창을 파괴합니다. */
        void destroy() override;
        /** @brief X11 이벤트를 처리합니다. 종료 요청이 있으면 false 를 반환합니다. */
        bool processMessages() override;
        /** @brief 핸들은 X11 Window, 디스플레이는 Display* 입니다. */
        NativeSurfaceKind getSurfaceKind() const override { return NativeSurfaceKind::X11; }

        /** @brief X11 창을 화면에 띄우거나 숨깁니다. */
        void applyWindowVisibility( bool bShow ) override;
        /** @brief X11 창이 지금 화면에 보이는지(창 관리자가 매핑했는지) 반환합니다. */
        bool isVisible() const override;
        /**
         * @brief 데스크톱이 알리는 `Xft.dpi` 리소스 / 96 입니다(GNOME · KDE · xrdb). 없으면 1.
         * @details XWayland 에서도 같은 길이다(컴포지터가 배율을 이 리소스에 적는다). 리소스는 연결을 연 때의 값(`XResourceManagerString` 은 `XOpenDisplay` 가 읽어 둔 것)이라
         *          실행 중 배율을 바꾸면 따라가지 않는다 — 서버에 묻는 길은 왕복이고 `Engine::updateRuntimeUI` 가 프레임마다 부르므로 그대로 둔다. Xrandr 의 물리 크기 DPI 는 배율이 아니라 화면 밀도라 쓰지 않는다.
         */
        float32 getContentScale() const override;

        /** @brief EWMH `_NET_WM_STATE_FULLSCREEN` 을 켜고 끄고, 창 모드면 `XResizeWindow` 로 크기를 바꿉니다. 크기 통보는 다음 ConfigureNotify 입니다. */
        bool setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height ) override;

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
        /**
         * @brief 이 창을 CLIPBOARD 선택의 주인으로 두고 @p text 를 들고 있습니다. 다른 프로그램의 요청(`SelectionRequest`)은 `processMessages` 가 답합니다.
         * @return 주인이 되지 못하면(창이 없거나 서버가 거절) false 입니다.
         */
        [[nodiscard]] bool writeClipboardText( string_view text ) override;
        /** @brief X11 창은 항상 CLIPBOARD 선택을 잇는다. */
        [[nodiscard]] bool supportsClipboard() const override { return true; }
        /**
         * @brief CLIPBOARD 선택의 글(UTF8_STRING)을 읽습니다. 주인이 이 창이면 들고 있는 글이고, 아니면 주인에게 변환을 청해 짧게 기다립니다. 큰 글은 INCR 조각으로 받습니다.
         * @return 주인이 없거나, UTF-8 글을 주지 않거나, 시간 안에 답하지 않으면 false 입니다.
         */
        [[nodiscard]] bool readClipboardText( string& outText ) override;
#endif

    protected:
        // 핸들 읽기는 기반의 보호 구역이다 — 바깥은 `IWindow::tryGetX11Surface` 로만 읽는다(여기서 다시 public 으로 열지 않는다).
#if defined( SW_PLATFORM_LINUX )
        /** @brief X11 창 핸들을 반환합니다. */
        void* getNativeHandle() const override { return reinterpret_cast<void*>( _x11Window ); }
        /** @brief X11 디스플레이 연결 핸들을 반환합니다. */
        void* getNativeDisplay() const override { return _pX11Display; }
#else
        void* getNativeHandle() const override { return nullptr; }
#endif

        /** @brief `XGetWindowAttributes` 로 지금 위치를 담습니다. */
        void captureRestorePosition() override;
        /** @brief 복원 위치를 창 관리자에게 맡기는 기본 좌표로 되돌립니다. */
        void clearRestorePosition() override;
        /** @brief `WM_NORMAL_HINTS` 의 최소 크기(PMinSize)를 창 관리자에게 알립니다. */
        void applyMinimumClientSize() override;
        /** @brief `XStoreName` 과 `_NET_WM_NAME`(UTF8_STRING)에 제목을 적습니다. */
        void applyTitle() override;

    private:
#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
        /** @brief 선택 이벤트(`SelectionRequest` · `SelectionClear`)면 처리하고 true 입니다. @p pEvent 는 `XEvent*` 입니다. */
        bool handleSelectionEvent( const void* pEvent );
        /** @brief 진행 중인 INCR 보내기가 기다리던 `PropertyNotify`(상대가 조각을 지움)면 다음 조각을 적고 true 입니다. @p pEvent 는 `XEvent*` 입니다. */
        bool handleIncrPropertyEvent( const void* pEvent );
        /** @brief 들고 있는 글을 큰 글로 보내기로 하고 @p property 에 INCR 표지를 적습니다. 건은 `_listClipboardIncr` 에 들어간다. 첫 조각은 상대가 표지를 지울 때 간다. */
        void beginClipboardIncr( uint64 requestor, uint64 property );
        /** @brief 기한이 지난(상대가 사라진) INCR 건을 버립니다. */
        void expireClipboardIncr();
#endif

    private:
        string                           _clipboardText;     ///< CLIPBOARD 주인일 때 내주는 글(UTF-8). 주인인지는 서버에 묻는다(`XGetSelectionOwner`) — 따로 표시하지 않는다
        vector<X11ClipboardIncrTransfer> _listClipboardIncr; ///< 큰 글을 조각으로 보내는 중인 건
        [[maybe_unused]] void*           _pX11Display;
        [[maybe_unused]] uint64          _x11Window;
        [[maybe_unused]] uint64          _x11WmDelete;
        [[maybe_unused]] uint8           _reservedX11 : 8;
        [[maybe_unused]] uint16          _padding;
    };
} // namespace sw
