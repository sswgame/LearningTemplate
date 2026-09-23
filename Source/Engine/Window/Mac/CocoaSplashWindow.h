/**
 * @file CocoaSplashWindow.h
 * @brief macOS Cocoa 기반 ISplashWindow 구현입니다.
 */
#pragma once
#include "Engine/Window/ISplashWindow.h"

namespace sw
{
    /**
     * @class CocoaSplashWindow
     * @brief macOS Cocoa 기반의 가벼운 스플래시 창입니다.
     */
    class CocoaSplashWindow : public ISplashWindow
    {
    public:
        CocoaSplashWindow();
        virtual ~CocoaSplashWindow() override;

        bool initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height ) override;
        void updateStatus( const utf8* pStatus, float32 progress = -1.0f ) override;
        void setProgress( float32 progress ) override;
        void dismiss() override;

    private:
        [[maybe_unused]] void* _pCocoaWindow;
    };
} // namespace sw
