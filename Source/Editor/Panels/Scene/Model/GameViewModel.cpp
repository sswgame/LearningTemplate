#include "pch.h"

#include "Editor/Panels/Scene/Model/GameViewModel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"

#include "Engine/Graphics/Canvas/DebugDrawCanvas.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 보조 창에서 게임으로 넘기는 단추입니다(에디터 단추와 게임 단추의 짝). 순서가 `_forwardedButtonMask` 의 비트다. */
        struct ForwardedButton
        {
            EditorMouseButton _editorButton;
            MouseButton       _button;
        };
        constexpr ForwardedButton kForwardedButtons[] = {
            {  EditorMouseButton::Left,   MouseButton::Left},
            { EditorMouseButton::Right,  MouseButton::Right},
            {EditorMouseButton::Middle, MouseButton::Middle}
        };
    } // namespace

    GameViewModel::GameViewModel()
        : _aspect{ EditorGameViewAspect::Free }
        , _bNoCameraHintShown{ false }
        , _forwardedButtonMask{ 0 }
        , _keyForwarder{}
    {
    }

    void GameViewModel::refreshNoCameraHint()
    {
        Scene*                 pScene      = editor::getActiveScene();
        const CameraComponent* pGameCamera = pScene != nullptr ? pScene->getActiveGameCamera() : nullptr;
        _bNoCameraHintShown                = CameraRegistry::isUsableCamera( pGameCamera ) == false;
    }

    void GameViewModel::forwardDetachedKeyboard( bool bForward, const vector<RawInputEvent>& listEvent )
    {
        InputManager& input = editor::getSubsystem<InputManager>();
        // 포커스를 잃었거나 패널을 주 창에 되돌렸으면 누른 채 남은 키를 놓는다(`DetachedKeyForwarder`).
        _keyForwarder.forward( bForward, listEvent, [&input]( const RawInputEvent& event )
        { return input.postRawEvent( event ); } );
    }

    void GameViewModel::forwardDetachedPointer( bool bDetached, bool bHovered, const EditorPointerState& pointer, float2 mainViewportPos )
    {
        if ( bDetached == false && _forwardedButtonMask == 0 )
            return; // 주 창에 있으면 주 창의 메시지가 게임으로 간다(`ImGuiEditor::processEvent`)
        InputManager& input    = editor::getSubsystem<InputManager>();
        const int32   x        = static_cast<int32>( MathUtil::round( pointer._position._x - mainViewportPos._x ) );
        const int32   y        = static_cast<int32>( MathUtil::round( pointer._position._y - mainViewportPos._y ) );
        const bool    bForward = bDetached && bHovered;
        if ( bForward && ( pointer._delta._x != 0.0f || pointer._delta._y != 0.0f ) )
            (void)input.postRawEvent( RawInputEvent::makeMouseMove( x, y ) ); // 큐가 차면 이 프레임의 이동만 잃는다 — 다음 이동이 위치를 다시 준다
        for ( uint32 index = 0; index < static_cast<uint32>( std::size( kForwardedButtons ) ); ++index )
        {
            const ForwardedButton& button       = kForwardedButtons[index];
            const uint8            bit          = static_cast<uint8>( 1u << index );
            const bool             bPressedHere = bForward && pointer.wasPressed( button._editorButton );
            if ( bPressedHere && input.postRawEvent( RawInputEvent::makeMouseButtonDown( button._button, x, y ) ) )
                _forwardedButtonMask = static_cast<uint8>( _forwardedButtonMask | bit );
            // 뗌은 넣은 누름의 짝이면 패널 밖이어도 넣는다 — 안 넣으면 게임에서 단추가 눌린 채 남는다.
            const bool bReleasedForwarded = ( _forwardedButtonMask & bit ) != 0 && pointer.wasReleased( button._editorButton );
            if ( bReleasedForwarded && input.postRawEvent( RawInputEvent::makeMouseButtonUp( button._button, x, y ) ) )
                _forwardedButtonMask = static_cast<uint8>( _forwardedButtonMask & ~bit );
        }
        if ( bForward && pointer._wheel != 0.0f )
            (void)input.postRawEvent( RawInputEvent::makeMouseWheel( pointer._wheel ) );
    }

    void GameViewModel::setDebugDrawShown( bool bShown )
    {
        DebugDrawCanvasUtil::setGameViewOverlayEnabled( bShown );
        EditorContext&        editorContext = editor::getSubsystem<EditorContext>();
        const utf8*           pCommand      = bShown ? "gv_debugDrawGameView 1" : "gv_debugDrawGameView 0";
        EditorNetPlaySession& session       = editorContext.getSubsystem<EditorNetPlaySession>();
        for ( int32 processIndex = 0; processIndex < session.getProcessCount(); ++processIndex )
        {
            (void)session.sendProcessCommand( processIndex, pCommand ); // 끝난 프로세스는 false — 받을 쪽이 없다
        }
    }
} // namespace sw::editor
