#include "pch.h"

#include "Engine/Input/Map/InputAccessibilitySettings.h"

#include "Core/Container/VectorUtil.h"

namespace sw
{
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
