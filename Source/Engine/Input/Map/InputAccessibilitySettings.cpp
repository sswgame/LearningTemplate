#include "pch.h"

#include "Engine/Input/Map/InputAccessibilitySettings.h"

#include "Core/Algorithm/VectorUtil.h"

/**
 * @file InputAccessibilitySettings.cpp
 * @brief 플레이어마다의 입력 설정(토글 액션 목록)을 구현합니다.
 */

namespace sw
{
    InputAccessibilitySettings::InputAccessibilitySettings()
        : _mouseSensitivity{ 1.0f, 1.0f }
        , _gamepadSensitivity{ 1.0f, 1.0f }
        , _stickDeadzoneOverride{ -1.0f }
        , _deadzoneShape{ DeadzoneShape::Radial }
        , _digitalNormalization{ DigitalNormalization::Circular }
        , _bInvertX{ SW_FALSE }
        , _bInvertY{ SW_FALSE }
        , _listToggleAction{}
    {
    }

    void InputAccessibilitySettings::setToggleAction( const hashed_string& action, bool bToggle )
    {
        if ( action.empty() )
            return;
        if ( bToggle == false )
        {
            (void)VectorUtil::removeFirst( _listToggleAction, action ); // 없으면 지울 것이 없다
            return;
        }
        if ( VectorUtil::contains( _listToggleAction, action ) == false )
            _listToggleAction.push_back( action );
    }

    bool InputAccessibilitySettings::isToggleAction( const hashed_string& action ) const
    {
        return VectorUtil::contains( _listToggleAction, action );
    }
} // namespace sw
