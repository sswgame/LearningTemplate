#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Client/MetroidvaniaHUDViewModel.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    MetroidvaniaHUDViewModel::MetroidvaniaHUDViewModel()
        : UIViewModel{}
        , _healthRatio{ 1.0f }
        , _staminaRatio{ 1.0f }
        , _flaskCount{ 0 }
        , _currency{ 0 }
        , _completion{ 0 }
        , _deadVisibility{ WidgetVisibility::Collapsed }
        , _winVisibility{ WidgetVisibility::Collapsed }
    {
    }

    MetroidvaniaHUDViewModel::~MetroidvaniaHUDViewModel() = default;

    const TypeInfo* MetroidvaniaHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void MetroidvaniaHUDViewModel::setVitals( float32 healthRatio, float32 staminaRatio, int32 flaskCount )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _healthRatio, MathUtil::clamp( healthRatio, 0.0f, 1.0f ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _staminaRatio, MathUtil::clamp( staminaRatio, 0.0f, 1.0f ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _flaskCount, flaskCount );
    }

    void MetroidvaniaHUDViewModel::setProgress( int32 currency, float32 completionPercent )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _currency, currency );
        (void)SW_SET_VIEW_MODEL_FIELD( _completion, static_cast<int32>( completionPercent ) );
    }

    void MetroidvaniaHUDViewModel::setResult( bool bDead, bool bWon )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _deadVisibility, toHUDVisibility( bDead ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _winVisibility, toHUDVisibility( bWon ) );
    }
} // namespace sw
