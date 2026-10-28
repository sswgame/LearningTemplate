#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureWorldMap.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Base/World/Land/AreaGraph.h"
#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureVitals.h"

namespace sw
{
    SW_LOG_CALLER( "AdventureWorldMap" );

    AdventureWorldMap::AdventureWorldMap()
        : _catalog{}
        , _listActivated{}
        , _listCompleted{}
        , _orbCount{ 0 }
        , _orbsPerExchange{ 4 }
        , _completedShrineCount{ 0 }
    {
    }

    bool AdventureWorldMap::readLandmarks( const XMLNode& root, const utf8* pNodeName, AdventureLandmarkKind kind, string_view sourceName, uint32& inoutCount )
    {
        bool bValid = true;
        for ( XMLNode node = root.findChild( pNodeName ); node; node = node.findNextSibling( pNodeName ) )
        {
            AdventureLandmarkDef landmark;
            landmark._kind = kind;
            if ( GameDataXML::readFields( node, landmark, sourceName ) == false )
            {
                bValid = false;
                continue;
            }
            if ( kind == AdventureLandmarkKind::Tower && landmark._region.empty() )
            {
                SW_LOG_ERROR( "%#: tower '%#' needs region=\"...\" (the region it reveals)", sourceName, landmark._id.c_str() );
                bValid = false;
                continue;
            }
            (void)_catalog.add( landmark );
            ++inoutCount;
        }
        return bValid;
    }

    uint32 AdventureWorldMap::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        AdventureWorldRootDef rootDef;
        if ( GameDataXML::readFields( root, rootDef, sourceName ) == false )
            return 0;
        _catalog.clear();
        _orbsPerExchange       = rootDef._orbsPerExchange;
        uint32     loadedCount = 0;
        const bool bTowers     = readLandmarks( root, "Tower", AdventureLandmarkKind::Tower, sourceName, loadedCount );
        const bool bShrines    = readLandmarks( root, "Shrine", AdventureLandmarkKind::Shrine, sourceName, loadedCount );
        if ( bTowers == false || bShrines == false )
            SW_LOG_ERROR( "%#: some landmarks were not read (listed above)", sourceName );
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

    int32 AdventureWorldMap::activateTower( const hashed_string& towerID, AreaGraph& areaGraph )
    {
        const int32 index                = _catalog.findIndex( towerID );
        const bool  bTowerNotActivatable = index < 0 || _catalog.getAt( static_cast<size_t>( index ) )._kind != AdventureLandmarkKind::Tower || _listActivated[static_cast<size_t>( index )] == SW_TRUE;
        if ( bTowerNotActivatable )
            return -1;
        _listActivated[static_cast<size_t>( index )] = SW_TRUE;
        return areaGraph.discoverRegion( _catalog.getAt( static_cast<size_t>( index ) )._region );
    }

    bool AdventureWorldMap::discoverShrine( const hashed_string& shrineID )
    {
        const int32 index                 = _catalog.findIndex( shrineID );
        const bool  bShrineNotActivatable = index < 0 || _catalog.getAt( static_cast<size_t>( index ) )._kind != AdventureLandmarkKind::Shrine || _listActivated[static_cast<size_t>( index )] == SW_TRUE;
        if ( bShrineNotActivatable )
            return false;
        _listActivated[static_cast<size_t>( index )] = SW_TRUE;
        return true;
    }

    bool AdventureWorldMap::completeShrine( const hashed_string& shrineID )
    {
        const int32 index                 = _catalog.findIndex( shrineID );
        const bool  bShrineNotCompletable = index < 0 || _catalog.getAt( static_cast<size_t>( index ) )._kind != AdventureLandmarkKind::Shrine || _listCompleted[static_cast<size_t>( index )] == SW_TRUE;
        if ( bShrineNotCompletable )
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

    bool AdventureWorldMap::canWarpTo( const hashed_string& landmarkID ) const
    {
        return isActivated( landmarkID );
    }

    bool AdventureWorldMap::isActivated( const hashed_string& landmarkID ) const
    {
        const int32 index = _catalog.findIndex( landmarkID );
        return index >= 0 && _listActivated[static_cast<size_t>( index )] == SW_TRUE;
    }

    bool AdventureWorldMap::isCompleted( const hashed_string& landmarkID ) const
    {
        const int32 index = _catalog.findIndex( landmarkID );
        return index >= 0 && _listCompleted[static_cast<size_t>( index )] == SW_TRUE;
    }
} // namespace sw
