/**
 * @file CocoaWindow.h
 * @brief macOS Cocoa 네이티브 윈도우
 */
#pragma once
#include "Engine/Window/IWindow.h"

namespace sw
{

    /// @brief Cocoa NSWindow + Metal 레이어
    class CocoaWindow : public IWindow
    {
    public:
        /** @brief NSWindow 없이 시작합니다. */
        CocoaWindow();
        /** @brief NSWindow를 파괴합니다. */
        virtual ~CocoaWindow() override;

        /** @brief Cocoa 창을 만들고 화면에 띄웁니다. */
        bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) override;
        /** @brief Cocoa 윈도우를 파괴합니다. */
        void destroy() override;
        /** @brief 기존 창의 크기와 위치를 유지한 채 Cocoa 윈도우를 재생성합니다. */
        bool recreate() override;
        /** @brief Cocoa 이벤트를 처리합니다. 종료 요청 시 false를 반환합니다. */
        bool processMessages() override;
        /** @brief 창 표시 / 숨김 제어 */
        void showWindow( bool bShow ) override;
        /** @brief 창이 현재 표시 중인지 여부 */
        bool isVisible() const override;

#if defined( SW_PLATFORM_MACOS )
        /** @brief Metal 레이어 포인터를 반환합니다. */
        void* getNativeHandle() const override { return _pCocoaMetalLayer; }
        /** @brief Cocoa NSWindow 포인터를 반환합니다. */
        void* getCocoaWindow() const { return _pCocoaWindow; }
#else
        void* getNativeHandle() const override { return nullptr; }
#endif

    private:
        [[maybe_unused]] void* _pCocoaWindow;
        [[maybe_unused]] void* _pCocoaApp;
        [[maybe_unused]] void* _pCocoaMetalLayer;
        [[maybe_unused]] void* _pCocoaDelegate;
    };
} // namespace sw
