/**
 * @file FakeWindow.h
 * @brief 창 테스트가 쓰는 플랫폼 없는 창입니다. `IWindow` 의 순수 가상 함수에 모두 "하지 않는다" 로 답한다 — 테스트는 보려는 함수만 다시 재정의한다.
 * @details 테스트 폴더에만 있다(배포 구성은 짓지 않는다). 가짜마다 같은 답을 따로 적으면 `IWindow` 에 순수 가상이 늘 때마다 가짜 셋을 고쳐야 했다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Window/IWindow.h"
#include "Engine/Window/WindowMonitor.h"

namespace sw
{
    /** @class FakeWindow @brief 파일 머리말 참고. 표면 종류는 생성 인자다. */
    class FakeWindow : public IWindow
    {
    public:
        explicit FakeWindow( NativeSurfaceKind surfaceKind = NativeSurfaceKind::Win32 )
            : IWindow{}
            , _surfaceKind{ surfaceKind }
        {
        }

        bool initializeWindow( const utf8* /*pTitle*/, uint32 /*width*/, uint32 /*height*/ ) override { return true; }
        void destroy() override {}
        bool processMessages() override { return true; }
        void waitForMessages( uint32 /*timeoutMilliseconds*/ ) override {}
        bool tryGetFrameExtents( WindowFrameExtents& outExtents ) const override
        {
            outExtents = WindowFrameExtents{};
            return true;
        }
        bool              canPlaceWindow() const override { return false; }
        bool              setFramePosition( int32 /*x*/, int32 /*y*/ ) override { return false; }
        NativeSurfaceKind getSurfaceKind() const override { return _surfaceKind; }
        float32           getContentScale() const override { return 1.0f; }
        float4            getSafeAreaInsets() const override { return float4{}; }
        bool              tryGetMonitors( vector<WindowMonitor>& outListMonitor ) const override
        {
            outListMonitor.clear();
            return false;
        }
        bool isVisible() const override { return true; }
        bool requestInputFocus() override { return false; }
        bool supportsCursorShape() const override { return false; }
        bool setCursorShape( CursorShape /*shape*/ ) override { return false; }
        bool supportsPointerLock() const override { return false; }
        bool setPointerLock( WindowPointerLock lock ) override { return lock == WindowPointerLock::Released; }
        bool supportsClipboard() const override { return false; }
        bool readClipboardText( string& outText ) override
        {
            outText.clear();
            return false;
        }
        bool writeClipboardText( string_view /*text*/ ) override { return false; }
        bool setCursorVisible( bool /*bVisible*/ ) override { return false; }
        bool setDisplayMode( WindowDisplayMode /*mode*/, uint32 /*width*/, uint32 /*height*/ ) override { return false; }
        bool canMinimize() const override { return false; }
        bool setMinimized( bool /*bMinimized*/ ) override { return false; }
        bool sendFocusEvent( bool /*bFocused*/ ) override { return false; }
        /** @brief 플랫폼 메시지 처리가 새 크기를 받은 것처럼 합니다(Win32 `WM_SIZE` · X11 `ConfigureNotify`). */
        void simulateResize( uint32 width, uint32 height ) { notifyResized( width, height ); }
        /** @brief 플랫폼 모달 루프의 타이머가 온 것처럼 합니다(Win32 `WM_TIMER`). */
        void simulateModalLoopTimer() { tickModalLoop(); }

    protected:
        void* getNativeHandle() const override { return nullptr; }
        void  applyTextInputWanted( bool /*bWanted*/ ) override {}
        void  applyTextInputArea( const WindowTextInputArea& /*area*/ ) override {}

    private:
        NativeSurfaceKind _surfaceKind;
    };
} // namespace sw
