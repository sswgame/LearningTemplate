#include "pch.h"

#include "Engine/Window/CallbackInputWindow.h"

#include "Engine/Window/NativeWindowEvent.h"

namespace sw
{
    CallbackInputWindow::CallbackInputWindow()
        : IWindow{}
        , _listPendingInput{}
        , _safeInsets{}
        , _contentScale{ 1.0f }
    {
    }

    CallbackInputWindow::~CallbackInputWindow() = default;

    bool CallbackInputWindow::processMessages()
    {
        // 모은 것을 넘기는 동안 콜백이 더 넣지 못한다(같은 스레드) — 그래도 처리기가 창을 다시 부를 수 있어 사본을 돈다.
        vector<PlatformInputEvent> listInput = std::move( _listPendingInput );
        _listPendingInput.clear();
        for ( const PlatformInputEvent& input : listInput )
        {
            if ( _customHandler.isBound() == false )
                break;
            NativeWindowEvent windowEvent{};
            windowEvent.setNativeWindow( getSurfaceKind(), getNativeHandle() );
            windowEvent._message = NativeWindowEvent::kMessagePlatformInput;
            windowEvent._lParam  = reinterpret_cast<intptr_t>( &input );
            (void)_customHandler.get()( windowEvent );
        }
        return _bShouldClose == SW_FALSE;
    }

    bool CallbackInputWindow::tryGetFrameExtents( WindowFrameExtents& outExtents ) const
    {
        outExtents = WindowFrameExtents{};
        return true;
    }

    bool CallbackInputWindow::setFramePosition( int32 x, int32 y )
    {
        (void)x;
        (void)y;
        return false;
    }

    void CallbackInputWindow::applyPlatformResize( uint32 width, uint32 height, float32 contentScale, const float4& safeInsets )
    {
        _contentScale           = contentScale > 0.0f ? contentScale : 1.0f;
        _safeInsets             = safeInsets;
        const bool bSizeChanged = width != _width || height != _height;
        _width                  = width;
        _height                 = height;
        if ( bSizeChanged && _bRecreating == SW_FALSE && _onResize.isBound() )
            _onResize.get()( _width, _height );
    }
} // namespace sw
