#include "pch.h"

#include "Engine/Window/CallbackInputWindow.h"

#include "Engine/Window/NativeWindowEvent.h"

namespace sw
{
    CallbackInputWindow::CallbackInputWindow()
        : GenericWindow{}
        , _listPendingInput{}
        , _listPendingText{}
        , _safeInsets{}
        , _contentScale{ 1.0f }
    {
    }

    CallbackInputWindow::~CallbackInputWindow() = default;

    bool CallbackInputWindow::processMessages()
    {
        // 모은 것을 넘기는 동안 콜백이 더 넣지 못한다(같은 스레드) — 그래도 처리기가 창을 다시 부를 수 있어 사본을 돈다.
        vector<PlatformInputEvent> listInput = std::move( _listPendingInput );
        vector<PendingText>        listText  = std::move( _listPendingText );
        _listPendingInput.clear();
        _listPendingText.clear();
        size_t textIndex = 0;
        for ( size_t inputIndex = 0; inputIndex <= listInput.size(); ++inputIndex )
        {
            // 이 입력 앞에 온 글을 먼저 넘긴다(받은 차례 — 백스페이스와 글자가 한 프레임에 섞여도 순서가 맞다).
            for ( ; textIndex < listText.size() && listText[textIndex]._inputPosition <= inputIndex; ++textIndex )
            {
                dispatchTextInput( listText[textIndex]._kind, listText[textIndex]._text );
            }
            if ( inputIndex == listInput.size() || _customHandler.isBound() == false )
                continue;
            NativeWindowEvent windowEvent{};
            windowEvent.setNativeWindow( getSurfaceKind(), getNativeHandle() );
            windowEvent._message = NativeWindowEvent::kMessagePlatformInput;
            windowEvent._lParam  = reinterpret_cast<intptr_t>( &listInput[inputIndex] );
            (void)_customHandler.get()( windowEvent );
        }
        return _bShouldClose == SW_FALSE;
    }

    void CallbackInputWindow::queueTextInput( WindowTextInputKind kind, string_view text )
    {
        PendingText pending;
        pending._inputPosition = _listPendingInput.size();
        pending._kind          = kind;
        pending._text.assign( text.data(), text.size() );
        _listPendingText.push_back( std::move( pending ) );
    }

    void CallbackInputWindow::queueTextActions( const vector<AndroidTextAction>& listAction )
    {
        for ( const AndroidTextAction& action : listAction )
        {
            switch ( action._kind )
            {
                case AndroidTextActionKind::Composition:
                    queueTextInput( WindowTextInputKind::Composition, action._text );
                    break;
                case AndroidTextActionKind::Commit:
                    queueTextInput( WindowTextInputKind::Commit, action._text );
                    break;
                case AndroidTextActionKind::BackspaceKey:
                case AndroidTextActionKind::DeleteKey:
                {
                    // 입력기의 지우기는 키 누름 · 뗌 한 쌍이다 — 하드웨어 키(AKEYCODE_DEL · FORWARD_DEL)와 같은 길로 간다.
                    PlatformInputEvent input{};
                    input._keyCode = action._kind == AndroidTextActionKind::BackspaceKey ? AndroidInputConnectionState::kBackspaceKeyCode : AndroidInputConnectionState::kForwardDeleteKeyCode;
                    input._kind    = PlatformInputKind::KeyDown;
                    queueInput( input );
                    input._kind = PlatformInputKind::KeyUp;
                    queueInput( input );
                    break;
                }
            }
        }
    }

    void CallbackInputWindow::applyPlatformFocus( bool bFocused )
    {
        if ( hasFocus() == bFocused )
            return;
        GenericWindow::setFocusState( bFocused );
        PlatformInputEvent event{};
        event._kind = bFocused ? PlatformInputKind::FocusGained : PlatformInputKind::FocusLost;
        queueInput( event );
    }

    bool CallbackInputWindow::tryGetMonitors( vector<WindowMonitor>& outListMonitor ) const
    {
        outListMonitor.clear();
        return false;
    }

    bool CallbackInputWindow::setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height )
    {
        (void)mode;
        (void)width;
        (void)height;
        return false;
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
        _contentScale = contentScale > 0.0f ? contentScale : 1.0f;
        _safeInsets   = safeInsets;
        notifyResized( width, height );
    }
} // namespace sw
