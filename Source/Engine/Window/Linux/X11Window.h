/**
 * @file X11Window.h
 * @brief Linux X11 네이티브 창입니다.
 */
#pragma once
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

#if defined( SW_PLATFORM_LINUX )
        /** @brief X11 창 핸들을 반환합니다. */
        void* getNativeHandle() const override { return reinterpret_cast<void*>( _x11Window ); }
        /** @brief X11 디스플레이 연결 핸들을 반환합니다. */
        void* getNativeDisplay() const override { return _pX11Display; }
        /** @brief X11 전용 창 ID 를 반환합니다. */
        uint64 getX11Window() const { return _x11Window; }
#else
        void* getNativeHandle() const override { return nullptr; }
#endif

    protected:
        /** @brief `XGetWindowAttributes` 로 지금 위치를 담습니다. */
        void captureRestorePosition() override;
        /** @brief 복원 위치를 창 관리자에게 맡기는 기본 좌표로 되돌립니다. */
        void clearRestorePosition() override;

    private:
        [[maybe_unused]] void*  _pX11Display;
        [[maybe_unused]] uint64 _x11Window;
        [[maybe_unused]] uint64 _x11WmDelete;
        [[maybe_unused]] uint8  _reservedX11 : 8;
        [[maybe_unused]] uint16 _padding;
    };
} // namespace sw
