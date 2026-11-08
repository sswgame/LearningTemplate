#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureWorldMap.h"

#include "Core/Foundation/Constants.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureVitals.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"
#include "GameFramework/Runtime/World/Land/AreaGraph.h"

namespace sw
{
    AdventureWorldMap::AdventureWorldMap()
        : _catalog{}
        , _listActivated{}
        , _listCompleted{}
        , _orbCount{ 0 }
        , _orbsPerExchange{ 4 }
        , _completedShrineCount{ 0 }
    {
    }

    bool AdventureWorldMap::loadLandmark( const XMLNode& node, AdventureLandmarkKind kind, string_view sourceName )
    {
        AdventureLandmarkDef landmark;
        landmark._kind = kind;
        if ( XMLFields::readFields( node, landmark, sourceName ) == false )
            return false;
        if ( kind == AdventureLandmarkKind::Tower && landmark._region.empty() )
        {
            SW_LOG_ERROR_ID( "AdventureWorldMap.TowerNeedsRegionRegion", "%#: tower '%#' needs region=\"...\" (the region it reveals)", sourceName, landmark._id.c_str() );
            return false;
        }
        // `loadRoot` 가 먼저 비웠으니 이미 있으면 같은 글 안의 겹침이다. 받으면 뒤의 종류가 앞을 덮어(사당이 탑을) 그 표지가 말없이 사라진다.
        if ( _catalog.find( landmark._id ) != nullptr )
        {
            SW_LOG_ERROR_ID( "AdventureWorldMap.DuplicateLandmark", "%#: landmark id '%#' is used twice", sourceName, landmark._id.c_str() );
            return false;
        }
        (void)_catalog.add( landmark );
        return true;
    }

    bool AdventureWorldMap::loadTower( const XMLNode& node, string_view sourceName )
    {
        return loadLandmark( node, AdventureLandmarkKind::Tower, sourceName );
    }

    bool AdventureWorldMap::loadShrine( const XMLNode& node, string_view sourceName )
    {
        return loadLandmark( node, AdventureLandmarkKind::Shrine, sourceName );
    }

    uint32 AdventureWorldMap::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        AdventureWorldRootDef rootDef;
        if ( XMLFields::readFields( root, rootDef, sourceName ) == false )
            return 0;
        _catalog.clear();
        _orbsPerExchange = rootDef._orbsPerExchange;
        // 카탈로그 순서가 저장 번호이므로 탑을 먼저 다 읽고 사당을 읽는다.
        using Element                          = XMLCatalogElement<AdventureWorldMap>;
        static constexpr Element kArrElement[] = {
            Element::of<&AdventureWorldMap::loadTower>( "Tower" ),
            Element::of<&AdventureWorldMap::loadShrine>( "Shrine" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInTableOrder( *this, root, sourceName, kArrElement );
        resetState();
        return result._loadedCount;
    }

    void AdventureWorldMap::resetState()
    {
        _listActivated.assign( _catalog.getCount(), SW_FALSE );
        _listCompleted.assign( _catalog.getCount(), SW_FALSE );
        _orbCount             = 0;
        _completedShrineCount = 0;
    }

    void AdventureWorldMap::serializeState( StateIO& io )
    {
        io.value( _orbCount );
        io.check( 0 <= _orbCount );
        io.expect( static_cast<uint32>( _listActivated.size() ) );
        if ( io.isOk() == false )
            return;
        int32 completedCount = 0;
        for ( size_t index = 0; index < _listActivated.size(); ++index )
        {
            io.value( _listActivated[index] );
            io.value( _listCompleted[index] );
            if ( io.isWriting() )
                continue;
            // 마친 사당은 찾은 것이고, 사당만 마칠 수 있다
            const bool bShrine = _catalog.getAt( index )._kind == AdventureLandmarkKind::Shrine;
            if ( io.check( _listActivated[index] <= SW_TRUE && _listCompleted[index] <= SW_TRUE && ( _listCompleted[index] == SW_FALSE || ( bShrine && _listActivated[index] == SW_TRUE ) ) ) == false )
                return;
            completedCount += _listCompleted[index];
        }
        if ( io.isReading() )
        {
            _completedShrineCount = completedCount;
        }
    }

    int32 AdventureWorldMap::activateTower( const hashed_string& towerID, AreaGraph& areaGraph )
    {
        const optional<int32> found = _catalog.findIndex( towerID );
        if ( found.has_value() == false )
            return constant::invalid_index::kInt32;
        const int32 index                = *found;
        const bool  bTowerNotActivatable = _catalog.getAt( static_cast<size_t>( index ) )._kind != AdventureLandmarkKind::Tower || _listActivated[static_cast<size_t>( index )] == SW_TRUE;
        if ( bTowerNotActivatable )
            return constant::invalid_index::kInt32;
        _listActivated[static_cast<size_t>( index )] = SW_TRUE;
        return areaGraph.discoverRegion( _catalog.getAt( static_cast<size_t>( index ) )._region );
    }

    bool AdventureWorldMap::discoverShrine( const hashed_string& shrineID )
    {
        const optional<int32> found = _catalog.findIndex( shrineID );
        if ( found.has_value() == false )
            return false;
        const int32 index                 = *found;
        const bool  bShrineNotActivatable = _catalog.getAt( static_cast<size_t>( index ) )._kind != AdventureLandmarkKind::Shrine || _listActivated[static_cast<size_t>( index )] == SW_TRUE;
        if ( bShrineNotActivatable )
            return false;
        _listActivated[static_cast<size_t>( index )] = SW_TRUE;
        return true;
    }

    bool AdventureWorldMap::completeShrine( const hashed_string& shrineID )
    {
        const optional<int32> found = _catalog.findIndex( shrineID );
        if ( found.has_value() == false )
            return false;
        const int32 index                 = *found;
        const bool  bShrineNotCompletable = _catalog.getAt( static_cast<size_t>( index ) )._kind != AdventureLandmarkKind::Shrine || _listCompleted[static_cast<size_t>( index )] == SW_TRUE;
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
        const optional<int32> index = _catalog.findIndex( landmarkID );
        return index.has_value() && _listActivated[static_cast<size_t>( *index )] == SW_TRUE;
    }

    bool AdventureWorldMap::isCompleted( const hashed_string& landmarkID ) const
    {
        const optional<int32> index = _catalog.findIndex( landmarkID );
        return index.has_value() && _listCompleted[static_cast<size_t>( *index )] == SW_TRUE;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<AdventureWorldMap>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( AdventureWorldMap, AdventureWorldMap, SW_KIT_API, AdventureWorldMap::kAssetType.withLoader( AssetTypeLoader::of<AdventureWorldMap>( MemoryTag::Game ) ) );
} // namespace sw
