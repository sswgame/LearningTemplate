/**
 * @file X11Window.h
 * @brief Linux X11 네이티브 창입니다.
 */
#pragma once
#include "Core/Container/string.h"

#include "Engine/Window/IWindow.h"

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

        /** @brief X11 창을 만듭니다. 화면에 띄우는 것은 showWindow() 입니다. */
        bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) override;
        /** @brief X11 창을 파괴합니다. */
        void destroy() override;
        /** @brief X11 이벤트를 처리합니다. 종료 요청이 있으면 false 를 반환합니다. */
        bool processMessages() override;

        /** @brief X11 창을 화면에 띄우거나 숨깁니다. */
        void applyWindowVisibility( bool bShow ) override;
        /** @brief X11 창이 지금 화면에 보이는지(창 관리자가 매핑했는지) 반환합니다. */
        bool isVisible() const override;
        /** @brief 데스크톱이 알리는 `Xft.dpi` 리소스 / 96 입니다(GNOME · KDE · xrdb). 없으면 1. */
        float32 getContentScale() const override;

        /** @brief EWMH `_NET_WM_STATE_FULLSCREEN` 을 켜고 끄고, 창 모드면 `XResizeWindow` 로 크기를 바꿉니다. 크기 통보는 다음 ConfigureNotify 입니다. */
        bool setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height ) override;

#if defined( SW_PLATFORM_LINUX )
        /** @brief X11 창 핸들을 반환합니다. */
        void* getNativeHandle() const override { return reinterpret_cast<void*>( _x11Window ); }
        /** @brief X11 디스플레이 연결 핸들을 반환합니다. */
        void* getNativeDisplay() const override { return _pX11Display; }
        /** @brief X11 전용 창 ID 를 반환합니다. */
        uint64 getX11Window() const { return _x11Window; }
    #if defined( SW_WITH_CLIENT_CODE )
        /**
         * @brief 이 창을 CLIPBOARD 선택의 주인으로 두고 @p text 를 들고 있습니다. 다른 프로그램의 요청(`SelectionRequest`)은 `processMessages` 가 답합니다.
         * @return 주인이 되지 못하면(창이 없거나 서버가 거절) false 입니다.
         */
        [[nodiscard]] bool writeClipboardText( string_view text );
        /**
         * @brief CLIPBOARD 선택의 글(UTF8_STRING)을 읽습니다. 주인이 이 창이면 들고 있는 글이고, 아니면 주인에게 변환을 청해 짧게 기다립니다. 큰 글은 INCR 조각으로 받습니다.
         * @return 주인이 없거나, UTF-8 글을 주지 않거나, 시간 안에 답하지 않으면 false 입니다.
         */
        [[nodiscard]] bool readClipboardText( string& outText );
    #endif
#else
        void* getNativeHandle() const override { return nullptr; }
#endif

    protected:
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
#endif

    private:
        string                  _clipboardText; ///< CLIPBOARD 주인일 때 내주는 글(UTF-8). 주인인지는 서버에 묻는다(`XGetSelectionOwner`) — 따로 표시하지 않는다
        [[maybe_unused]] void*  _pX11Display;
        [[maybe_unused]] uint64 _x11Window;
        [[maybe_unused]] uint64 _x11WmDelete;
        [[maybe_unused]] uint8  _reservedX11 : 8;
        [[maybe_unused]] uint16 _padding;
    };
} // namespace sw
