#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/Client/ConquestHUDViewModel.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    ConquestHUDViewModel::ConquestHUDViewModel()
        : UIViewModel{}
        , _territory{ 0 }
        , _siteCount{ 0 }
        , _squad{ 0 }
        , _population{ 0 }
        , _populationCap{ 0 }
        , _resource{ 0 }
        , _healthRatio{ 0.0f }
        , _wonVisibility{ WidgetVisibility::Collapsed }
        , _lostVisibility{ WidgetVisibility::Collapsed }
    {
    }

    ConquestHUDViewModel::~ConquestHUDViewModel() = default;

    const TypeInfo* ConquestHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void ConquestHUDViewModel::setTerritory( int32 territory )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _territory, territory );
    }

    void ConquestHUDViewModel::setSiteCount( int32 siteCount )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _siteCount, siteCount );
    }

    void ConquestHUDViewModel::setSquad( int32 squad )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _squad, squad );
    }

    void ConquestHUDViewModel::setPopulation( int32 population )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _population, population );
    }

    void ConquestHUDViewModel::setPopulationCap( int32 populationCap )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _populationCap, populationCap );
    }

    void ConquestHUDViewModel::setResource( int32 resource )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _resource, resource );
    }

    void ConquestHUDViewModel::setHealthRatio( float32 healthRatio )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _healthRatio, MathUtil::clamp( healthRatio, 0.0f, 1.0f ) );
    }

    void ConquestHUDViewModel::setWonShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _wonVisibility, toHUDVisibility( bShown ) );
    }

    void ConquestHUDViewModel::setLostShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _lostVisibility, toHUDVisibility( bShown ) );
    }
} // namespace sw
