#include "pch.h"

#include "GameFramework/Kits/Action/Metroidvania/MetroMapState.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/World/Land/AreaGraph.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroidvaniaCatalog.h"

namespace sw
{
    MetroMapState::MetroMapState()
        : _pCatalog{ nullptr }
        , _pGraph{ nullptr }
        , _listRegionMap{}
        , _listSite{}
        , _listPickup{}
    {
    }

    void MetroMapState::initialize( const MetroidvaniaCatalog* pCatalog, AreaGraph* pGraph )
    {
        _pCatalog = pCatalog;
        _pGraph   = pGraph;
        _listRegionMap.clear();
        _listSite.clear();
        _listPickup.clear();
    }

    bool MetroMapState::enterArea( const hashed_string& areaId ) { return _pGraph != nullptr && _pGraph->enterArea( areaId ); }

    MetroMapPurchase MetroMapState::buyRegionMap( const hashed_string& region, Wallet& inoutWallet, const hashed_string& currency )
    {
        if ( _pCatalog == nullptr || _pGraph == nullptr )
            return MetroMapPurchase::UnknownRegion;
        const MetroRegionMapDef* pMap = _pCatalog->findRegionMap( region );
        if ( pMap == nullptr )
            return MetroMapPurchase::UnknownRegion;
        if ( hasRegionMap( region ) )
            return MetroMapPurchase::AlreadyOwned;
        if ( inoutWallet.trySpend( currency, pMap->_price ) == false )
            return MetroMapPurchase::NotEnoughCurrency;
        _listRegionMap.push_back( pMap->_id );
        (void)_pGraph->discoverRegion( pMap->_id );
        return MetroMapPurchase::Bought;
    }

    bool MetroMapState::hasRegionMap( const hashed_string& region ) const { return contains( _listRegionMap, region ); }

    bool MetroMapState::isShownOnMap( const hashed_string& areaId ) const
    {
        if ( _pGraph == nullptr )
            return false;
        const AreaDef* pArea = _pGraph->findArea( areaId );
        return pArea != nullptr && hasRegionMap( pArea->_region ) && _pGraph->isDiscovered( areaId );
    }

    const MetroPickupDef* MetroMapState::collectPickup( const hashed_string& pickupId )
    {
        if ( _pCatalog == nullptr || isCollected( pickupId ) )
            return nullptr;
        const MetroPickupDef* pPickup = _pCatalog->findPickup( pickupId );
        if ( pPickup == nullptr )
            return nullptr;
        _listPickup.push_back( pPickup->_id );
        return pPickup;
    }

    bool MetroMapState::isCollected( const hashed_string& pickupId ) const { return contains( _listPickup, pickupId ); }

    void MetroMapState::collectItemMarkers( vector<const MetroPickupDef*>& outListPickup ) const
    {
        outListPickup.clear();
        if ( _pCatalog == nullptr || _pGraph == nullptr )
            return;
        for ( const MetroPickupDef& pickup : _pCatalog->getPickups() )
        {
            if ( isCollected( pickup._id ) || _pGraph->isVisited( pickup._area ) || isShownOnMap( pickup._area ) == false )
                continue;
            outListPickup.push_back( &pickup );
        }
    }

    bool MetroMapState::activateSite( const hashed_string& siteId )
    {
        if ( _pCatalog == nullptr || _pGraph == nullptr || isSiteActive( siteId ) )
            return false;
        const MetroSiteDef* pSite = _pCatalog->findSite( siteId );
        if ( pSite == nullptr || _pGraph->isVisited( pSite->_area ) == false )
            return false;
        _listSite.push_back( pSite->_id );
        return true;
    }

    bool MetroMapState::isSiteActive( const hashed_string& siteId ) const { return contains( _listSite, siteId ); }

    bool MetroMapState::canFastTravel( const hashed_string& fromSiteId, const hashed_string& toSiteId ) const
    {
        if ( _pCatalog == nullptr || fromSiteId == toSiteId )
            return false;
        const MetroSiteDef* pFrom = _pCatalog->findSite( fromSiteId );
        const MetroSiteDef* pTo   = _pCatalog->findSite( toSiteId );
        if ( pFrom == nullptr || pTo == nullptr || pFrom->_bFastTravel == SW_FALSE || pTo->_bFastTravel == SW_FALSE )
            return false;
        return isSiteActive( fromSiteId ) && isSiteActive( toSiteId );
    }

    float32 MetroMapState::computeExplorationRatio() const { return _pGraph != nullptr ? _pGraph->computeExplorationRatio() : 0.0f; }

    float32 MetroMapState::computeCollectionRatio() const
    {
        if ( _pCatalog == nullptr || _pCatalog->getPickups().empty() )
            return 1.0f;
        return static_cast<float32>( _listPickup.size() ) / static_cast<float32>( _pCatalog->getPickups().size() );
    }

    float32 MetroMapState::computeCompletionPercent() const
    {
        if ( _pCatalog == nullptr || _pGraph == nullptr )
            return 0.0f;
        const size_t total = _pGraph->getAreas().size() + _pCatalog->getPickups().size();
        if ( total == 0 )
            return 100.0f;
        const size_t done = static_cast<size_t>( _pGraph->getVisitedCount() ) + _listPickup.size();
        return 100.0f * static_cast<float32>( done ) / static_cast<float32>( total );
    }

    void MetroMapState::fillSaveState( vector<hashed_string>& outListRegionMap, vector<hashed_string>& outListSite, vector<hashed_string>& outListPickup ) const
    {
        outListRegionMap.clear();
        outListSite.clear();
        outListPickup.clear();
        if ( _pCatalog == nullptr )
            return;
        // 얻은 순서가 아니라 카탈로그 순서로 — 같은 진행이면 늘 같은 세이브.
        for ( const MetroRegionMapDef& regionMap : _pCatalog->getRegionMaps() )
        {
            if ( hasRegionMap( regionMap._id ) )
                outListRegionMap.push_back( regionMap._id );
        }
        for ( const MetroSiteDef& site : _pCatalog->getSites() )
        {
            if ( isSiteActive( site._id ) )
                outListSite.push_back( site._id );
        }
        for ( const MetroPickupDef& pickup : _pCatalog->getPickups() )
        {
            if ( isCollected( pickup._id ) )
                outListPickup.push_back( pickup._id );
        }
    }

    void MetroMapState::restoreSaveState( const vector<hashed_string>& listRegionMap, const vector<hashed_string>& listSite, const vector<hashed_string>& listPickup )
    {
        _listRegionMap.clear();
        _listSite.clear();
        _listPickup.clear();
        if ( _pCatalog == nullptr )
            return;
        for ( const hashed_string& region : listRegionMap )
        {
            if ( _pCatalog->findRegionMap( region ) == nullptr || hasRegionMap( region ) )
                continue;
            _listRegionMap.push_back( region );
            if ( _pGraph != nullptr )
                (void)_pGraph->discoverRegion( region );
        }
        for ( const hashed_string& siteId : listSite )
        {
            if ( _pCatalog->findSite( siteId ) != nullptr && isSiteActive( siteId ) == false )
                _listSite.push_back( siteId );
        }
        for ( const hashed_string& pickupId : listPickup )
        {
            if ( _pCatalog->findPickup( pickupId ) != nullptr && isCollected( pickupId ) == false )
                _listPickup.push_back( pickupId );
        }
    }

    bool MetroMapState::contains( const vector<hashed_string>& listId, const hashed_string& id )
    {
        for ( const hashed_string& entry : listId )
        {
            if ( entry == id )
                return true;
        }
        return false;
    }

    void MetroMapState::writeState( Archive& outArchive ) const
    {
        for ( const vector<hashed_string>* pListId : { &_listRegionMap, &_listSite, &_listPickup } )
        {
            outArchive << static_cast<uint32>( pListId->size() );
            for ( const hashed_string& id : *pListId )
            {
                StateArchiveUtil::writeName( outArchive, id );
            }
        }
    }

    bool MetroMapState::readState( Archive& archive )
    {
        if ( _pCatalog == nullptr )
            return false;
        // 산 지도 · 연 지점 · 주운 것 순서 — 모두 카탈로그에 있고 겹치지 않아야 한다
        vector<hashed_string> listRegionMap;
        vector<hashed_string> listSite;
        vector<hashed_string> listPickup;
        for ( int32 listIndex = 0; listIndex < 3; ++listIndex )
        {
            vector<hashed_string>& listId  = listIndex == 0 ? listRegionMap : ( listIndex == 1 ? listSite : listPickup );
            uint32                 idCount = 0;
            if ( StateArchiveUtil::readCount( archive, 4, idCount ) == false )
                return false;
            listId.reserve( idCount );
            for ( uint32 entry = 0; entry < idCount; ++entry )
            {
                hashed_string id;
                if ( StateArchiveUtil::readName( archive, id ) == false )
                    return false;
                bool bKnown = false;
                if ( listIndex == 0 )
                    bKnown = _pCatalog->findRegionMap( id ) != nullptr;
                else if ( listIndex == 1 )
                    bKnown = _pCatalog->findSite( id ) != nullptr;
                else
                    bKnown = _pCatalog->findPickup( id ) != nullptr;
                if ( bKnown == false || contains( listId, id ) )
                    return false;
                listId.push_back( id );
            }
        }
        restoreSaveState( listRegionMap, listSite, listPickup );
        return true;
    }
} // namespace sw
