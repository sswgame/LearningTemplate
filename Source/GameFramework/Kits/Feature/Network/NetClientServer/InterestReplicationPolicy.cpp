#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/InterestReplicationPolicy.h"

#include <algorithm>

namespace sw
{
    InterestReplicationPolicy::InterestReplicationPolicy()
        : _interest{}
        , _mapPosition{}
        , _listObserver{}
        , _listKeyScratch{}
        , _listEnterScratch{}
        , _listMergeScratch{}
        , _listUnpositionedIndex{}
    {
    }

    bool InterestReplicationPolicy::initialize( float32 cellSize, float32 enterRadius, float32 leaveRadius )
    {
        string error;
        if ( _interest.initialize( cellSize, enterRadius, leaveRadius, error ) == false )
        {
            SW_LOG_ERROR_ID( "InterestReplicationPolicy.InterestReplicationPolicyRefusedCellEnter", "InterestReplicationPolicy: refused - %# (cell %#, enter %#, leave %#)", error.c_str(), cellSize, enterRadius, leaveRadius );
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
            // 1) 사라졌거나 나가는 반경 밖으로 간 엔티티를 뺀다(차례는 그대로).
            const auto leaveIt = std::remove_if( observer._listVisible.begin(), observer._listVisible.end(), [this, &observer]( uint32 entityID )
            {
                const auto positionIt = _mapPosition.find( entityID );
                return positionIt == _mapPosition.end() || _interest.isOutsideLeaveRadius( observer._center, positionIt->second );
            } );
            observer._listVisible.erase( leaveIt, observer._listVisible.end() );
            // 2) 들어오는 반경 안의 엔티티를 더한다(이미 보이면 그대로) — 정렬해 합쳐 id 오름차순 · 겹침 없음을 지킨다.
            _interest.queryEntering( observer._center, _listKeyScratch );
            _listEnterScratch.clear();
            for ( const SlotHandle key : _listKeyScratch )
            {
                _listEnterScratch.push_back( NetInterestGrid::getEntityID( key ) );
            }
            std::sort( _listEnterScratch.begin(), _listEnterScratch.end() );
            _listMergeScratch.clear();
            std::set_union( observer._listVisible.begin(), observer._listVisible.end(), _listEnterScratch.begin(),
                            std::unique( _listEnterScratch.begin(), _listEnterScratch.end() ), std::back_inserter( _listMergeScratch ) );
            observer._listVisible.swap( _listMergeScratch );
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
        return pObserver != nullptr && std::binary_search( pObserver->_listVisible.begin(), pObserver->_listVisible.end(), entity._entityID );
    }

    float32 InterestReplicationPolicy::computePriority( int32 connectionID, const NetEntityState& entity ) const
    {
        const Observer* pObserver  = findObserver( connectionID );
        const auto      positionIt = _mapPosition.find( entity._entityID );
        if ( pObserver == nullptr || positionIt == _mapPosition.end() )
            return 1.0f;
        return 1.0f / ( 1.0f + float3::getHorizontalDistance( pObserver->_center, positionIt->second ) * 0.1f );
    }

    bool InterestReplicationPolicy::evaluateEntity( int32 connectionID, const NetEntityState& entity, float32& outPriority ) const
    {
        // 자리 표를 한 번만 찾는다(두 질문을 따로 물으면 엔티티 × 연결마다 셋이다). 답은 `isRelevant` · `computePriority` 와 같다.
        const auto positionIt = _mapPosition.find( entity._entityID );
        if ( positionIt == _mapPosition.end() )
        {
            outPriority = 1.0f;
            return true;
        }
        const Observer* pObserver = findObserver( connectionID );
        if ( pObserver == nullptr || std::binary_search( pObserver->_listVisible.begin(), pObserver->_listVisible.end(), entity._entityID ) == false )
            return false;
        outPriority = 1.0f / ( 1.0f + float3::getHorizontalDistance( pObserver->_center, positionIt->second ) * 0.1f );
        return true;
    }

    int32 InterestReplicationPolicy::getVisibleCount( int32 connectionID ) const
    {
        const Observer* pObserver = findObserver( connectionID );
        return pObserver != nullptr ? static_cast<int32>( pObserver->_listVisible.size() ) : 0;
    }

    void InterestReplicationPolicy::prepareSnapshots( const vector<NetEntityState>& listWorldEntity )
    {
        _listUnpositionedIndex.clear();
        for ( size_t worldIndex = 0; worldIndex < listWorldEntity.size(); ++worldIndex )
        {
            SW_ASSERT( worldIndex == 0 || listWorldEntity[worldIndex - 1]._entityID < listWorldEntity[worldIndex]._entityID ); // 합치기가 기대는 차례(endTick 이 정렬한다)
            if ( _mapPosition.find( listWorldEntity[worldIndex]._entityID ) == _mapPosition.end() )
                _listUnpositionedIndex.push_back( static_cast<uint32>( worldIndex ) );
        }
    }

    void InterestReplicationPolicy::collectRelevantEntities( int32 connectionID, const vector<NetEntityState>& listWorldEntity, vector<NetRelevantEntity>& outList ) const
    {
        outList.clear();
        const Observer* pObserver          = findObserver( connectionID );
        size_t          unpositionedCursor = 0;
        // 자리 없는 엔티티를 @p worldIndex 앞까지 차례로 넣는다(우선도 1 — `evaluateEntity` 와 같다).
        auto flushUnpositionedBefore = [&]( uint32 worldIndex )
        {
            while ( unpositionedCursor < _listUnpositionedIndex.size() && _listUnpositionedIndex[unpositionedCursor] < worldIndex )
            {
                outList.push_back( NetRelevantEntity{ _listUnpositionedIndex[unpositionedCursor], 1.0f } );
                ++unpositionedCursor;
            }
        };
        if ( pObserver != nullptr )
        {
            // 보이는 id 와 월드 목록이 둘 다 id 오름차순이다 — 월드 쪽 자리는 앞으로만 간다.
            auto worldIt = listWorldEntity.begin();
            for ( const uint32 entityID : pObserver->_listVisible )
            {
                worldIt = std::lower_bound( worldIt, listWorldEntity.end(), entityID, []( const NetEntityState& entity, uint32 id )
                { return entity._entityID < id; } );
                if ( worldIt == listWorldEntity.end() )
                    break;
                if ( worldIt->_entityID != entityID )
                    continue; // 이번 틱 월드에 없는 엔티티
                const auto positionIt = _mapPosition.find( entityID );
                if ( positionIt == _mapPosition.end() )
                    continue; // 자리를 지운 엔티티는 자리 없는 엔티티로 든다(`prepareSnapshots`)
                const uint32 worldIndex = static_cast<uint32>( worldIt - listWorldEntity.begin() );
                flushUnpositionedBefore( worldIndex );
                outList.push_back( NetRelevantEntity{ worldIndex, 1.0f / ( 1.0f + float3::getHorizontalDistance( pObserver->_center, positionIt->second ) * 0.1f ) } );
            }
        }
        flushUnpositionedBefore( static_cast<uint32>( listWorldEntity.size() ) );
    }
} // namespace sw
