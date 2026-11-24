#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/Client/AsymmetricHorrorHUDViewModel.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    AsymmetricHorrorHUDViewModel::AsymmetricHorrorHUDViewModel()
        : UIViewModel{}
        , _remainingGenerators{ 0 }
        , _activityRatio{ 0.0f }
        , _heartbeat{ 0.0f }
        , _skillCheckVisibility{ WidgetVisibility::Collapsed }
        , _winVisibility{ WidgetVisibility::Collapsed }
        , _drawVisibility{ WidgetVisibility::Collapsed }
        , _loseVisibility{ WidgetVisibility::Collapsed }
    {
    }

    AsymmetricHorrorHUDViewModel::~AsymmetricHorrorHUDViewModel() = default;

    const TypeInfo* AsymmetricHorrorHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void AsymmetricHorrorHUDViewModel::setRemainingGenerators( int32 value )
    {
        (void)setField( _remainingGenerators, value, "_remainingGenerators" );
    }

    void AsymmetricHorrorHUDViewModel::setActivityRatio( float32 ratio )
    {
        (void)setField( _activityRatio, MathUtil::saturate( ratio ), "_activityRatio" );
    }

    void AsymmetricHorrorHUDViewModel::setHeartbeat( float32 ratio )
    {
        (void)setField( _heartbeat, MathUtil::saturate( ratio ), "_heartbeat" );
    }

    void AsymmetricHorrorHUDViewModel::setSkillCheckShown( bool bShown )
    {
        (void)setField( _skillCheckVisibility, toVisibility( bShown ), "_skillCheckVisibility" );
    }

    void AsymmetricHorrorHUDViewModel::setWinShown( bool bShown )
    {
        (void)setField( _winVisibility, toVisibility( bShown ), "_winVisibility" );
    }

    void AsymmetricHorrorHUDViewModel::setDrawShown( bool bShown )
    {
        (void)setField( _drawVisibility, toVisibility( bShown ), "_drawVisibility" );
    }

    void AsymmetricHorrorHUDViewModel::setLoseShown( bool bShown )
    {
        (void)setField( _loseVisibility, toVisibility( bShown ), "_loseVisibility" );
    }

    WidgetVisibility AsymmetricHorrorHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }
} // namespace sw
