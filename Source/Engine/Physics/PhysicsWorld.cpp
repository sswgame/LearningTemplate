#include "pch.h"

#include "Engine/Physics/PhysicsWorld.h"

#include "Core/Math/MathUtil.h"

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
             *          이고, NaN 도 마찬가지다. x86 에서 그 캐스팅은 넘치든 모자라든 똑같이 int32
             *          최솟값으로 붙어 버리므로, `±FLT_MAX` 처럼 아주 넓은 AABB 의 최소·최대가 **같은
             *          셀 번호**가 된다 — 폭이 1 로 읽혀서 "너무 크다" 판정을 통과하고, 그 바디가
             *          원점 근처가 아닌 엉뚱한 셀 **하나**에만 등록된다. 그러면 그 바디가 겹치는
             *          셀을 보는 질의가 바디를 못 찾는다(조용한 충돌 누락).
             *
             *          범위 안으로 접어 넣으면 넓은 AABB 는 넓은 셀 범위가 되고, 셀 수 상한에 걸려
             *          `_listOversizedBody` 로 간다 — 질의가 항상 함께 보는 자리다. 나눗셈을
             *          float64 로 하는 이유는 float64 가 모든 int32 를 정확히 담아서 경계 비교가
             *          어긋나지 않기 때문이다.
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
        // 뒤집힌 AABB(min > max)도 받는다 — 호출부마다 정규화를 적으면 그중 하나가 빠진다.
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
        // `kMaxBodyCellCount` 검사를 통과해 **막으려던 순회를 그대로 돌게 된다** — 무한대 AABB 하나가
        // 그리드 삽입을 돌아오지 않게 만든다. 호출부는 이 값을 상한과 견주기만 하므로, 넘칠 때는
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
     * @brief 대상 AABB가 점유하는 모든 3D 그리드 셀에 바디 핸들을 등록합니다.
     */
    void PhysicsWorld::insertBodyToGrid( BodyHandle handle, const AABB& aabb )
    {
        if ( aabb.isValid() == false )
            return;

        // 너무 큰 바디는 셀마다 적지 않는다 — 적으면 한 바디가 셀 표를 통째로 불린다.
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
     * @brief 대상 AABB가 점유하던 그리드 셀들에서 바디 핸들을 안전하게 제거합니다.
     */
    void PhysicsWorld::removeBodyFromGrid( BodyHandle handle, const AABB& aabb )
    {
        if ( aabb.isValid() == false )
            return;

        // **넣을 때와 같은 판단을 쓴다.** 크기 판정은 AABB 만으로 정해지므로 넣을 때와 뺄 때가
        // 반드시 같은 답을 낸다 — 다르면 큰 바디가 목록에 영원히 남거나, 셀에 죽은 핸들이 남는다.
        if ( isOversizedForGrid( aabb ) )
        {
            for ( size_t slot = 0; slot < _listOversizedBody.size(); ++slot )
            {
                if ( _listOversizedBody[slot] != handle )
                    continue;
                _listOversizedBody[slot] = _listOversizedBody.back();
                _listOversizedBody.pop_back();
                break;
            }
            return;
        }

        // **넣을 때와 같은 범위를 훑는다** — 이것이 이 타입이 있는 이유다. 덜 훑으면 죽은 핸들이 남는다.
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

    /**
     * @brief 새로운 물리 바디를 월드 풀에 등록하고 공간 그리드에 배치합니다.
     */
    PhysicsWorld::BodyHandle PhysicsWorld::addBody( const AABB& aabb, uint8 layer, uint64 objectId )
    {
        PhysicsBody body{};
        body._aabb     = aabb;
        body._layer    = layer;
        body._objectId = objectId;
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        BodyHandle                          handle = _bodies.insert( body );
        insertBodyToGrid( handle, aabb );
        return handle;
    }

    /**
     * @brief 물리 바디를 월드에서 제거하고 공간 그리드에서 매핑을 해제합니다.
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
     * @brief 바디의 위치/크기(AABB)를 갱신하고 공간 그리드 셀 점유 상태를 재배치합니다.
     */
    void PhysicsWorld::setAabb( BodyHandle handle, const AABB& aabb )
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        PhysicsBody*                        pBody = _bodies.get( handle );
        if ( pBody == nullptr )
            return;

        const AABB oldAABB = pBody->_aabb;
        if ( oldAABB.isValid() && aabb.isValid() )
        {
            // 지름길: 덮는 셀이 그대로면 그리드를 건드릴 필요가 없다. 이 판단이 삽입·제거와 **같은
            // 계산**을 써야 한다 — 아니면 바디가 틀린 셀에 앉은 채로 남는다.
            const CellRange oldRange = CellRange::fromAabb( oldAABB, kCellSize );
            const CellRange newRange = CellRange::fromAabb( aabb, kCellSize );
            if ( oldRange == newRange )
            {
                pBody->_aabb = aabb;
                return;
            }
        }

        removeBodyFromGrid( handle, pBody->_aabb );
        pBody->_aabb = aabb;
        insertBodyToGrid( handle, aabb );
    }

    /**
     * @brief 바디 핸들로부터 물리 바디 정보를 스레드 안전하게 복사 조회합니다.
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
     * @brief 물리 시뮬레이션 한 단계를 진행합니다.
     */
    void PhysicsWorld::step( float32 deltaTime )
    {
        (void)deltaTime;
    }

    /**
     * @brief 두 바디 간의 레이어 충돌 마스크 및 AABB 교차 여부를 검사합니다.
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
     * @brief 특정 3D 바운딩 박스(AABB)와 교차하는 모든 물리 바디들을 공간 그리드를 통해 고속 검색합니다.
     *
     * 1. 박스가 걸치는 공간 그리드 셀들을 순회하며 중복 없는 후보 바디 목록을 수집.
     * 2. 후보 바디들에 대해 레이어 마스크 및 정밀 AABB 교차 검사를 수행하여 outHandles에 저장.
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
            _bodies.forEachHandle( [&]( ObjectHandle handle, const PhysicsBody& body )
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
            _bodies.forEachHandle( [&]( ObjectHandle handle, const PhysicsBody& body )
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
