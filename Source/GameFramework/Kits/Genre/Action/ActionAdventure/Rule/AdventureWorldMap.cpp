#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureWorldMap.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureVitals.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
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

    bool AdventureWorldMap::readLandmarks( const XMLNode& root, const utf8* pNodeName, AdventureLandmarkKind kind, string_view sourceName, uint32& inoutCount )
    {
        bool bValid = true;
        for ( XMLNode node = root.findChild( pNodeName ); node; node = node.findNextSibling( pNodeName ) )
        {
            AdventureLandmarkDef landmark;
            landmark._kind = kind;
            if ( XMLFields::readFields( node, landmark, sourceName ) == false )
            {
                bValid = false;
                continue;
            }
            if ( kind == AdventureLandmarkKind::Tower && landmark._region.empty() )
            {
                SW_LOG_ERROR_ID( "AdventureWorldMap.TowerNeedsRegionRegion", "%#: tower '%#' needs region=\"...\" (the region it reveals)", sourceName, landmark._id.c_str() );
                bValid = false;
                continue;
            }
            // `loadRoot` 가 먼저 비웠으니 이미 있으면 같은 글 안의 겹침이다. 받으면 뒤의 종류가 앞을 덮어(사당이 탑을) 그 표지가 말없이 사라진다.
            if ( _catalog.find( landmark._id ) != nullptr )
            {
                SW_LOG_ERROR_ID( "AdventureWorldMap.DuplicateLandmark", "%#: landmark id '%#' is used twice", sourceName, landmark._id.c_str() );
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
        if ( XMLFields::readFields( root, rootDef, sourceName ) == false )
            return 0;
        _catalog.clear();
        _orbsPerExchange       = rootDef._orbsPerExchange;
        uint32     loadedCount = 0;
        const bool bTowers     = readLandmarks( root, "Tower", AdventureLandmarkKind::Tower, sourceName, loadedCount );
        const bool bShrines    = readLandmarks( root, "Shrine", AdventureLandmarkKind::Shrine, sourceName, loadedCount );
        if ( bTowers == false || bShrines == false )
            SW_LOG_ERROR_ID( "AdventureWorldMap.SomeLandmarksNotRead", "%#: some landmarks were not read (listed above)", sourceName );
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

    void AdventureWorldMap::writeState( Archive& outArchive ) const
    {
        outArchive << _orbCount;
        outArchive << static_cast<uint32>( _listActivated.size() );
        for ( size_t index = 0; index < _listActivated.size(); ++index )
        {
            outArchive << _listActivated[index];
            outArchive << _listCompleted[index];
        }
    }

    bool AdventureWorldMap::readState( Archive& archive )
    {
        int32  orbCount = 0;
        uint32 count    = 0;
        archive >> orbCount;
        if ( archive.isError() || orbCount < 0 || archive.readCount( 2, count ) == false || count != _listActivated.size() )
            return false;
        vector<uint8> listActivated( count, SW_FALSE );
        vector<uint8> listCompleted( count, SW_FALSE );
        int32         completedCount = 0;
        for ( uint32 index = 0; index < count; ++index )
        {
            archive >> listActivated[index];
            archive >> listCompleted[index];
            // 마친 사당은 찾은 것이고, 사당만 마칠 수 있다
            const bool bShrine = _catalog.getAt( index )._kind == AdventureLandmarkKind::Shrine;
            const bool bValid  = archive.isError() == false && listActivated[index] <= SW_TRUE && listCompleted[index] <= SW_TRUE &&
                                ( listCompleted[index] == SW_FALSE || ( bShrine && listActivated[index] == SW_TRUE ) );
            if ( bValid == false )
                return false;
            completedCount += listCompleted[index];
        }
        _listActivated        = std::move( listActivated );
        _listCompleted        = std::move( listCompleted );
        _orbCount             = orbCount;
        _completedShrineCount = completedCount;
        return true;
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

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<AdventureWorldMap>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( AdventureWorldMap, AdventureWorldMap, SW_KIT_API, AdventureWorldMap::kAssetType.withLoader( AssetTypeLoader::of<AdventureWorldMap>( MemoryTag::Game ) ) );
} // namespace sw
