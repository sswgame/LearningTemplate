#include "pch.h"

#include "Engine/UI/Input/UIInputMapView.h"

#include "Engine/Input/Map/InputMap.h"
#include "Engine/UI/UISystem.h"

namespace sw
{
    UIInputMapView::UIInputMapView( const InputMap& inputMap, const UISystem* pUISystem )
        : _inputMap{ inputMap }
        , _pUISystem{ pUISystem != nullptr && pUISystem->isInitialized() ? pUISystem : nullptr }
    {
    }

    bool UIInputMapView::isActionBlocked( const hashed_string& action ) const
    {
        if ( _pUISystem == nullptr )
            return false;
        return _pUISystem->isGameInputBlocked() || _pUISystem->isActionConsumed( _inputMap, action );
    }

    bool UIInputMapView::wasActionTriggered( const hashed_string& action ) const
    {
        return _inputMap.wasActionTriggered( action ) && isActionBlocked( action ) == false;
    }

    bool UIInputMapView::wasActionPressed( const hashed_string& action ) const
    {
        return _inputMap.wasActionPressed( action ) && isActionBlocked( action ) == false;
    }

    bool UIInputMapView::isActionDown( const hashed_string& action ) const
    {
        return _inputMap.isActionDown( action ) && isActionBlocked( action ) == false;
    }

    float32 UIInputMapView::getAxis1D( const hashed_string& action ) const
    {
        return isActionBlocked( action ) ? 0.0f : _inputMap.getAxis1D( action );
    }

    float2 UIInputMapView::getVector2D( const hashed_string& action ) const
    {
        return isActionBlocked( action ) ? float2{} : _inputMap.getVector2D( action );
    }
} // namespace sw
