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
        (void)SW_SET_VIEW_MODEL_FIELD( _remainingGenerators, value );
    }

    void AsymmetricHorrorHUDViewModel::setActivityRatio( float32 ratio )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _activityRatio, MathUtil::saturate( ratio ) );
    }

    void AsymmetricHorrorHUDViewModel::setHeartbeat( float32 ratio )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _heartbeat, MathUtil::saturate( ratio ) );
    }

    void AsymmetricHorrorHUDViewModel::setSkillCheckShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _skillCheckVisibility, toHUDVisibility( bShown ) );
    }

    void AsymmetricHorrorHUDViewModel::setWinShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _winVisibility, toHUDVisibility( bShown ) );
    }

    void AsymmetricHorrorHUDViewModel::setDrawShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _drawVisibility, toHUDVisibility( bShown ) );
    }

    void AsymmetricHorrorHUDViewModel::setLoseShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _loseVisibility, toHUDVisibility( bShown ) );
    }
} // namespace sw
