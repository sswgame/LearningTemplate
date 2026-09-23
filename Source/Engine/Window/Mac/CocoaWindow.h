/**
 * @file CocoaWindow.h
 * @brief macOS Cocoa 네이티브 창입니다.
 */
#pragma once
#include "Engine/Window/IWindow.h"

namespace sw
{

    /// @brief Cocoa NSWindow 와 Metal 레이어입니다.
    class CocoaWindow : public IWindow
    {
    public:
        /** @brief NSWindow 없이 시작합니다. */
        CocoaWindow();
        /** @brief NSWindow 를 파괴합니다. */
        virtual ~CocoaWindow() override;

        /** @brief Cocoa 창을 만들고 화면에 띄웁니다. */
        bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) override;
        /** @brief Cocoa 창을 파괴합니다. */
        void destroy() override;
        /** @brief 창 다시 만들기를 지원하지 않습니다. 항상 false 를 반환합니다. */
        bool recreate() override;
        /** @brief Cocoa 이벤트를 처리합니다. 종료 요청이 있으면 false 를 반환합니다. */
        bool processMessages() override;
        /** @brief 창을 띄우거나 숨깁니다. */
        void applyWindowVisibility( bool bShow ) override;
        /** @brief 창이 지금 화면에 보이는지 반환합니다. */
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
