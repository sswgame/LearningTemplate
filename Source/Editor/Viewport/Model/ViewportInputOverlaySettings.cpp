// 뷰포트 입력 오버레이의 Model 쪽(ImGui 없음) — 설정 서브시스템 등록과 입력 값 모으기. 그리기는 `ViewportInputOverlay.cpp`(MVVM View)이고 이 값만 본다.
#include "pch.h"

#include "Core/Container/EnumArray.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Viewport/ViewportInputOverlay.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputGlyphResolver.h"
#include "Engine/Input/Map/InputMap.h"

namespace sw::editor
{
    SW_EDITOR_SUBSYSTEM( ViewportInputOverlaySettings, 20 );

    void ViewportInputOverlayState::gather( const InputManager& input, bool bWithActions, ViewportInputOverlayState& outState )
    {
        // 오버레이 버튼 차례(`ViewportOverlayFaceButton`)의 패드 버튼이다.
        static constexpr auto kArrPadButton = makeEnumTable<ViewportOverlayFaceButton, GamepadButton>( GamepadButton::A, GamepadButton::B, GamepadButton::X, GamepadButton::Y );

        const GamepadDevice* pGamepad   = input.getGamepad( 0 );
        const bool           bConnected = pGamepad != nullptr && pGamepad->isConnected();
        outState._leftStick             = bConnected ? pGamepad->getLeftStick() : float2{};
        outState._rightStick            = bConnected ? pGamepad->getRightStick() : float2{};
        outState._leftTrigger           = bConnected ? pGamepad->getLeftTrigger() : 0.0f;
        outState._rightTrigger          = bConnected ? pGamepad->getRightTrigger() : 0.0f;
        outState._faceButtonDownMask    = 0;
        for ( uint32 index = 0; index < static_cast<uint32>( ViewportOverlayFaceButton::Count ); ++index )
        {
            if ( pGamepad != nullptr && pGamepad->isButtonDown( kArrPadButton[static_cast<ViewportOverlayFaceButton>( index )] ) )
                outState._faceButtonDownMask |= static_cast<uint8>( 1u << index );
        }

        outState._listActiveAction.clear();
        if ( bWithActions == false )
            return;
        const InputMap& inputMap = input.getInputMap();
        for ( const hashed_string& action : inputMap.getActionNames() )
        {
            if ( inputMap.isActionDown( action ) )
            {
                StringBuilder<constant::buffer::kMax256> actionLabel;
                actionLabel.appendFormat( "%# %#", InputGlyphResolver::findActionGlyph( inputMap, sw::hashed_string( action.view() ) ), action.c_str() );
                outState._listActiveAction.push_back( string( actionLabel.view() ) );
            }
        }
    }
} // namespace sw::editor
