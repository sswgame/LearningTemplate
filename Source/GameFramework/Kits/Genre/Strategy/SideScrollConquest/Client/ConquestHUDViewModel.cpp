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

    WidgetVisibility ConquestHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }

    void ConquestHUDViewModel::setTerritory( int32 territory )
    {
        (void)setField( _territory, territory, "_territory" );
    }

    void ConquestHUDViewModel::setSiteCount( int32 siteCount )
    {
        (void)setField( _siteCount, siteCount, "_siteCount" );
    }

    void ConquestHUDViewModel::setSquad( int32 squad )
    {
        (void)setField( _squad, squad, "_squad" );
    }

    void ConquestHUDViewModel::setPopulation( int32 population )
    {
        (void)setField( _population, population, "_population" );
    }

    void ConquestHUDViewModel::setPopulationCap( int32 populationCap )
    {
        (void)setField( _populationCap, populationCap, "_populationCap" );
    }

    void ConquestHUDViewModel::setResource( int32 resource )
    {
        (void)setField( _resource, resource, "_resource" );
    }

    void ConquestHUDViewModel::setHealthRatio( float32 healthRatio )
    {
        (void)setField( _healthRatio, MathUtil::clamp( healthRatio, 0.0f, 1.0f ), "_healthRatio" );
    }

    void ConquestHUDViewModel::setWonShown( bool bShown )
    {
        (void)setField( _wonVisibility, toVisibility( bShown ), "_wonVisibility" );
    }

    void ConquestHUDViewModel::setLostShown( bool bShown )
    {
        (void)setField( _lostVisibility, toVisibility( bShown ), "_lostVisibility" );
    }
} // namespace sw
