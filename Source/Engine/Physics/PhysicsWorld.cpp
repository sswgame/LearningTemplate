#include "pch.h"

#include "Engine/Physics/PhysicsWorld.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/MemoryProfiler.h"

#include "Engine/Physics/CCD.h"

namespace sw
{
    namespace
    {
        struct PhysicsWorldInternal
        {
            /**
             * @brief 부동소수점 월드 좌표를 정수 그리드 셀 번호로 바꿉니다.
             * @details float 을 int 로 캐스팅하는 것은 값이 int32 범위 밖이면 **정의되지 않은 동작**
             *          이고, NaN 도 마찬가지입니다. x86 에서 그 캐스팅은 넘치든 모자라든 똑같이 int32
             *          최솟값으로 붙어 버리므로, `±FLT_MAX` 처럼 아주 넓은 AABB 의 최소 · 최대가 **같은
             *          셀 번호**가 됩니다. 폭이 1 로 읽혀서 "너무 크다" 판정을 통과하고, 그 바디가
             *          원점 근처가 아닌 엉뚱한 셀 **하나**에만 등록됩니다. 그러면 그 바디가 겹치는
             *          셀을 보는 질의가 바디를 찾지 못합니다(조용한 충돌 누락).
             *
             *          범위 안으로 접어 넣으면 넓은 AABB 는 넓은 셀 범위가 되고, 셀 수 상한에 걸려
             *          `_listOversizedBody` 로 갑니다. 질의가 항상 함께 보는 자리입니다. 나눗셈을
             *          float64 로 하는 이유는 float64 가 모든 int32 를 정확히 담아서 경계 비교가
             *          어긋나지 않기 때문입니다.
             */
            static int32 toCellCoord( float32 val, float32 cellSize )
            {
                const float64 scaled = MathUtil::floor( static_cast<float64>( val ) / static_cast<float64>( cellSize ) );
                if ( ( scaled >= static_cast<float64>( MathUtil::MinInt32 ) ) == false ) // NaN 도 이쪽으로 온다
                    return MathUtil::MinInt32;
                if ( scaled > static_cast<float64>( MathUtil::MaxInt32 ) )
                    return MathUtil::MaxInt32;
                return static_cast<int32>( scaled );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PhysicsWorld::CellRange PhysicsWorld::CellRange::fromAabb( const AABB& aabb, float32 cellSize )
    {
        // 뒤집힌 AABB(min > max)도 받는다. 부르는 쪽마다 정규화를 적으면 그중 하나가 빠진다.
        const float3 normMin = float3::min( aabb._min, aabb._max );
        const float3 normMax = float3::max( aabb._min, aabb._max );

        CellRange range{};
        range._minX = PhysicsWorldInternal::toCellCoord( normMin._x, cellSize );
        range._maxX = PhysicsWorldInternal::toCellCoord( normMax._x, cellSize );
        range._minY = PhysicsWorldInternal::toCellCoord( normMin._y, cellSize );
        range._maxY = PhysicsWorldInternal::toCellCoord( normMax._y, cellSize );
        range._minZ = PhysicsWorldInternal::toCellCoord( normMin._z, cellSize );
        range._maxZ = PhysicsWorldInternal::toCellCoord( normMax._z, cellSize );
        return range;
    }

    int64 PhysicsWorld::CellRange::getCellCount() const noexcept
    {
        const int64 spanX = static_cast<int64>( _maxX ) - static_cast<int64>( _minX ) + 1;
        const int64 spanY = static_cast<int64>( _maxY ) - static_cast<int64>( _minY ) + 1;
        const int64 spanZ = static_cast<int64>( _maxZ ) - static_cast<int64>( _minZ ) + 1;
        if ( spanX <= 0 || spanY <= 0 || spanZ <= 0 )
            return 0;

        // **곱하기 전에 넘칠지 본다.** 셀 번호는 int32 라 한 축의 폭이 2^32 까지 가고, 세 축을 곱하면
        // int64 를 한참 넘는다. 넘친 곱은 작은 수(심지어 0)가 되어 "좁은 범위" 로 읽히고, 그러면
        // `kMaxBodyCellCount` 검사를 통과해 **막으려던 순회를 그대로 돌게 된다.** 무한대 AABB 하나가
        // 그리드 삽입을 돌아오지 않게 만든다. 부르는 쪽은 이 값을 상한과 견주기만 하므로, 넘칠 때는
        // 표현 가능한 최댓값으로 붙여 두면 답이 맞는다.
        int64 cellCount = spanX;
        if ( cellCount > MathUtil::MaxInt64 / spanY )
            return MathUtil::MaxInt64;
        cellCount *= spanY;
        if ( cellCount > MathUtil::MaxInt64 / spanZ )
            return MathUtil::MaxInt64;
        return cellCount * spanZ;
    }

    bool PhysicsWorld::shouldScanAllBodies( const CellRange& range ) const
    {
        const int64 cellCount = range.getCellCount();
        return cellCount <= 0 || cellCount > kMaxQueryCellCount || cellCount > static_cast<int64>( _bodies.size() );
    }

    bool PhysicsWorld::isOversizedForGrid( const AABB& aabb )
    {
        const int64 cellCount = CellRange::fromAabb( aabb, kCellSize ).getCellCount();
        return cellCount <= 0 || cellCount > kMaxBodyCellCount;
    }

    size_t PhysicsWorld::getGridCellCount() const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        return _mapGrid.size();
    }

    void PhysicsWorld::gatherCandidateHandles( const CellRange& range, vector<BodyHandle>& outListHandle ) const
    {
        outListHandle.clear();
        outListHandle.reserve( 64 );
        range.forEachCell( [this, &outListHandle]( const CellCoord& coord )
        {
            auto it = _mapGrid.find( coord );
            if ( it != _mapGrid.end() )
                outListHandle.insert( outListHandle.end(), it->second.begin(), it->second.end() );
        } );

        // **큰 바디는 셀에 없다.** 그리드를 훑는 길로 왔더라도 그쪽을 함께 봐야 답이 맞는다.
        outListHandle.insert( outListHandle.end(), _listOversizedBody.begin(), _listOversizedBody.end() );

        // 한 바디가 여러 셀에 걸쳐 있으므로 같은 핸들이 여러 번 들어온다.
        std::sort( outListHandle.begin(), outListHandle.end() );
        outListHandle.erase( std::unique( outListHandle.begin(), outListHandle.end() ), outListHandle.end() );
    }

    /**
     * @brief 대상 AABB 가 덮는 모든 3D 그리드 셀에 바디 핸들을 등록합니다(너무 큰 바디는 따로 둡니다).
     */
    void PhysicsWorld::insertBodyToGrid( BodyHandle handle, const AABB& aabb )
    {
        if ( aabb.isValid() == false )
            return;

        // 너무 큰 바디는 셀마다 적지 않는다. 적으면 한 바디가 셀 표를 통째로 불린다.
        if ( isOversizedForGrid( aabb ) )
        {
            _listOversizedBody.push_back( handle );
            return;
        }

        CellRange::fromAabb( aabb, kCellSize ).forEachCell( [this, handle]( const CellCoord& coord )
        {
            _mapGrid[coord].push_back( handle );
        } );
    }

    /**
     * @brief 대상 AABB 가 덮던 그리드 셀들에서 바디 핸들을 뺍니다.
     */
    void PhysicsWorld::removeBodyFromGrid( BodyHandle handle, const AABB& aabb )
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
        CellRange::fromAabb( aabb, kCellSize ).forEachCell( [this, handle]( const CellCoord& coord )
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

    void PhysicsWorld::gatherStepCandidates( const CellRange& range, vector<BodyHandle>& outListHandle ) const
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
    PhysicsWorld::BodyHandle PhysicsWorld::addBody( const AABB& aabb, uint8 layer, uint64 objectId )
    {
        SW_MEMORY_SCOPE( Physics );
        PhysicsBodyState state{};
        state._aabb  = aabb;
        state._layer = layer;
        return addBody( state, objectId );
    }

    PhysicsWorld::BodyHandle PhysicsWorld::addBody( const PhysicsBodyState& state, uint64 objectId )
    {
        SW_MEMORY_SCOPE( Physics );
        PhysicsBody body{};
        body._aabb        = state._aabb;
        body._stepAabb    = state._aabb; // 출발점은 더한 자리다 — 더하기 전 어딘가에서 쓸려 오지 않는다
        body._layer       = state._layer;
        body._objectId    = objectId;
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
    void PhysicsWorld::removeBody( BodyHandle handle )
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        const PhysicsBody*                  pBody = _bodies.get( handle );
        if ( pBody != nullptr )
            removeBodyFromGrid( handle, pBody->_aabb );
        _bodies.erase( handle );
    }

    /**
     * @brief 바디의 위치 · 크기(AABB)를 갱신하고 공간 그리드에서 차지하는 셀을 다시 맞춥니다.
     */
    void PhysicsWorld::setAabb( BodyHandle handle, const AABB& aabb )
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        PhysicsBody*                        pBody = _bodies.get( handle );
        if ( pBody == nullptr )
            return;
        setAabbLocked( handle, *pBody, aabb );
    }

    void PhysicsWorld::updateBody( BodyHandle handle, const PhysicsBodyState& state, BodyMoveType moveType )
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        PhysicsBody*                        pBody = _bodies.get( handle );
        if ( pBody == nullptr )
            return;
        // 레이어 · 트리거는 그리드와 무관하다 — 쌍을 재는 `step` 이 읽는다. 바뀌면 다음 step 에서 걸러진 쌍이 끝난다.
        pBody->_layer       = state._layer;
        pBody->_bContinuous = state._bContinuous;
        pBody->_bTrigger    = state._bTrigger;
        setAabbLocked( handle, *pBody, state._aabb );
        // 순간이동은 그 길을 지나가지 않았다 — 새 자리를 다음 쓸림의 출발점으로 둔다(언리얼 `TeleportPhysics`).
        if ( moveType == BodyMoveType::Teleport )
            pBody->_stepAabb = state._aabb;
    }

    void PhysicsWorld::setAabbLocked( BodyHandle handle, PhysicsBody& body, const AABB& aabb )
    {
        const AABB oldAABB = body._aabb;
        if ( oldAABB.isValid() && aabb.isValid() )
        {
            // 지름길: 덮는 셀이 그대로면 그리드를 건드릴 필요가 없다. 이 판단이 삽입 · 제거와 **같은
            // 계산**을 써야 한다. 아니면 바디가 틀린 셀에 앉은 채로 남는다.
            const CellRange oldRange = CellRange::fromAabb( oldAABB, kCellSize );
            const CellRange newRange = CellRange::fromAabb( aabb, kCellSize );
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
    bool PhysicsWorld::tryGetBody( BodyHandle handle, PhysicsBody& out ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        const PhysicsBody*                  pBody = _bodies.get( handle );
        if ( pBody != nullptr )
        {
            out = *pBody;
            return true;
        }
        return false;
    }

    /**
     * @brief 바디 쌍의 겹침을 다시 재 지난 step 과 달라진 쌍을 이벤트로 냅니다. 적분하는 솔버는 없습니다(강체가 없다).
     */
    void PhysicsWorld::step( float32 deltaTime )
    {
        (void)deltaTime; // 적분하지 않는다 — 강체가 없다. 겹침만 다시 잰다.
        std::unique_lock<std::shared_mutex> lock{ _mutex };

        // 바디를 먼저 베낀다 — 표를 도는 동안(표의 잠금 안) 다른 바디를 꺼내면 같은 잠금을 다시 잡는다.
        struct BodyEntry
        {
            BodyHandle  _handle{};
            PhysicsBody _body{};
        };
        vector<BodyEntry> listBody;
        listBody.reserve( _bodies.size() );
        _bodies.forEachHandle( [&listBody]( SlotHandle handle, const PhysicsBody& body )
        { listBody.push_back( BodyEntry{ handle, body } ); } );

        // 이번 step 에 셀 하나 이내로 움직인 바디들의 축별 가장 큰 거리 — 연속 바디의 후보를 모으는 범위를 이만큼 넓힌다(`addSweptPairs`).
        // 셀 하나보다 멀리 간 바디는 넓히는 데 넣지 않고 따로 모아 연속 바디마다 하나씩 잰다.
        float3             maxDisplacement{ 0.0f, 0.0f, 0.0f };
        vector<BodyHandle> listFarMover;
        for ( const BodyEntry& entry : listBody )
        {
            if ( entry._body._aabb.isValid() == false || entry._body._stepAabb.isValid() == false )
                continue;
            if ( isFarMover( entry._body ) )
            {
                listFarMover.push_back( entry._handle );
                continue;
            }
            const float3 displacement = entry._body._aabb.getCenter() - entry._body._stepAabb.getCenter();
            maxDisplacement           = float3::max( maxDisplacement, float3{ MathUtil::abs( displacement._x ), MathUtil::abs( displacement._y ),
                                                                    MathUtil::abs( displacement._z ) } );
        }

        _listScratchPair.clear();
        vector<BodyHandle> listCandidate;
        for ( const BodyEntry& entry : listBody )
        {
            if ( entry._body._aabb.isValid() == false )
                continue;
            gatherStepCandidates( CellRange::fromAabb( entry._body._aabb, kCellSize ), listCandidate );
            for ( const BodyHandle candidate : listCandidate )
            {
                // 쌍마다 한 번 — 작은 핸들 쪽에서만 센다.
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
        // 같은 쌍이 제자리 겹침 · 쓸림(연속 바디 둘이면 양쪽 쓸림)으로 여러 번 들 수 있다 — 쌍 순서로 줄 세우고 가장 먼저 닿은 하나만 남긴다.
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
            const bool bHasPrevious = previousIndex < _listOverlapPair.size();
            const bool bHasCurrent  = currentIndex < _listScratchPair.size();
            if ( bHasPrevious && bHasCurrent && _listOverlapPair[previousIndex].isSamePair( _listScratchPair[currentIndex] ) )
            {
                ++previousIndex;
                ++currentIndex;
                continue;
            }
            if ( bHasCurrent && ( bHasPrevious == false || _listScratchPair[currentIndex] < _listOverlapPair[previousIndex] ) )
            {
                const OverlapPair& pair = _listScratchPair[currentIndex++];
                _listOverlapEvent.push_back(
                    PhysicsOverlapEvent{ pair._firstObjectId, pair._secondObjectId, pair._time, SW_TRUE, pair._bFirstTrigger, pair._bSecondTrigger } );
                continue;
            }
            // 끝 이벤트의 트리거 여부는 겹쳐 있던 때의 것이다 — 바디가 이미 사라졌을 수 있다.
            const OverlapPair& pair = _listOverlapPair[previousIndex++];
            _listOverlapEvent.push_back(
                PhysicsOverlapEvent{ pair._firstObjectId, pair._secondObjectId, 1.0f, SW_FALSE, pair._bFirstTrigger, pair._bSecondTrigger } );
        }
        // **먼저 닿은 것이 먼저 간다.** 빠른 총알이 한 step 에 적 둘을 지나가면 받는 쪽은 앞의 이벤트에 반응해 사라진다 — 핸들 순서로 두면
        // 뒤의 적이 맞을 수 있다. 같은 때끼리는 쌍 순서 그대로다(안정 정렬).
        std::stable_sort( _listOverlapEvent.begin(), _listOverlapEvent.end(), []( const PhysicsOverlapEvent& lhs, const PhysicsOverlapEvent& rhs )
        { return lhs._time < rhs._time; } );

        // 이번 자리가 다음 step 의 출발점이다(연속 바디가 여기서부터 쓸린다).
        _bodies.forEachHandle( []( SlotHandle, PhysicsBody& body )
        { body._stepAabb = body._aabb; } );
        _listOverlapPair.swap( _listScratchPair );
    }

    void PhysicsWorld::addSweptPairs( BodyHandle handle, const PhysicsBody& body, const float3& maxNearDisplacement, const vector<BodyHandle>& listFarMover,
                                      vector<BodyHandle>& inoutListCandidate )
    {
        const AABB& from = body._stepAabb;
        if ( from.isValid() == false || body._aabb.isValid() == false )
            return;
        const float3 displacement = body._aabb.getCenter() - from.getCenter();

        // 후보는 그리드에 지금 자리로 들어 있다. 상대가 그 사이 어느 때 이 바디와 겹쳤다면, 상대의 지금 자리는 쓸린 범위에서 상대가 움직인 거리
        // 안에 있다 — 그래서 셀 하나 이내로 움직인 바디들의 가장 큰 거리만큼 넓혀 모으면 그 바디들 가운데 빠지는 상대가 없다.
        const AABB swept = from.unionWith( body._aabb );
        const AABB range{ swept._min - maxNearDisplacement, swept._max + maxNearDisplacement };
        gatherStepCandidates( CellRange::fromAabb( range, kCellSize ), inoutListCandidate );
        for ( const BodyHandle candidate : inoutListCandidate )
        {
            // 멀리 간 바디는 아래에서 한 번만 잰다.
            const PhysicsBody* pOther = _bodies.get( candidate );
            if ( pOther == nullptr || isFarMover( *pOther ) )
                continue;
            addSweptPairIfTouched( handle, body, displacement, candidate );
        }
        for ( const BodyHandle farMover : listFarMover )
            addSweptPairIfTouched( handle, body, displacement, farMover );
    }

    void PhysicsWorld::addSweptPairIfTouched( BodyHandle handle, const PhysicsBody& body, const float3& displacement, BodyHandle candidate )
    {
        if ( candidate == handle )
            return;
        const PhysicsBody* pOther    = _bodies.get( candidate );
        const bool         bCanTouch = pOther != nullptr && pOther->_aabb.isValid() && _layers.shouldCollide( body._layer, pOther->_layer );
        if ( bCanTouch == false )
            return;
        // 상대 운동: 둘 다 출발점에서, 이 바디의 이동에서 상대의 이동을 뺀 만큼 쓴다(Box2D 총알 TOI). 상대가 가만히 있었으면 그냥 쓸림과 같다.
        const AABB&  otherFrom            = pOther->_stepAabb.isValid() ? pOther->_stepAabb : pOther->_aabb;
        const float3 otherDisplacement    = pOther->_aabb.getCenter() - otherFrom.getCenter();
        const float3 relativeDisplacement = displacement - otherDisplacement;
        if ( relativeDisplacement.getLengthSquared() <= 0.0f )
            return; // 함께 움직였다 — 제자리 겹침이 전부다
        // 닿은 때 0 은 출발점에서 이미 겹쳐 있던 것이다 — 그 겹침은 지난 step 이 쟀고, 지금도 겹치면 제자리 겹침이 잇는다. 넣으면 떠난 쌍의
        // 끝이 한 step 늦는다.
        SweepHit   hit{};
        const bool bEnteredWhileMoving = CCD::sweepAabb( body._stepAabb, relativeDisplacement, otherFrom, hit ) && hit._time > 0.0f;
        if ( bEnteredWhileMoving == false )
            return;
        _listScratchPair.push_back( OverlapPair::makeOrdered( handle, body, candidate, *pOther, hit._time ) );
    }

    bool PhysicsWorld::isFarMover( const PhysicsBody& body )
    {
        if ( body._aabb.isValid() == false || body._stepAabb.isValid() == false )
            return false;
        const float3 displacement = body._aabb.getCenter() - body._stepAabb.getCenter();
        return MathUtil::abs( displacement._x ) > kCellSize || MathUtil::abs( displacement._y ) > kCellSize || MathUtil::abs( displacement._z ) > kCellSize;
    }

    /**
     * @brief 두 바디가 레이어 마스크와 AABB 모두에서 겹치는지 검사합니다.
     */
    bool PhysicsWorld::overlaps( BodyHandle a, BodyHandle b ) const
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
    void PhysicsWorld::queryAabb( const AABB& box, uint8 layer, vector<BodyHandle>& outListHandle ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        outListHandle.clear();

        if ( box.isValid() == false )
            return;

        const CellRange range = CellRange::fromAabb( box, kCellSize );
        if ( shouldScanAllBodies( range ) )
        {
            _bodies.forEachHandle( [&]( SlotHandle handle, const PhysicsBody& body )
            {
                if ( queryOverlaps( box, layer, body._aabb, body._layer, _layers ) )
                    outListHandle.push_back( handle );
            } );
            return;
        }

        vector<BodyHandle> listCandidateHandle;
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

    bool PhysicsWorld::sweepTest( const AABB& movingBox, const float3& displacement, uint8 layer, SweepHit& outHit ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        outHit._bHit = false;
        outHit._time = 1.0f;

        if ( movingBox.isValid() == false )
            return false;

        const AABB sweptBounds{
            float3::min( movingBox._min, movingBox._min + displacement ),
            float3::max( movingBox._max, movingBox._max + displacement ) };

        const CellRange range = CellRange::fromAabb( sweptBounds, kCellSize );

        bool     bFoundHit = false;
        SweepHit nearestHit{};
        nearestHit._time = 1.0f;

        if ( shouldScanAllBodies( range ) )
        {
            _bodies.forEachHandle( [&]( SlotHandle handle, const PhysicsBody& body )
            {
                if ( _layers.shouldCollide( layer, body._layer ) )
                {
                    SweepHit hit{};
                    if ( CCD::sweepAabb( movingBox, displacement, body._aabb, hit ) )
                    {
                        if ( hit._time < nearestHit._time || bFoundHit == false )
                        {
                            bFoundHit               = true;
                            nearestHit              = hit;
                            nearestHit._hitBody     = handle;
                            nearestHit._hitObjectId = body._objectId;
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

        vector<BodyHandle> listCandidateHandle;
        gatherCandidateHandles( range, listCandidateHandle );

        for ( BodyHandle handle : listCandidateHandle )
        {
            const PhysicsBody* pBody = _bodies.get( handle );
            if ( pBody != nullptr && _layers.shouldCollide( layer, pBody->_layer ) )
            {
                SweepHit hit{};
                if ( CCD::sweepAabb( movingBox, displacement, pBody->_aabb, hit ) )
                {
                    if ( hit._time < nearestHit._time || bFoundHit == false )
                    {
                        bFoundHit               = true;
                        nearestHit              = hit;
                        nearestHit._hitBody     = handle;
                        nearestHit._hitObjectId = pBody->_objectId;
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
