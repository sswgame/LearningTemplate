#include "pch.h"

#include "Engine/Input/Map/GameInputView.h"

#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/Map/InputMap.h"

namespace sw
{
    GameInputView::GameInputView( const InputMap& inputMap, const IInputCaptureQuery* pCapture )
        : _inputMap{ inputMap }
        , _pCapture{ pCapture }
    {
    }

    bool GameInputView::isActionBlocked( const hashed_string& action ) const
    {
        if ( _pCapture == nullptr )
            return false;
        return _pCapture->isGameInputCaptured() || _pCapture->isActionCaptured( _inputMap, action );
    }

    bool GameInputView::wasActionTriggered( const hashed_string& action ) const
    {
        return _inputMap.wasActionTriggered( action ) && isActionBlocked( action ) == false;
    }

    bool GameInputView::wasActionPressed( const hashed_string& action ) const
    {
        return _inputMap.wasActionPressed( action ) && isActionBlocked( action ) == false;
    }

    bool GameInputView::isActionDown( const hashed_string& action ) const
    {
        return _inputMap.isActionDown( action ) && isActionBlocked( action ) == false;
    }

    float32 GameInputView::getAxis1D( const hashed_string& action ) const
    {
        return isActionBlocked( action ) ? 0.0f : _inputMap.getAxis1D( action );
    }

    float2 GameInputView::getVector2D( const hashed_string& action ) const
    {
        return isActionBlocked( action ) ? float2{} : _inputMap.getVector2D( action );
    }
} // namespace sw
