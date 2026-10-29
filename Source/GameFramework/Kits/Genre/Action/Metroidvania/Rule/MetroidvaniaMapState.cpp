#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroidvaniaMapState.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/World/Land/AreaGraph.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/Catalog/MetroidvaniaCatalog.h"

namespace sw
{
    MetroidvaniaMapState::MetroidvaniaMapState()
        : _pCatalog{ nullptr }
        , _pGraph{ nullptr }
        , _listRegionMap{}
        , _listSite{}
        , _listPickup{}
    {
    }

    void MetroidvaniaMapState::initialize( const MetroidvaniaCatalog* pCatalog, AreaGraph* pGraph )
    {
        _pCatalog = pCatalog;
        _pGraph   = pGraph;
        _listRegionMap.clear();
        _listSite.clear();
        _listPickup.clear();
    }

    bool MetroidvaniaMapState::enterArea( const hashed_string& areaID )
    {
        return _pGraph != nullptr && _pGraph->enterArea( areaID );
    }

    MetroidvaniaMapPurchase MetroidvaniaMapState::buyRegionMap( const hashed_string& region, Wallet& inoutWallet, const hashed_string& currency )
    {
        if ( _pCatalog == nullptr || _pGraph == nullptr )
            return MetroidvaniaMapPurchase::UnknownRegion;
        const MetroidvaniaRegionMapDef* pMap = _pCatalog->findRegionMap( region );
        if ( pMap == nullptr )
            return MetroidvaniaMapPurchase::UnknownRegion;
        if ( hasRegionMap( region ) )
            return MetroidvaniaMapPurchase::AlreadyOwned;
        if ( inoutWallet.trySpend( currency, pMap->_price ) == false )
            return MetroidvaniaMapPurchase::NotEnoughCurrency;
        _listRegionMap.push_back( pMap->_id );
        (void)_pGraph->discoverRegion( pMap->_id );
        return MetroidvaniaMapPurchase::Bought;
    }

    bool MetroidvaniaMapState::hasRegionMap( const hashed_string& region ) const
    {
        return VectorUtil::contains( _listRegionMap, region );
    }

    bool MetroidvaniaMapState::isShownOnMap( const hashed_string& areaID ) const
    {
        if ( _pGraph == nullptr )
            return false;
        const AreaDef* pArea = _pGraph->findArea( areaID );
        return pArea != nullptr && hasRegionMap( pArea->_region ) && _pGraph->isDiscovered( areaID );
    }

    const MetroidvaniaPickupDef* MetroidvaniaMapState::collectPickup( const hashed_string& pickupID )
    {
        if ( _pCatalog == nullptr || isCollected( pickupID ) )
            return nullptr;
        const MetroidvaniaPickupDef* pPickup = _pCatalog->findPickup( pickupID );
        if ( pPickup == nullptr )
            return nullptr;
        _listPickup.push_back( pPickup->_id );
        return pPickup;
    }

    bool MetroidvaniaMapState::isCollected( const hashed_string& pickupID ) const
    {
        return VectorUtil::contains( _listPickup, pickupID );
    }

    void MetroidvaniaMapState::collectItemMarkers( vector<const MetroidvaniaPickupDef*>& outListPickup ) const
    {
        outListPickup.clear();
        if ( _pCatalog == nullptr || _pGraph == nullptr )
            return;
        for ( const MetroidvaniaPickupDef& pickup : _pCatalog->getPickups() )
        {
            const bool bPickupHidden = isCollected( pickup._id ) || _pGraph->isVisited( pickup._area ) || isShownOnMap( pickup._area ) == false;
            if ( bPickupHidden )
                continue;
            outListPickup.push_back( &pickup );
        }
    }

    bool MetroidvaniaMapState::activateSite( const hashed_string& siteID )
    {
        const bool bCannotActivateSite = _pCatalog == nullptr || _pGraph == nullptr || isSiteActive( siteID );
        if ( bCannotActivateSite )
            return false;
        const MetroidvaniaSiteDef* pSite = _pCatalog->findSite( siteID );
        if ( pSite == nullptr || _pGraph->isVisited( pSite->_area ) == false )
            return false;
        _listSite.push_back( pSite->_id );
        return true;
    }

    bool MetroidvaniaMapState::isSiteActive( const hashed_string& siteID ) const
    {
        return VectorUtil::contains( _listSite, siteID );
    }

    bool MetroidvaniaMapState::canFastTravel( const hashed_string& fromSiteID, const hashed_string& toSiteID ) const
    {
        if ( _pCatalog == nullptr || fromSiteID == toSiteID )
            return false;
        const MetroidvaniaSiteDef* pFrom                  = _pCatalog->findSite( fromSiteID );
        const MetroidvaniaSiteDef* pTo                    = _pCatalog->findSite( toSiteID );
        const bool                 bFastTravelUnavailable = pFrom == nullptr || pTo == nullptr || pFrom->_bFastTravel == false || pTo->_bFastTravel == false;
        if ( bFastTravelUnavailable )
            return false;
        return isSiteActive( fromSiteID ) && isSiteActive( toSiteID );
    }

    float32 MetroidvaniaMapState::computeExplorationRatio() const
    {
        return _pGraph != nullptr ? _pGraph->computeExplorationRatio() : 0.0f;
    }

    float32 MetroidvaniaMapState::computeCollectionRatio() const
    {
        if ( _pCatalog == nullptr || _pCatalog->getPickups().empty() )
            return 1.0f;
        return static_cast<float32>( _listPickup.size() ) / static_cast<float32>( _pCatalog->getPickups().size() );
    }

    float32 MetroidvaniaMapState::computeCompletionPercent() const
    {
        if ( _pCatalog == nullptr || _pGraph == nullptr )
            return 0.0f;
        const size_t total = _pGraph->getAreas().size() + _pCatalog->getPickups().size();
        if ( total == 0 )
            return 100.0f;
        const size_t done = static_cast<size_t>( _pGraph->getVisitedCount() ) + _listPickup.size();
        return 100.0f * static_cast<float32>( done ) / static_cast<float32>( total );
    }

    void MetroidvaniaMapState::fillSaveState( vector<hashed_string>& outListRegionMap, vector<hashed_string>& outListSite, vector<hashed_string>& outListPickup ) const
    {
        outListRegionMap.clear();
        outListSite.clear();
        outListPickup.clear();
        if ( _pCatalog == nullptr )
            return;
        // 얻은 순서가 아니라 카탈로그 순서로 쓴다. 그래야 같은 진행이면 항상 같은 세이브가 나온다.
        for ( const MetroidvaniaRegionMapDef& regionMap : _pCatalog->getRegionMaps() )
        {
            if ( hasRegionMap( regionMap._id ) )
                outListRegionMap.push_back( regionMap._id );
        }
        for ( const MetroidvaniaSiteDef& site : _pCatalog->getSites() )
        {
            if ( isSiteActive( site._id ) )
                outListSite.push_back( site._id );
        }
        for ( const MetroidvaniaPickupDef& pickup : _pCatalog->getPickups() )
        {
            if ( isCollected( pickup._id ) )
                outListPickup.push_back( pickup._id );
        }
    }

    void MetroidvaniaMapState::restoreSaveState( const vector<hashed_string>& listRegionMap, const vector<hashed_string>& listSite, const vector<hashed_string>& listPickup )
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
        for ( const hashed_string& siteID : listSite )
        {
            if ( _pCatalog->findSite( siteID ) != nullptr && isSiteActive( siteID ) == false )
                _listSite.push_back( siteID );
        }
        for ( const hashed_string& pickupID : listPickup )
        {
            if ( _pCatalog->findPickup( pickupID ) != nullptr && isCollected( pickupID ) == false )
                _listPickup.push_back( pickupID );
        }
    }

    void MetroidvaniaMapState::writeState( Archive& outArchive ) const
    {
        for ( const vector<hashed_string>* pListID : { &_listRegionMap, &_listSite, &_listPickup } )
        {
            outArchive << static_cast<uint32>( pListID->size() );
            for ( const hashed_string& id : *pListID )
            {
                StateArchiveUtil::writeName( outArchive, id );
            }
        }
    }

    bool MetroidvaniaMapState::readState( Archive& archive )
    {
        if ( _pCatalog == nullptr )
            return false;
        // 산 지도, 연 지점, 주운 것 순서로 읽는다. 모두 카탈로그에 있고 겹치지 않아야 한다
        vector<hashed_string> listRegionMap;
        vector<hashed_string> listSite;
        vector<hashed_string> listPickup;
        for ( int32 listIndex = 0; listIndex < 3; ++listIndex )
        {
            vector<hashed_string>& listID  = listIndex == 0 ? listRegionMap : ( listIndex == 1 ? listSite : listPickup );
            uint32                 idCount = 0;
            if ( StateArchiveUtil::readCount( archive, 4, idCount ) == false )
                return false;
            listID.reserve( idCount );
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
                if ( bKnown == false || VectorUtil::contains( listID, id ) )
                    return false;
                listID.push_back( id );
            }
        }
        restoreSaveState( listRegionMap, listSite, listPickup );
        return true;
    }
} // namespace sw
