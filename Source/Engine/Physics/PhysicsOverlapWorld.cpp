#include "pch.h"

#include "Engine/Physics/PhysicsOverlapWorld.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Common/EngineParallel.h"
#include "Engine/Physics/Collision/ContinuousCollision.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 부동소수점 월드 좌표를 정수 그리드 셀 번호로 바꿉니다.
         * @details float 을 int 로 캐스팅할 때 값이 int32 범위 밖이면 **정의되지 않은 동작**이고,
         *          NaN 도 마찬가지다. x86 에서 그 캐스팅은 넘치든 모자라든 똑같이 int32
         *          최솟값이 되므로, `±FLT_MAX` 처럼 아주 넓은 AABB 의 최소와 최대가 **같은
         *          셀 번호**가 된다. 폭이 1 로 읽혀서 "너무 크다" 판정을 통과하고, 그 바디가
         *          원점 근처가 아닌 엉뚱한 셀 **하나**에만 등록된다. 그러면 그 바디가 겹치는
         *          셀을 보는 질의가 바디를 찾지 못한다(조용한 충돌 누락).
         *
         *          범위 안으로 접어 넣으면 넓은 AABB 는 넓은 셀 범위가 되고, 셀 수 상한에 걸려
         *          `_listOversizedBody` 로 간다. 질의가 항상 함께 보는 목록이다. 나눗셈을
         *          float64 로 하는 이유는 float64 가 모든 int32 를 정확히 담아서 경계 비교가
         *          어긋나지 않기 때문이다.
         */
        int32 toCellCoord( float32 val, float32 cellSize )
        {
            const float64 scaled = MathUtil::floor( static_cast<float64>( val ) / static_cast<float64>( cellSize ) );
            if ( ( scaled >= static_cast<float64>( MathUtil::kMinInt32 ) ) == false ) // NaN 도 이쪽으로 온다
                return MathUtil::kMinInt32;
            if ( scaled > static_cast<float64>( MathUtil::kMaxInt32 ) )
                return MathUtil::kMaxInt32;
            return static_cast<int32>( scaled );
        }
    } // namespace
} // namespace sw

namespace sw
{
    PhysicsOverlapWorld::PhysicsOverlapWorld()
    {
        _listQueryScratch.resize( engine::getParallelScratchSlotCount() );
    }

    vector<PhysicsOverlapWorld::BodyHandle>& PhysicsOverlapWorld::getQueryScratch( vector<BodyHandle>& listFallback ) const
    {
        // 서비스가 없으면 모든 스레드가 슬롯 0 을 받으므로 나눠 쓸 수 없다. 테스트와 도구만 이 경로로 온다.
        if ( engine::areEngineServicesBound() == false )
            return listFallback;
        const uint32 slot = engine::getParallelScratchSlot();
        if ( slot >= _listQueryScratch.size() )
            return listFallback;
        vector<BodyHandle>& scratch = _listQueryScratch[slot];
        scratch.clear();
        return scratch;
    }

    PhysicsOverlapWorld::CellRange PhysicsOverlapWorld::CellRange::fromAABB( const AABB& aabb, float32 cellSize )
    {
        // 뒤집힌 AABB(min > max)도 받는다. 부르는 쪽마다 정규화를 적으면 그중 하나가 빠진다.
        const float3 normMin = float3::min( aabb._min, aabb._max );
        const float3 normMax = float3::max( aabb._min, aabb._max );

        CellRange range{};
        range._minX = toCellCoord( normMin._x, cellSize );
        range._maxX = toCellCoord( normMax._x, cellSize );
        range._minY = toCellCoord( normMin._y, cellSize );
        range._maxY = toCellCoord( normMax._y, cellSize );
        range._minZ = toCellCoord( normMin._z, cellSize );
        range._maxZ = toCellCoord( normMax._z, cellSize );
        return range;
    }

    int64 PhysicsOverlapWorld::CellRange::getCellCount() const noexcept
    {
        const int64 spanX          = static_cast<int64>( _maxX ) - static_cast<int64>( _minX ) + 1;
        const int64 spanY          = static_cast<int64>( _maxY ) - static_cast<int64>( _minY ) + 1;
        const int64 spanZ          = static_cast<int64>( _maxZ ) - static_cast<int64>( _minZ ) + 1;
        const bool  bEmptyGridSpan = spanX <= 0 || spanY <= 0 || spanZ <= 0;
        if ( bEmptyGridSpan )
            return 0;

        // **곱하기 전에 넘칠지 확인한다.** 셀 번호는 int32 라 한 축의 폭이 2^32 까지 가고, 세 축을 곱하면
        // int64 를 한참 넘는다. 넘친 곱은 작은 수(심지어 0)가 되어 "좁은 범위"로 읽히고, 그러면
        // `kMaxBodyCellCount` 검사를 통과해 **막으려던 순회를 그대로 돌게 된다.** 무한대 AABB 하나가
        // 그리드 삽입을 끝나지 않게 만든다. 호출하는 쪽은 이 값을 상한과 비교하기만 하므로, 넘칠 때는
        // 표현 가능한 최댓값으로 고정하면 답이 맞는다.
        int64 cellCount = spanX;
        if ( cellCount > MathUtil::kMaxInt64 / spanY )
            return MathUtil::kMaxInt64;
        cellCount *= spanY;
        if ( cellCount > MathUtil::kMaxInt64 / spanZ )
            return MathUtil::kMaxInt64;
        return cellCount * spanZ;
    }

    bool PhysicsOverlapWorld::shouldScanAllBodies( const CellRange& range ) const
    {
        const int64 cellCount = range.getCellCount();
        return cellCount <= 0 || cellCount > kMaxQueryCellCount || cellCount > static_cast<int64>( _bodies.size() );
    }

    bool PhysicsOverlapWorld::isOversizedForGrid( const AABB& aabb )
    {
        const int64 cellCount = CellRange::fromAABB( aabb, kCellSize ).getCellCount();
        return cellCount <= 0 || cellCount > kMaxBodyCellCount;
    }

    size_t PhysicsOverlapWorld::getGridCellCount() const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        return _mapGrid.size();
    }

    void PhysicsOverlapWorld::gatherCandidateHandles( const CellRange& range, vector<BodyHandle>& outListHandle ) const
    {
        outListHandle.clear();
        outListHandle.reserve( 64 );
        range.forEachCell( [this, &outListHandle]( const CellCoord& coord )
        {
            auto it = _mapGrid.find( coord );
            if ( it != _mapGrid.end() )
                outListHandle.insert( outListHandle.end(), it->second.begin(), it->second.end() );
        } );

        // **큰 바디는 셀에 없다.** 그리드를 훑는 경로로 왔더라도 그 목록을 함께 봐야 답이 맞는다.
        outListHandle.insert( outListHandle.end(), _listOversizedBody.begin(), _listOversizedBody.end() );

        // 한 바디가 여러 셀에 걸쳐 있으므로 같은 핸들이 여러 번 들어온다.
        std::sort( outListHandle.begin(), outListHandle.end() );
        outListHandle.erase( std::unique( outListHandle.begin(), outListHandle.end() ), outListHandle.end() );
    }

    /**
     * @brief 대상 AABB 가 덮는 모든 3D 그리드 셀에 바디 핸들을 등록합니다(너무 큰 바디는 따로 둡니다).
     */
    void PhysicsOverlapWorld::insertBodyToGrid( BodyHandle handle, const AABB& aabb )
    {
        if ( aabb.isValid() == false )
            return;

        // 너무 큰 바디는 셀마다 적지 않는다. 적으면 한 바디가 셀 테이블을 통째로 부풀린다.
        if ( isOversizedForGrid( aabb ) )
        {
            _listOversizedBody.push_back( handle );
            return;
        }

        CellRange::fromAABB( aabb, kCellSize ).forEachCell( [this, handle]( const CellCoord& coord )
        {
            _mapGrid[coord].push_back( handle );
        } );
    }

    /**
     * @brief 대상 AABB 가 덮던 그리드 셀들에서 바디 핸들을 뺍니다.
     */
    void PhysicsOverlapWorld::removeBodyFromGrid( BodyHandle handle, const AABB& aabb )
    {
        if ( aabb.isValid() == false )
            return;

        // **넣을 때와 같은 판단을 쓴다.** 크기 판정은 AABB 만으로 정해지므로 넣을 때와 뺄 때가
        // 반드시 같은 답을 낸다. 다르면 큰 바디가 목록에 영원히 남거나, 셀에 죽은 핸들이 남는다.
        if ( isOversizedForGrid( aabb ) )
        {
            (void)VectorUtil::removeSingleSwap( _listOversizedBody, handle ); // 없으면 뺄 것이 없다
            return;
        }

        // **넣을 때와 같은 범위를 훑는다.** 이것이 이 타입이 있는 이유다. 덜 훑으면 죽은 핸들이 남는다.
        CellRange::fromAABB( aabb, kCellSize ).forEachCell( [this, handle]( const CellCoord& coord )
        {
            auto it = _mapGrid.find( coord );
            if ( it == _mapGrid.end() )
                return;

            vector<BodyHandle>& listHandle = it->second;
            auto                handleIt   = std::find( listHandle.begin(), listHandle.end(), handle );
            if ( handleIt != listHandle.end() )
            {
                *handleIt = listHandle.back();
                listHandle.pop_back();
                if ( listHandle.empty() )
                    _mapGrid.erase( it );
            }
        } );
    }

    void PhysicsOverlapWorld::gatherStepCandidates( const CellRange& range, vector<BodyHandle>& outListHandle ) const
    {
        if ( shouldScanAllBodies( range ) == false )
        {
            gatherCandidateHandles( range, outListHandle );
            return;
        }
        outListHandle.clear();
        _bodies.forEachHandle( [&outListHandle]( SlotHandle handle, const PhysicsBody& )
        { outListHandle.push_back( handle ); } );
    }

    /**
     * @brief 새 물리 바디를 월드에 등록하고 공간 그리드에 배치합니다.
     */
    PhysicsOverlapWorld::BodyHandle PhysicsOverlapWorld::addBody( const AABB& aabb, uint8 layer, uint64 objectID )
    {
        SW_MEMORY_SCOPE( Physics );
        PhysicsBodyState state{};
        state._aabb  = aabb;
        state._layer = layer;
        return addBody( state, objectID );
    }

    PhysicsOverlapWorld::BodyHandle PhysicsOverlapWorld::addBody( const PhysicsBodyState& state, uint64 objectID )
    {
        SW_MEMORY_SCOPE( Physics );
        PhysicsBody body{};
        body._aabb        = state._aabb;
        body._stepAABB    = state._aabb; // 출발점은 추가한 위치다. 추가하기 전 어딘가에서 쓸려 오지 않는다.
        body._layer       = state._layer;
        body._objectID    = objectID;
        body._bContinuous = state._bContinuous;
        body._bTrigger    = state._bTrigger;
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        BodyHandle                          handle = _bodies.insert( body );
        insertBodyToGrid( handle, state._aabb );
        return handle;
    }

    /**
     * @brief 물리 바디를 월드에서 빼고 공간 그리드에서도 지웁니다.
     */
    void PhysicsOverlapWorld::removeBody( BodyHandle handle )
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        const PhysicsBody*                  pBody = _bodies.get( handle );
        if ( pBody != nullptr )
            removeBodyFromGrid( handle, pBody->_aabb );
        _bodies.erase( handle );
    }

    /**
     * @brief 바디의 위치와 크기(AABB)를 갱신하고, 공간 그리드에서 차지하는 셀을 다시 맞춥니다.
     */
    void PhysicsOverlapWorld::setAABB( BodyHandle handle, const AABB& aabb )
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        PhysicsBody*                        pBody = _bodies.get( handle );
        if ( pBody == nullptr )
            return;
        setAABBLocked( handle, *pBody, aabb );
    }

    void PhysicsOverlapWorld::updateBody( BodyHandle handle, const PhysicsBodyState& state, BodyMoveType moveType )
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        PhysicsBody*                        pBody = _bodies.get( handle );
        if ( pBody == nullptr )
            return;
        // 레이어와 트리거는 그리드와 무관하다. 쌍을 측정하는 `step` 이 읽는다. 바뀌면 다음 step 에서 걸러진 쌍이 끝난다.
        pBody->_layer       = state._layer;
        pBody->_bContinuous = state._bContinuous;
        pBody->_bTrigger    = state._bTrigger;
        setAABBLocked( handle, *pBody, state._aabb );
        // 순간이동은 그 경로를 지나가지 않았다. 새 위치를 다음 쓸림의 출발점으로 둔다(언리얼 `TeleportPhysics`).
        if ( moveType == BodyMoveType::Teleport )
            pBody->_stepAABB = state._aabb;
    }

    void PhysicsOverlapWorld::setAABBLocked( BodyHandle handle, PhysicsBody& body, const AABB& aabb )
    {
        const AABB oldAABB = body._aabb;
        if ( oldAABB.isValid() && aabb.isValid() )
        {
            // 지름길: 덮는 셀이 그대로면 그리드를 건드릴 필요가 없다. 이 판단은 삽입과 제거와 **같은
            // 계산**을 써야 한다. 아니면 바디가 틀린 셀에 앉은 채로 남는다.
            const CellRange oldRange = CellRange::fromAABB( oldAABB, kCellSize );
            const CellRange newRange = CellRange::fromAABB( aabb, kCellSize );
            if ( oldRange == newRange )
            {
                body._aabb = aabb;
                return;
            }
        }

        removeBodyFromGrid( handle, body._aabb );
        body._aabb = aabb;
        insertBodyToGrid( handle, aabb );
    }

    /**
     * @brief 바디 핸들로 물리 바디 정보를 찾아 복사합니다(스레드 안전).
     */
    bool PhysicsOverlapWorld::tryGetBody( BodyHandle handle, PhysicsBody& outBody ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        const PhysicsBody*                  pBody = _bodies.get( handle );
        if ( pBody != nullptr )
        {
            outBody = *pBody;
            return true;
        }
        return false;
    }

    /**
     * @brief 바디 쌍의 겹침을 다시 재 지난 step 과 달라진 쌍을 이벤트로 냅니다. 적분하는 솔버는 없습니다(강체가 없다).
     */
    void PhysicsOverlapWorld::step( float32 deltaTime )
    {
        (void)deltaTime; // 적분하지 않는다. 강체가 없으므로 겹침만 다시 측정한다.
        std::unique_lock<std::shared_mutex> lock{ _mutex };

        // 바디를 먼저 복사한다. 테이블을 도는 동안(테이블의 잠금 안) 다른 바디를 꺼내면 같은 잠금을 다시 잡게 된다. 참조로 돌면 교착한다.
        // 사본은 멤버 버퍼에 담는다(비우고 용량은 남긴다). step 은 배타 잠금 안이라 다른 스레드가 버퍼를 같이 쓰지 않는다.
        vector<StepBodyEntry>& listBody = _listScratchStepBody;
        listBody.clear();
        listBody.reserve( _bodies.size() );
        _bodies.forEachHandle( [&listBody]( SlotHandle handle, const PhysicsBody& body )
        { listBody.push_back( StepBodyEntry{ handle, body } ); } );

        // 이번 step 에 셀 하나 이내로 움직인 바디들의 축별 가장 큰 거리다. 연속 바디의 후보를 모으는 범위를 이만큼 넓힌다(`addSweptPairs`).
        // 셀 하나보다 멀리 간 바디는 넓히는 데 넣지 않고 따로 모아, 연속 바디마다 하나씩 측정한다.
        float3              maxDisplacement{ 0.0f, 0.0f, 0.0f };
        vector<BodyHandle>& listFarMover = _listScratchFarMover;
        listFarMover.clear();
        for ( const StepBodyEntry& entry : listBody )
        {
            if ( entry._body._aabb.isValid() == false || entry._body._stepAABB.isValid() == false )
                continue;
            if ( isFarMover( entry._body ) )
            {
                listFarMover.push_back( entry._handle );
                continue;
            }
            const float3 displacement = entry._body._aabb.getCenter() - entry._body._stepAABB.getCenter();
            maxDisplacement           = float3::max( maxDisplacement, float3{ MathUtil::abs( displacement._x ), MathUtil::abs( displacement._y ),
                                                                    MathUtil::abs( displacement._z ) } );
        }

        _listScratchPair.clear();
        vector<BodyHandle>& listCandidate = _listScratchCandidate;
        for ( const StepBodyEntry& entry : listBody )
        {
            if ( entry._body._aabb.isValid() == false )
                continue;
            gatherStepCandidates( CellRange::fromAABB( entry._body._aabb, kCellSize ), listCandidate );
            for ( const BodyHandle candidate : listCandidate )
            {
                // 쌍마다 한 번만 센다. 작은 핸들 쪽에서만 센다.
                if ( ( entry._handle < candidate ) == false )
                    continue;
                const PhysicsBody* pOther = _bodies.get( candidate );
                if ( pOther == nullptr || queryOverlaps( entry._body._aabb, entry._body._layer, pOther->_aabb, pOther->_layer, _layers ) == false )
                    continue;
                _listScratchPair.push_back( OverlapPair::makeOrdered( entry._handle, entry._body, candidate, *pOther, 1.0f ) );
            }
            if ( entry._body._bContinuous == SW_TRUE )
                addSweptPairs( entry._handle, entry._body, maxDisplacement, listFarMover, listCandidate );
        }
        // 같은 쌍이 제자리 겹침과 쓸림(연속 바디 둘이면 양쪽 쓸림)으로 여러 번 들어올 수 있다. 쌍 순서로 정렬하고 가장 먼저 닿은 하나만 남긴다.
        std::sort( _listScratchPair.begin(), _listScratchPair.end(), &OverlapPair::isEarlierInOrder );
        _listScratchPair.erase( std::unique( _listScratchPair.begin(), _listScratchPair.end(),
                                             []( const OverlapPair& lhs, const OverlapPair& rhs )
        { return lhs.isSamePair( rhs ); } ),
                                _listScratchPair.end() );

        // 지난 쌍과 견줘 달라진 것만 낸다(둘 다 정렬돼 있다).
        _listOverlapEvent.clear();
        size_t previousIndex = 0;
        size_t currentIndex  = 0;
        while ( previousIndex < _listOverlapPair.size() || currentIndex < _listScratchPair.size() )
        {
            const bool bHasPrevious    = previousIndex < _listOverlapPair.size();
            const bool bHasCurrent     = currentIndex < _listScratchPair.size();
            const bool bSamePairInBoth = bHasPrevious && bHasCurrent && _listOverlapPair[previousIndex].isSamePair( _listScratchPair[currentIndex] );
            if ( bSamePairInBoth )
            {
                ++previousIndex;
                ++currentIndex;
                continue;
            }
            if ( bHasCurrent && ( bHasPrevious == false || _listScratchPair[currentIndex] < _listOverlapPair[previousIndex] ) )
            {
                const OverlapPair& pair = _listScratchPair[currentIndex++];
                _listOverlapEvent.push_back(
                    PhysicsOverlapEvent{ pair._firstObjectID, pair._secondObjectID, pair._hitFraction, SW_TRUE, pair._bFirstTrigger, pair._bSecondTrigger } );
                continue;
            }
            // 끝 이벤트의 트리거 여부는 겹쳐 있던 때의 값이다. 바디가 이미 사라졌을 수 있다.
            const OverlapPair& pair = _listOverlapPair[previousIndex++];
            _listOverlapEvent.push_back(
                PhysicsOverlapEvent{ pair._firstObjectID, pair._secondObjectID, 1.0f, SW_FALSE, pair._bFirstTrigger, pair._bSecondTrigger } );
        }
        // **먼저 닿은 것이 먼저 간다.** 빠른 총알이 한 step 에 적 둘을 지나가면, 받는 쪽은 앞의 이벤트에 반응해 사라진다. 핸들 순서로 두면
        // 뒤의 적이 맞을 수 있다. 같은 시점끼리는 쌍 순서 그대로다(안정 정렬).
        std::stable_sort( _listOverlapEvent.begin(), _listOverlapEvent.end(), []( const PhysicsOverlapEvent& lhs, const PhysicsOverlapEvent& rhs )
        { return lhs._hitFraction < rhs._hitFraction; } );

        // 이번 위치가 다음 step 의 출발점이다(연속 바디가 여기서부터 쓸린다).
        _bodies.forEachHandle( []( SlotHandle, PhysicsBody& body )
        { body._stepAABB = body._aabb; } );
        _listOverlapPair.swap( _listScratchPair );
    }

    void PhysicsOverlapWorld::addSweptPairs( BodyHandle handle, const PhysicsBody& body, const float3& maxNearDisplacement, const vector<BodyHandle>& listFarMover,
                                             vector<BodyHandle>& inoutListCandidate )
    {
        const AABB& from = body._stepAABB;
        if ( from.isValid() == false || body._aabb.isValid() == false )
            return;
        const float3 displacement = body._aabb.getCenter() - from.getCenter();

        // 후보는 그리드에 현재 위치로 들어 있다. 상대가 그 사이 어느 때 이 바디와 겹쳤다면, 상대의 현재 위치는 쓸린 범위에서 상대가 움직인 거리
        // 안에 있다. 그래서 셀 하나 이내로 움직인 바디들의 가장 큰 거리만큼 넓혀 모으면, 그 바디들 가운데 빠지는 상대가 없다.
        const AABB swept = from.unionWith( body._aabb );
        const AABB range{ swept._min - maxNearDisplacement, swept._max + maxNearDisplacement };
        gatherStepCandidates( CellRange::fromAABB( range, kCellSize ), inoutListCandidate );
        for ( const BodyHandle candidate : inoutListCandidate )
        {
            // 멀리 간 바디는 아래에서 한 번만 측정한다.
            const PhysicsBody* pOther = _bodies.get( candidate );
            if ( pOther == nullptr || isFarMover( *pOther ) )
                continue;
            addSweptPairIfTouched( handle, body, displacement, candidate );
        }
        for ( const BodyHandle farMover : listFarMover )
        {
            addSweptPairIfTouched( handle, body, displacement, farMover );
        }
    }

    void PhysicsOverlapWorld::addSweptPairIfTouched( BodyHandle handle, const PhysicsBody& body, const float3& displacement, BodyHandle candidate )
    {
        if ( candidate == handle )
            return;
        const PhysicsBody* pOther    = _bodies.get( candidate );
        const bool         bCanTouch = pOther != nullptr && pOther->_aabb.isValid() && _layers.shouldCollide( body._layer, pOther->_layer );
        if ( bCanTouch == false )
            return;
        // 상대 운동: 둘 다 출발점에서, 이 바디의 이동에서 상대의 이동을 뺀 만큼 쓴다(Box2D 총알 TOI). 상대가 가만히 있었으면 그냥 쓸림과 같다.
        const AABB&  otherFrom            = pOther->_stepAABB.isValid() ? pOther->_stepAABB : pOther->_aabb;
        const float3 otherDisplacement    = pOther->_aabb.getCenter() - otherFrom.getCenter();
        const float3 relativeDisplacement = displacement - otherDisplacement;
        if ( relativeDisplacement.getLengthSquared() <= 0.0f )
            return; // 함께 움직였다. 제자리 겹침이 전부다.
        // 닿은 시점 0 은 출발점에서 이미 겹쳐 있던 것이다. 그 겹침은 지난 step 이 측정했고, 지금도 겹치면 제자리 겹침이 이어 간다. 여기서 넣으면 떠난 쌍의
        // 끝이 한 step 늦는다.
        SweepHit   hit{};
        const bool bEnteredWhileMoving = ContinuousCollision::sweepAABB( body._stepAABB, relativeDisplacement, otherFrom, hit ) && hit._hitFraction > 0.0f;
        if ( bEnteredWhileMoving == false )
            return;
        _listScratchPair.push_back( OverlapPair::makeOrdered( handle, body, candidate, *pOther, hit._hitFraction ) );
    }

    bool PhysicsOverlapWorld::isFarMover( const PhysicsBody& body )
    {
        if ( body._aabb.isValid() == false || body._stepAABB.isValid() == false )
            return false;
        const float3 displacement = body._aabb.getCenter() - body._stepAABB.getCenter();
        return MathUtil::abs( displacement._x ) > kCellSize || MathUtil::abs( displacement._y ) > kCellSize || MathUtil::abs( displacement._z ) > kCellSize;
    }

    /**
     * @brief 두 바디가 레이어 마스크와 AABB 모두에서 겹치는지 검사합니다.
     */
    bool PhysicsOverlapWorld::overlaps( BodyHandle a, BodyHandle b ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        const PhysicsBody*                  pBodyA = _bodies.get( a );
        const PhysicsBody*                  pBodyB = _bodies.get( b );
        if ( pBodyA == nullptr || pBodyB == nullptr )
            return false;
        return queryOverlaps( pBodyA->_aabb, pBodyA->_layer, pBodyB->_aabb, pBodyB->_layer, _layers );
    }

    /**
     * @brief AABB 와 겹치는 물리 바디를 공간 그리드로 빠르게 찾습니다.
     *
     * 1. 박스가 걸치는 그리드 셀을 돌며 중복 없는 후보 바디 목록을 모읍니다(범위가 비었거나 너무 넓으면 모든 바디를 봅니다).
     * 2. 후보마다 레이어 마스크와 정확한 AABB 교차를 검사해 outListHandle 에 넣습니다.
     */
    void PhysicsOverlapWorld::queryAABB( const AABB& box, uint8 layer, vector<BodyHandle>& outListHandle ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        outListHandle.clear();

        if ( box.isValid() == false )
            return;

        const CellRange range = CellRange::fromAABB( box, kCellSize );
        if ( shouldScanAllBodies( range ) )
        {
            _bodies.forEachHandle( [&]( SlotHandle handle, const PhysicsBody& body )
            {
                if ( queryOverlaps( box, layer, body._aabb, body._layer, _layers ) )
                    outListHandle.push_back( handle );
            } );
            return;
        }

        vector<BodyHandle>  listFallback;
        vector<BodyHandle>& listCandidateHandle = getQueryScratch( listFallback );
        gatherCandidateHandles( range, listCandidateHandle );

        for ( BodyHandle handle : listCandidateHandle )
        {
            const PhysicsBody* pBody = _bodies.get( handle );
            if ( pBody != nullptr )
            {
                if ( queryOverlaps( box, layer, pBody->_aabb, pBody->_layer, _layers ) )
                    outListHandle.push_back( handle );
            }
        }
    }

    bool PhysicsOverlapWorld::sweepTest( const AABB& movingBox, const float3& displacement, uint8 layer, SweepHit& outHit ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        outHit._bHit        = false;
        outHit._hitFraction = 1.0f;

        if ( movingBox.isValid() == false )
            return false;

        const AABB sweptBounds{
            float3::min( movingBox._min, movingBox._min + displacement ),
            float3::max( movingBox._max, movingBox._max + displacement ) };

        const CellRange range = CellRange::fromAABB( sweptBounds, kCellSize );

        bool     bFoundHit = false;
        SweepHit nearestHit{};
        nearestHit._hitFraction = 1.0f;

        if ( shouldScanAllBodies( range ) )
        {
            _bodies.forEachHandle( [&]( SlotHandle handle, const PhysicsBody& body )
            {
                if ( _layers.shouldCollide( layer, body._layer ) )
                {
                    SweepHit hit{};
                    if ( ContinuousCollision::sweepAABB( movingBox, displacement, body._aabb, hit ) )
                    {
                        if ( hit._hitFraction < nearestHit._hitFraction || bFoundHit == false )
                        {
                            bFoundHit               = true;
                            nearestHit              = hit;
                            nearestHit._hitBody     = handle;
                            nearestHit._hitObjectID = body._objectID;
                        }
                    }
                }
            } );

            if ( bFoundHit )
            {
                outHit = nearestHit;
                return true;
            }
            return false;
        }

        vector<BodyHandle>  listFallback;
        vector<BodyHandle>& listCandidateHandle = getQueryScratch( listFallback );
        gatherCandidateHandles( range, listCandidateHandle );

        for ( BodyHandle handle : listCandidateHandle )
        {
            const PhysicsBody* pBody = _bodies.get( handle );
            if ( pBody != nullptr && _layers.shouldCollide( layer, pBody->_layer ) )
            {
                SweepHit hit{};
                if ( ContinuousCollision::sweepAABB( movingBox, displacement, pBody->_aabb, hit ) )
                {
                    if ( hit._hitFraction < nearestHit._hitFraction || bFoundHit == false )
                    {
                        bFoundHit               = true;
                        nearestHit              = hit;
                        nearestHit._hitBody     = handle;
                        nearestHit._hitObjectID = pBody->_objectID;
                    }
                }
            }
        }

        if ( bFoundHit )
        {
            outHit = nearestHit;
            return true;
        }

        return false;
    }
} // namespace sw
