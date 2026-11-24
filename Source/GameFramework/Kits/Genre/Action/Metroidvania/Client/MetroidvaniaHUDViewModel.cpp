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
        (void)setField( _healthRatio, MathUtil::clamp( healthRatio, 0.0f, 1.0f ), "_healthRatio" );
        (void)setField( _staminaRatio, MathUtil::clamp( staminaRatio, 0.0f, 1.0f ), "_staminaRatio" );
        (void)setField( _flaskCount, flaskCount, "_flaskCount" );
    }

    void MetroidvaniaHUDViewModel::setProgress( int32 currency, float32 completionPercent )
    {
        (void)setField( _currency, currency, "_currency" );
        (void)setField( _completion, static_cast<int32>( completionPercent ), "_completion" );
    }

    void MetroidvaniaHUDViewModel::setResult( bool bDead, bool bWon )
    {
        (void)setField( _deadVisibility, toVisibility( bDead ), "_deadVisibility" );
        (void)setField( _winVisibility, toVisibility( bWon ), "_winVisibility" );
    }

    WidgetVisibility MetroidvaniaHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }
} // namespace sw
