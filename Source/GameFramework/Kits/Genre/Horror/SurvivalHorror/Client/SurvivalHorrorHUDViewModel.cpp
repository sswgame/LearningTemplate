#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/Client/SurvivalHorrorHUDViewModel.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    SurvivalHorrorHUDViewModel::SurvivalHorrorHUDViewModel()
        : UIViewModel{}
        , _items{ 0 }
        , _healthRatio{ 0.0f }
        , _sanityRatio{ 0.0f }
        , _batteryRatio{ 0.0f }
        , _darkVisibility{ WidgetVisibility::Collapsed }
        , _hallucinationVisibility{ WidgetVisibility::Collapsed }
        , _fightVisibility{ WidgetVisibility::Collapsed }
        , _winVisibility{ WidgetVisibility::Collapsed }
        , _loseVisibility{ WidgetVisibility::Collapsed }
    {
    }

    SurvivalHorrorHUDViewModel::~SurvivalHorrorHUDViewModel() = default;

    const TypeInfo* SurvivalHorrorHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void SurvivalHorrorHUDViewModel::setItems( int32 value )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _items, value );
    }

    void SurvivalHorrorHUDViewModel::setHealthRatio( float32 ratio )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _healthRatio, MathUtil::saturate( ratio ) );
    }

    void SurvivalHorrorHUDViewModel::setSanityRatio( float32 ratio )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _sanityRatio, MathUtil::saturate( ratio ) );
    }

    void SurvivalHorrorHUDViewModel::setBatteryRatio( float32 ratio )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _batteryRatio, MathUtil::saturate( ratio ) );
    }

    void SurvivalHorrorHUDViewModel::setDarkShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _darkVisibility, toHUDVisibility( bShown ) );
    }

    void SurvivalHorrorHUDViewModel::setHallucinationShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _hallucinationVisibility, toHUDVisibility( bShown ) );
    }

    void SurvivalHorrorHUDViewModel::setFightShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _fightVisibility, toHUDVisibility( bShown ) );
    }

    void SurvivalHorrorHUDViewModel::setWinShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _winVisibility, toHUDVisibility( bShown ) );
    }

    void SurvivalHorrorHUDViewModel::setLoseShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _loseVisibility, toHUDVisibility( bShown ) );
    }
} // namespace sw
