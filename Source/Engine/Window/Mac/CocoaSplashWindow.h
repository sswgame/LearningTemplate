/**
 * @file CocoaSplashWindow.h
 * @brief macOS Cocoa 기반 ISplashWindow 구현체 헤더
 */
#pragma once
#include "Engine/Window/ISplashWindow.h"

namespace sw
{
    /**
     * @class CocoaSplashWindow
     * @brief macOS Cocoa 기반의 경량 스플래시 창
     */
    class CocoaSplashWindow : public ISplashWindow
    {
    public:
        CocoaSplashWindow();
        virtual ~CocoaSplashWindow() override;

        bool initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height ) override;
        void updateStatus( const utf8* pStatus ) override;
        void dismiss() override;

    private:
        [[maybe_unused]] void* _pCocoaWindow;
    };
} // namespace sw
