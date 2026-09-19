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
        /**
         * @brief 스플래시 이미지를 **창 크기로** 줄여 `_listScaledPixel` 에 담습니다.
         * @details X11 에는 `StretchDIBits` 같은 것이 없다 — `XPutImage` 는 **1:1 로만** 찍는다.
         *          그래서 예전에는 1376×768 원본이 480×280 창에 **좌상단만** 그려졌다.
         */
        void buildScaledImage();

    private:
        [[maybe_unused]] void*  _pX11Display;
        [[maybe_unused]] uint64 _x11Window;
        [[maybe_unused]] uint64 _x11Pixmap;       /**< 창 배경으로 걸어 두는 그림. 서버가 알아서 다시 그린다. */
        vector<uint8>           _listScaledPixel; /**< 창 크기로 줄인 BGRA 픽셀. 비어 있으면 그리지 않는다. */
    };
} // namespace sw
