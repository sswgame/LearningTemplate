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
        (void)setField( _items, value, "_items" );
    }

    void SurvivalHorrorHUDViewModel::setHealthRatio( float32 ratio )
    {
        (void)setField( _healthRatio, MathUtil::saturate( ratio ), "_healthRatio" );
    }

    void SurvivalHorrorHUDViewModel::setSanityRatio( float32 ratio )
    {
        (void)setField( _sanityRatio, MathUtil::saturate( ratio ), "_sanityRatio" );
    }

    void SurvivalHorrorHUDViewModel::setBatteryRatio( float32 ratio )
    {
        (void)setField( _batteryRatio, MathUtil::saturate( ratio ), "_batteryRatio" );
    }

    void SurvivalHorrorHUDViewModel::setDarkShown( bool bShown )
    {
        (void)setField( _darkVisibility, toVisibility( bShown ), "_darkVisibility" );
    }

    void SurvivalHorrorHUDViewModel::setHallucinationShown( bool bShown )
    {
        (void)setField( _hallucinationVisibility, toVisibility( bShown ), "_hallucinationVisibility" );
    }

    void SurvivalHorrorHUDViewModel::setFightShown( bool bShown )
    {
        (void)setField( _fightVisibility, toVisibility( bShown ), "_fightVisibility" );
    }

    void SurvivalHorrorHUDViewModel::setWinShown( bool bShown )
    {
        (void)setField( _winVisibility, toVisibility( bShown ), "_winVisibility" );
    }

    void SurvivalHorrorHUDViewModel::setLoseShown( bool bShown )
    {
        (void)setField( _loseVisibility, toVisibility( bShown ), "_loseVisibility" );
    }

    WidgetVisibility SurvivalHorrorHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }
} // namespace sw
