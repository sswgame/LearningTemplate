/**
 * @file X11SplashWindow.h
 * @brief Linux X11 기반 ISplashWindow 구현입니다.
 */
#pragma once
#include "Engine/Window/ISplashWindow.h"

namespace sw
{
    /**
     * @class X11SplashWindow
     * @brief Linux X11 기반의 가벼운 스플래시 창입니다.
     */
    class X11SplashWindow : public ISplashWindow
    {
    public:
        X11SplashWindow();
        virtual ~X11SplashWindow() override;

        [[nodiscard]] bool initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height ) override;
        void               updateStatus( const utf8* pStatus, float32 progress = -1.0f ) override;
        void               setProgress( float32 progress ) override;
        void               dismiss() override;

    private:
        [[maybe_unused]] void*  _pX11Display;
        [[maybe_unused]] uint64 _x11Window;
        [[maybe_unused]] uint64 _x11Pixmap; /**< 창 배경으로 설정해 두는 이미지입니다. 서버가 알아서 다시 그립니다. */
    };
} // namespace sw
