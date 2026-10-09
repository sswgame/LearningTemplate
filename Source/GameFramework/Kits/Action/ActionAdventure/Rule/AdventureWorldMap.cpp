#include "pch.h"

#include "GameFramework/Kits/Action/ActionAdventure/Rule/AdventureWorldMap.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"
#include "GameFramework/Base/World/Land/AreaGraph.h"
#include "GameFramework/Kits/Action/ActionAdventure/Rule/AdventureVitals.h"

namespace sw
{
    SW_LOG_CALLER( "AdventureWorldMap" );

    const utf8* toString( AdventureExchangeResult result )
    {
        switch ( result )
        {
            case AdventureExchangeResult::Ok:
                return "Ok";
            case AdventureExchangeResult::NotEnoughOrbs:
                return "NotEnoughOrbs";
            case AdventureExchangeResult::AtLimit:
                return "AtLimit";
        }
        return "Unknown";
    }

    AdventureWorldMap::AdventureWorldMap()
        : _catalog{}
        , _listActivated{}
        , _listCompleted{}
        , _orbCount{ 0 }
        , _orbsPerExchange{ 4 }
        , _completedShrineCount{ 0 }
    {
    }

    void AdventureWorldMap::readLandmarks( const XmlNode& root, const utf8* pNodeName, AdventureLandmarkKind kind, string_view sourceName, uint32& inoutCount )
    {
        for ( XmlNode node = root.findChild( pNodeName ); node; node = node.findNextSibling( pNodeName ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            AdventureLandmarkDef landmark;
            landmark._id        = hashed_string( pId );
            landmark._kind      = kind;
            const utf8* pRegion = node.findAttribute( "region" );
            landmark._region    = pRegion != nullptr ? hashed_string( pRegion ) : hashed_string{};
            if ( kind == AdventureLandmarkKind::Tower && landmark._region.empty() )
                SW_LOG_WARNING( "%#: tower '%#' has no region - it reveals nothing", sourceName, pId );
            (void)_catalog.add( landmark );
            ++inoutCount;
        }
    }

    uint32 AdventureWorldMap::loadRoot( const XmlNode& root, string_view sourceName )
    {
        _catalog.clear();
        _orbsPerExchange   = MathUtil::max( 1, root.getAttributeInt( "orbsPerExchange", 4 ) );
        uint32 loadedCount = 0;
        readLandmarks( root, "Tower", AdventureLandmarkKind::Tower, sourceName, loadedCount );
        readLandmarks( root, "Shrine", AdventureLandmarkKind::Shrine, sourceName, loadedCount );
        resetState();
        return loadedCount;
    }

    void AdventureWorldMap::resetState()
    {
        _listActivated.assign( _catalog.getCount(), SW_FALSE );
        _listCompleted.assign( _catalog.getCount(), SW_FALSE );
        _orbCount             = 0;
        _completedShrineCount = 0;
    }

    int32 AdventureWorldMap::activateTower( const hashed_string& towerId, AreaGraph& areaGraph )
    {
        const int32 index = _catalog.findIndex( towerId );
        if ( index < 0 || _catalog.getAt( static_cast<size_t>( index ) )._kind != AdventureLandmarkKind::Tower || _listActivated[static_cast<size_t>( index )] == SW_TRUE )
            return -1;
        _listActivated[static_cast<size_t>( index )] = SW_TRUE;
        return areaGraph.discoverRegion( _catalog.getAt( static_cast<size_t>( index ) )._region );
    }

    bool AdventureWorldMap::discoverShrine( const hashed_string& shrineId )
    {
        const int32 index = _catalog.findIndex( shrineId );
        if ( index < 0 || _catalog.getAt( static_cast<size_t>( index ) )._kind != AdventureLandmarkKind::Shrine || _listActivated[static_cast<size_t>( index )] == SW_TRUE )
            return false;
        _listActivated[static_cast<size_t>( index )] = SW_TRUE;
        return true;
    }

    bool AdventureWorldMap::completeShrine( const hashed_string& shrineId )
    {
        const int32 index = _catalog.findIndex( shrineId );
        if ( index < 0 || _catalog.getAt( static_cast<size_t>( index ) )._kind != AdventureLandmarkKind::Shrine || _listCompleted[static_cast<size_t>( index )] == SW_TRUE )
            return false;
        _listActivated[static_cast<size_t>( index )] = SW_TRUE;
        _listCompleted[static_cast<size_t>( index )] = SW_TRUE;
        ++_orbCount;
        ++_completedShrineCount;
        return true;
    }

    AdventureExchangeResult AdventureWorldMap::exchangeOrbs( AdventureOrbReward reward, AdventureVitals& vitals )
    {
        if ( _orbCount < _orbsPerExchange )
            return AdventureExchangeResult::NotEnoughOrbs;
        const bool bGiven = reward == AdventureOrbReward::HeartContainer ? vitals.addHeartContainer() : vitals.addStaminaVessel();
        if ( bGiven == false )
            return AdventureExchangeResult::AtLimit;
        _orbCount -= _orbsPerExchange;
        return AdventureExchangeResult::Ok;
    }

    bool AdventureWorldMap::canWarpTo( const hashed_string& landmarkId ) const { return isActivated( landmarkId ); }

    bool AdventureWorldMap::isActivated( const hashed_string& landmarkId ) const
    {
        const int32 index = _catalog.findIndex( landmarkId );
        return index >= 0 && _listActivated[static_cast<size_t>( index )] == SW_TRUE;
    }

    bool AdventureWorldMap::isCompleted( const hashed_string& landmarkId ) const
    {
        const int32 index = _catalog.findIndex( landmarkId );
        return index >= 0 && _listCompleted[static_cast<size_t>( index )] == SW_TRUE;
    }
} // namespace sw
