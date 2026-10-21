#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/InterestReplicationPolicy.h"

namespace sw
{
    InterestReplicationPolicy::InterestReplicationPolicy()
        : _interest{}
        , _mapPosition{}
        , _listObserver{}
        , _listKeyScratch{}
        , _listLeaveScratch{}
    {
    }

    bool InterestReplicationPolicy::initialize( float32 cellSize, float32 enterRadius, float32 leaveRadius )
    {
        string error;
        if ( _interest.initialize( cellSize, enterRadius, leaveRadius, error ) == false )
        {
            SW_LOG_ERROR( "InterestReplicationPolicy: refused - %# (cell %#, enter %#, leave %#)", error.c_str(), cellSize, enterRadius, leaveRadius );
            return false;
        }
        _mapPosition.clear();
        _listObserver.clear();
        return true;
    }

    void InterestReplicationPolicy::setEntityPosition( uint32 entityID, const float3& position )
    {
        _mapPosition[entityID] = position;
        _interest.setPoint( entityID, position );
    }

    void InterestReplicationPolicy::removeEntity( uint32 entityID )
    {
        _mapPosition.erase( entityID );
        _interest.removePoint( entityID );
    }

    void InterestReplicationPolicy::setObserver( int32 connectionID, const float3& center )
    {
        if ( connectionID < 0 )
            return;
        if ( connectionID >= static_cast<int32>( _listObserver.size() ) )
            _listObserver.resize( static_cast<size_t>( connectionID + 1 ) );
        Observer& observer = _listObserver[static_cast<size_t>( connectionID )];
        observer._center   = center;
        observer._bActive  = SW_TRUE;
    }

    void InterestReplicationPolicy::removeObserver( int32 connectionID )
    {
        if ( connectionID >= 0 && connectionID < static_cast<int32>( _listObserver.size() ) )
            _listObserver[static_cast<size_t>( connectionID )] = Observer{};
    }

    void InterestReplicationPolicy::updateRelevancy()
    {
        for ( Observer& observer : _listObserver )
        {
            if ( observer._bActive == SW_FALSE )
                continue;
            // 1) 사라졌거나 나가는 반경 밖으로 간 엔티티를 뺀다.
            _listLeaveScratch.clear();
            for ( const uint32 entityID : observer._uniqueVisible )
            {
                const auto positionIt = _mapPosition.find( entityID );
                if ( positionIt == _mapPosition.end() || _interest.isOutsideLeaveRadius( observer._center, positionIt->second ) )
                    _listLeaveScratch.push_back( entityID );
            }
            for ( const uint32 entityID : _listLeaveScratch )
            {
                observer._uniqueVisible.erase( entityID );
            }
            // 2) 들어오는 반경 안의 엔티티를 더한다(이미 보이면 그대로).
            _interest.queryEntering( observer._center, _listKeyScratch );
            for ( const SlotHandle key : _listKeyScratch )
            {
                observer._uniqueVisible.insert( NetInterestGrid::getEntityID( key ) );
            }
        }
    }

    const InterestReplicationPolicy::Observer* InterestReplicationPolicy::findObserver( int32 connectionID ) const
    {
        const bool bInRange = 0 <= connectionID && connectionID < static_cast<int32>( _listObserver.size() );
        if ( bInRange == false || _listObserver[static_cast<size_t>( connectionID )]._bActive == SW_FALSE )
            return nullptr;
        return &_listObserver[static_cast<size_t>( connectionID )];
    }

    bool InterestReplicationPolicy::isRelevant( int32 connectionID, const NetEntityState& entity ) const
    {
        // 자리를 알리지 않은 엔티티는 공간 정책이 판단할 대상이 아니다 — 모두에게 보낸다(머리말).
        if ( _mapPosition.find( entity._entityID ) == _mapPosition.end() )
            return true;
        const Observer* pObserver = findObserver( connectionID );
        return pObserver != nullptr && pObserver->_uniqueVisible.find( entity._entityID ) != pObserver->_uniqueVisible.end();
    }

    float32 InterestReplicationPolicy::computePriority( int32 connectionID, const NetEntityState& entity ) const
    {
        const Observer* pObserver  = findObserver( connectionID );
        const auto      positionIt = _mapPosition.find( entity._entityID );
        if ( pObserver == nullptr || positionIt == _mapPosition.end() )
            return 1.0f;
        return 1.0f / ( 1.0f + float3::getHorizontalDistance( pObserver->_center, positionIt->second ) * 0.1f );
    }

    int32 InterestReplicationPolicy::getVisibleCount( int32 connectionID ) const
    {
        const Observer* pObserver = findObserver( connectionID );
        return pObserver != nullptr ? static_cast<int32>( pObserver->_uniqueVisible.size() ) : 0;
    }
} // namespace sw
