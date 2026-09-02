/**
 * @file X11SplashWindow.h
 * @brief Linux X11 기반 ISplashWindow 구현체 헤더
 */
#pragma once
#include "Engine/Window/ISplashWindow.h"

namespace sw
{
    /**
     * @class X11SplashWindow
     * @brief Linux X11 기반의 경량 스플래시 창
     */
    class X11SplashWindow : public ISplashWindow
    {
    public:
        X11SplashWindow();
        virtual ~X11SplashWindow() override;

        bool initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height ) override;
        void updateStatus( const utf8* pStatus, float32 progress = -1.0f ) override;
        void setProgress( float32 progress ) override;
        void dismiss() override;

    private:
        [[maybe_unused]] void*  _pX11Display;
        [[maybe_unused]] uint64 _x11Window;
    };
} // namespace sw
