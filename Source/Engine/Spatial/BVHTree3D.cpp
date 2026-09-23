#include "pch.h"

#include "Engine/Spatial/BVHTree3D.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/Math.h"

namespace sw
{
    namespace
    {
        struct BVHTree3DInternal
        {
            static constexpr float32 kSurfaceAreaFactor = 2.0f;
            /**
             * @brief 순회 스택의 칸 수. 트리는 삽입 · 삭제마다 회전으로 균형을 잡으므로 높이는 원소 수의 로그로 자라고,
             *        깊이 우선 스택은 높이 + 1 을 넘지 않는다 — 256 이면 원소 수와 무관하게 남는다.
             */
            static constexpr int32 kTraversalStackCapacity = static_cast<int32>( constant::kMaxBuffer256 );

            /**
             * @brief 경계가 `overlaps` 를 만족하는 잎의 핸들을 모읍니다 — 네 질의(상자 · 광선 · 구 · 절두체)의 공통 순회.
             * @details 예전에는 네 질의가 이 스무 줄을 각자 들고 판정식만 달랐다. 스택이 차면 자식을 **조용히 버렸다** — 균형
             *          트리에서는 닿지 않는 자리지만, 닿으면 질의 결과가 빠지므로 이제는 단언이 알린다.
             */
            template <typename OverlapFn>
            static void collectOverlapping( const vector<BVHNode3D>& listNode, int32 rootIndex, OverlapFn&& overlaps,
                                            vector<SlotHandle>& outListHandle )
            {
                if ( rootIndex == invalid_index::kInt32 )
                    return;

                int32 arrStack[kTraversalStackCapacity];
                int32 stackCount       = 0;
                arrStack[stackCount++] = rootIndex;
                while ( stackCount > 0 )
                {
                    const BVHNode3D& node = listNode[static_cast<size_t>( arrStack[--stackCount] )];
                    if ( overlaps( node._bounds ) == false )
                        continue;
                    if ( node.isLeaf() )
                    {
                        outListHandle.push_back( node._handle );
                        continue;
                    }
                    SW_ASSERT( stackCount + 2 <= kTraversalStackCapacity );
                    if ( node._leftChild != invalid_index::kInt32 && stackCount < kTraversalStackCapacity )
                        arrStack[stackCount++] = node._leftChild;
                    if ( node._rightChild != invalid_index::kInt32 && stackCount < kTraversalStackCapacity )
                        arrStack[stackCount++] = node._rightChild;
                }
            }

            /**
             * @brief 광선을 축 하나의 슬랩으로 잘라 [inoutNear, inoutFar] 를 좁힙니다. 이 축에서 빗나가면 false.
             * @details 슬랩 검사는 축마다 똑같다 — 예전에는 이 열다섯 줄이 축마다 한 벌씩 세 벌이었다(`Physics/CCD.cpp` 가
             *          같은 이유로 `clipSlab` 하나로 모은 모양이다). 이 축으로 나아가지 않으면 시작 좌표가 슬랩 안에 있는지만 본다.
             */
            static bool clipRaySlab( float32 origin, float32 direction, float32 slabMin, float32 slabMax, float32& inoutNear,
                                     float32& inoutFar )
            {
                if ( MathUtil::abs( direction ) < MathUtil::Epsilon )
                    return slabMin <= origin && origin <= slabMax;

                const float32 invDirection = 1.0f / direction;
                float32       tEnter       = ( slabMin - origin ) * invDirection;
                float32       tExit        = ( slabMax - origin ) * invDirection;
                if ( tEnter > tExit )
                    std::swap( tEnter, tExit );
                inoutNear = MathUtil::max( inoutNear, tEnter );
                inoutFar  = MathUtil::min( inoutFar, tExit );
                return inoutNear <= inoutFar;
            }
        };
    } // namespace

    BVHTree3D::BVHTree3D()
        : _listNode{}
        , _listFreeNode{}
        , _mapHandleToNode{}
        , _rootIndex{ invalid_index::kInt32 }
    {
    }

    AABB BVHTree3D::combineAabb( const AABB& a, const AABB& b )
    {
        return AABB{
            float3::min( a._min, b._min ),
            float3::max( a._max, b._max ) };
    }

    float32 BVHTree3D::getSurfaceArea( const AABB& box )
    {
        const float3 extents = box._max - box._min;
        return BVHTree3DInternal::kSurfaceAreaFactor * ( extents._x * extents._y + extents._y * extents._z + extents._z * extents._x );
    }

    int32 BVHTree3D::allocateNode()
    {
        if ( _listFreeNode.empty() == false )
        {
            const int32 nodeIndex = _listFreeNode.back();
            _listFreeNode.pop_back();
            _listNode[static_cast<size_t>( nodeIndex )] = BVHNode3D{};
            return nodeIndex;
        }

        const int32 nodeIndex = static_cast<int32>( _listNode.size() );
        _listNode.emplace_back();
        return nodeIndex;
    }

    void BVHTree3D::freeNode( int32 nodeIndex )
    {
        if ( 0 <= nodeIndex && static_cast<size_t>( nodeIndex ) < _listNode.size() )
        {
            _listNode[static_cast<size_t>( nodeIndex )]._parent     = invalid_index::kInt32;
            _listNode[static_cast<size_t>( nodeIndex )]._leftChild  = invalid_index::kInt32;
            _listNode[static_cast<size_t>( nodeIndex )]._rightChild = invalid_index::kInt32;
            _listNode[static_cast<size_t>( nodeIndex )]._handle     = SlotHandle{};
            _listFreeNode.push_back( nodeIndex );
        }
    }

    int32 BVHTree3D::insert( SlotHandle handle, const AABB& bounds )
    {
        if ( handle.isValid() == false )
            return invalid_index::kInt32;

        if ( _mapHandleToNode.find( handle ) != _mapHandleToNode.end() )
            remove( handle );

        const int32 leafIndex = allocateNode();
        BVHNode3D&  leaf      = _listNode[static_cast<size_t>( leafIndex )];
        leaf._bounds          = bounds;
        leaf._handle          = handle;
        leaf._height          = 0;

        insertLeaf( leafIndex );
        _mapHandleToNode[handle] = leafIndex;
        return leafIndex;
    }

    void BVHTree3D::update( SlotHandle handle, const AABB& bounds )
    {
        insert( handle, bounds );
    }

    void BVHTree3D::remove( SlotHandle handle )
    {
        auto it = _mapHandleToNode.find( handle );
        if ( it == _mapHandleToNode.end() )
            return;

        const int32 leafIndex = it->second;
        removeLeaf( leafIndex );
        freeNode( leafIndex );
        _mapHandleToNode.erase( it );
    }

    void BVHTree3D::clear()
    {
        _rootIndex = invalid_index::kInt32;
        _listNode.clear();
        _listFreeNode.clear();
        _mapHandleToNode.clear();
    }

    void BVHTree3D::insertLeaf( int32 leafIndex )
    {
        if ( _rootIndex == invalid_index::kInt32 )
        {
            _rootIndex                                           = leafIndex;
            _listNode[static_cast<size_t>( _rootIndex )]._parent = invalid_index::kInt32;
            return;
        }

        // Surface Area Heuristic (SAH) to find best sibling
        const AABB leafAABB = _listNode[static_cast<size_t>( leafIndex )]._bounds;
        int32      index    = _rootIndex;

        while ( _listNode[static_cast<size_t>( index )].isLeaf() == false )
        {
            const int32 leftChild  = _listNode[static_cast<size_t>( index )]._leftChild;
            const int32 rightChild = _listNode[static_cast<size_t>( index )]._rightChild;

            const float32 area         = getSurfaceArea( _listNode[static_cast<size_t>( index )]._bounds );
            const AABB    combinedAABB = combineAabb( _listNode[static_cast<size_t>( index )]._bounds, leafAABB );
            const float32 combinedArea = getSurfaceArea( combinedAABB );

            const float32 cost            = BVHTree3DInternal::kSurfaceAreaFactor * combinedArea;
            const float32 inheritanceCost = BVHTree3DInternal::kSurfaceAreaFactor * ( combinedArea - area );

            // Cost of descending into left child
            float32 costLeft = 0.0f;
            if ( _listNode[static_cast<size_t>( leftChild )].isLeaf() )
            {
                const AABB aabb = combineAabb( _listNode[static_cast<size_t>( leftChild )]._bounds, leafAABB );
                costLeft        = getSurfaceArea( aabb ) + inheritanceCost;
            }
            else
            {
                const AABB    aabb    = combineAabb( _listNode[static_cast<size_t>( leftChild )]._bounds, leafAABB );
                const float32 oldArea = getSurfaceArea( _listNode[static_cast<size_t>( leftChild )]._bounds );
                const float32 newArea = getSurfaceArea( aabb );
                costLeft              = ( newArea - oldArea ) + inheritanceCost;
            }

            // Cost of descending into right child
            float32 costRight = 0.0f;
            if ( _listNode[static_cast<size_t>( rightChild )].isLeaf() )
            {
                const AABB aabb = combineAabb( _listNode[static_cast<size_t>( rightChild )]._bounds, leafAABB );
                costRight       = getSurfaceArea( aabb ) + inheritanceCost;
            }
            else
            {
                const AABB    aabb    = combineAabb( _listNode[static_cast<size_t>( rightChild )]._bounds, leafAABB );
                const float32 oldArea = getSurfaceArea( _listNode[static_cast<size_t>( rightChild )]._bounds );
                const float32 newArea = getSurfaceArea( aabb );
                costRight             = ( newArea - oldArea ) + inheritanceCost;
            }

            if ( cost < costLeft && cost < costRight )
                break;

            index = ( costLeft < costRight ) ? leftChild : rightChild;
        }

        const int32 sibling = index;

        // Create a new parent node
        const int32 oldParent = _listNode[static_cast<size_t>( sibling )]._parent;
        const int32 newParent = allocateNode();

        _listNode[static_cast<size_t>( newParent )]._parent     = oldParent;
        _listNode[static_cast<size_t>( newParent )]._bounds     = combineAabb( leafAABB, _listNode[static_cast<size_t>( sibling )]._bounds );
        _listNode[static_cast<size_t>( newParent )]._height     = _listNode[static_cast<size_t>( sibling )]._height + 1;
        _listNode[static_cast<size_t>( newParent )]._leftChild  = sibling;
        _listNode[static_cast<size_t>( newParent )]._rightChild = leafIndex;

        _listNode[static_cast<size_t>( sibling )]._parent   = newParent;
        _listNode[static_cast<size_t>( leafIndex )]._parent = newParent;

        if ( oldParent != invalid_index::kInt32 )
        {
            if ( _listNode[static_cast<size_t>( oldParent )]._leftChild == sibling )
                _listNode[static_cast<size_t>( oldParent )]._leftChild = newParent;
            else
                _listNode[static_cast<size_t>( oldParent )]._rightChild = newParent;
        }
        else
        {
            _rootIndex = newParent;
        }

        // Walk back up the tree refitting AABBs and balancing
        index = _listNode[static_cast<size_t>( leafIndex )]._parent;
        while ( index != invalid_index::kInt32 )
        {
            index = balance( index );

            const int32 leftChild  = _listNode[static_cast<size_t>( index )]._leftChild;
            const int32 rightChild = _listNode[static_cast<size_t>( index )]._rightChild;

            _listNode[static_cast<size_t>( index )]._height = 1 + MathUtil::max(
                                                                      _listNode[static_cast<size_t>( leftChild )]._height,
                                                                      _listNode[static_cast<size_t>( rightChild )]._height );
            _listNode[static_cast<size_t>( index )]._bounds = combineAabb(
                _listNode[static_cast<size_t>( leftChild )]._bounds,
                _listNode[static_cast<size_t>( rightChild )]._bounds );

            index = _listNode[static_cast<size_t>( index )]._parent;
        }
    }

    void BVHTree3D::removeLeaf( int32 leafIndex )
    {
        if ( leafIndex == _rootIndex )
        {
            _rootIndex = invalid_index::kInt32;
            return;
        }

        const int32 parent      = _listNode[static_cast<size_t>( leafIndex )]._parent;
        const int32 grandParent = _listNode[static_cast<size_t>( parent )]._parent;
        const int32 sibling     = ( _listNode[static_cast<size_t>( parent )]._leftChild == leafIndex )
                                    ? _listNode[static_cast<size_t>( parent )]._rightChild
                                    : _listNode[static_cast<size_t>( parent )]._leftChild;

        if ( grandParent != invalid_index::kInt32 )
        {
            if ( _listNode[static_cast<size_t>( grandParent )]._leftChild == parent )
                _listNode[static_cast<size_t>( grandParent )]._leftChild = sibling;
            else
                _listNode[static_cast<size_t>( grandParent )]._rightChild = sibling;

            _listNode[static_cast<size_t>( sibling )]._parent = grandParent;
            freeNode( parent );

            int32 index = grandParent;
            while ( index != invalid_index::kInt32 )
            {
                index = balance( index );

                const int32 leftChild  = _listNode[static_cast<size_t>( index )]._leftChild;
                const int32 rightChild = _listNode[static_cast<size_t>( index )]._rightChild;

                _listNode[static_cast<size_t>( index )]._bounds = combineAabb(
                    _listNode[static_cast<size_t>( leftChild )]._bounds,
                    _listNode[static_cast<size_t>( rightChild )]._bounds );
                _listNode[static_cast<size_t>( index )]._height = 1 + MathUtil::max(
                                                                          _listNode[static_cast<size_t>( leftChild )]._height,
                                                                          _listNode[static_cast<size_t>( rightChild )]._height );

                index = _listNode[static_cast<size_t>( index )]._parent;
            }
        }
        else
        {
            _rootIndex                                        = sibling;
            _listNode[static_cast<size_t>( sibling )]._parent = invalid_index::kInt32;
            freeNode( parent );
        }
    }

    int32 BVHTree3D::balance( int32 nodeIndex )
    {
        BVHNode3D& A = _listNode[static_cast<size_t>( nodeIndex )];
        if ( A.isLeaf() || A._height < 2 )
            return nodeIndex;

        const int32 iB = A._leftChild;
        const int32 iC = A._rightChild;

        BVHNode3D& B = _listNode[static_cast<size_t>( iB )];
        BVHNode3D& C = _listNode[static_cast<size_t>( iC )];

        const int32 balanceFactor = C._height - B._height;

        // Rotate C up
        if ( balanceFactor > 1 )
        {
            const int32 iF = C._leftChild;
            const int32 iG = C._rightChild;
            BVHNode3D&  F  = _listNode[static_cast<size_t>( iF )];
            BVHNode3D&  G  = _listNode[static_cast<size_t>( iG )];

            C._leftChild = nodeIndex;
            C._parent    = A._parent;
            A._parent    = iC;

            if ( C._parent != invalid_index::kInt32 )
            {
                if ( _listNode[static_cast<size_t>( C._parent )]._leftChild == nodeIndex )
                    _listNode[static_cast<size_t>( C._parent )]._leftChild = iC;
                else
                    _listNode[static_cast<size_t>( C._parent )]._rightChild = iC;
            }
            else
            {
                _rootIndex = iC;
            }

            if ( F._height > G._height )
            {
                C._rightChild = iF;
                A._rightChild = iG;
                G._parent     = nodeIndex;
                A._bounds     = combineAabb( B._bounds, G._bounds );
                C._bounds     = combineAabb( A._bounds, F._bounds );

                A._height = 1 + MathUtil::max( B._height, G._height );
                C._height = 1 + MathUtil::max( A._height, F._height );
            }
            else
            {
                C._rightChild = iG;
                A._rightChild = iF;
                F._parent     = nodeIndex;
                A._bounds     = combineAabb( B._bounds, F._bounds );
                C._bounds     = combineAabb( A._bounds, G._bounds );

                A._height = 1 + MathUtil::max( B._height, F._height );
                C._height = 1 + MathUtil::max( A._height, G._height );
            }

            return iC;
        }

        // Rotate B up
        if ( balanceFactor < -1 )
        {
            const int32 iD = B._leftChild;
            const int32 iE = B._rightChild;
            BVHNode3D&  D  = _listNode[static_cast<size_t>( iD )];
            BVHNode3D&  E  = _listNode[static_cast<size_t>( iE )];

            B._leftChild = nodeIndex;
            B._parent    = A._parent;
            A._parent    = iB;

            if ( B._parent != invalid_index::kInt32 )
            {
                if ( _listNode[static_cast<size_t>( B._parent )]._leftChild == nodeIndex )
                    _listNode[static_cast<size_t>( B._parent )]._leftChild = iB;
                else
                    _listNode[static_cast<size_t>( B._parent )]._rightChild = iB;
            }
            else
            {
                _rootIndex = iB;
            }

            if ( D._height > E._height )
            {
                B._rightChild = iD;
                A._leftChild  = iE;
                E._parent     = nodeIndex;
                A._bounds     = combineAabb( C._bounds, E._bounds );
                B._bounds     = combineAabb( A._bounds, D._bounds );

                A._height = 1 + MathUtil::max( C._height, E._height );
                B._height = 1 + MathUtil::max( A._height, D._height );
            }
            else
            {
                B._rightChild = iE;
                A._leftChild  = iD;
                D._parent     = nodeIndex;
                A._bounds     = combineAabb( C._bounds, D._bounds );
                B._bounds     = combineAabb( A._bounds, E._bounds );

                A._height = 1 + MathUtil::max( C._height, D._height );
                B._height = 1 + MathUtil::max( A._height, E._height );
            }

            return iB;
        }

        return nodeIndex;
    }

    void BVHTree3D::queryAabb( const AABB& queryBox, vector<SlotHandle>& outListHandle ) const
    {
        // **먼저 비운다.** 이 네 질의는 결과를 덧붙이기만 했고, 게다가 트리가 비면 아무것도 건드리지
        // 않고 돌아갔다 — 호출부가 벡터 하나를 돌려 쓰면 지난 질의의 답이 이번 답인 척 남는다.
        // 형제들(`SpatialHashGrid2D` · `PhysicsWorld`)은 이미 비우고 시작한다.
        outListHandle.clear();
        BVHTree3DInternal::collectOverlapping( _listNode, _rootIndex, [&queryBox]( const AABB& box )
        { return box.intersects( queryBox ); },
                                               outListHandle );
    }

    void BVHTree3D::queryRay( const float3& origin, const float3& direction, float32 maxDist, vector<SlotHandle>& outListHandle ) const
    {
        outListHandle.clear();
        if ( maxDist <= 0.0f )
            return;

        // **방향을 단위 길이로 맞춘다.** `maxDist` 는 이름 그대로 거리인데, 슬랩 판정은 `tMax = maxDist` 를 방향 벡터
        // 배수로 쓴다 — 정규화하지 않으면 같은 인자가 방향 길이에 따라 다른 사거리를 뜻한다(길이 2 짜리 방향이면 사거리가
        // 두 배가 된다). 형제 `SpatialHashGrid2D::queryRay` 는 이미 정규화하고 있었고, 두 자료구조의 같은 인자가 서로
        // 다른 뜻이었다.
        float3 unitDirection = direction;
        if ( unitDirection.getLengthSquared() <= MathUtil::Epsilon )
            return;
        unitDirection.normalize();

        auto rayIntersects = [&]( const AABB& box ) -> bool
        {
            float32 tNear = 0.0f;
            float32 tFar  = maxDist;
            return BVHTree3DInternal::clipRaySlab( origin._x, unitDirection._x, box._min._x, box._max._x, tNear, tFar ) &&
                   BVHTree3DInternal::clipRaySlab( origin._y, unitDirection._y, box._min._y, box._max._y, tNear, tFar ) &&
                   BVHTree3DInternal::clipRaySlab( origin._z, unitDirection._z, box._min._z, box._max._z, tNear, tFar );
        };
        BVHTree3DInternal::collectOverlapping( _listNode, _rootIndex, rayIntersects, outListHandle );
    }

    void BVHTree3D::querySphere( const float3& center, float32 radius, vector<SlotHandle>& outListHandle ) const
    {
        outListHandle.clear();
        if ( radius <= 0.0f )
            return;

        const float32 radiusSquared    = radius * radius;
        auto          sphereIntersects = [&]( const AABB& box ) -> bool
        {
            const float3 closestPoint = center.clamped( box._min, box._max );
            return float3::getDistanceSquared( center, closestPoint ) <= radiusSquared;
        };
        BVHTree3DInternal::collectOverlapping( _listNode, _rootIndex, sphereIntersects, outListHandle );
    }

    void BVHTree3D::queryFrustum( const float4x4& viewProj, vector<SlotHandle>& outListHandle ) const
    {
        outListHandle.clear();
        // 평면 추출은 렌더러의 GPU 컬링과 같은 `Frustum` 하나다 — 예전에는 여기에 같은 식의 사본이 있었다.
        const Frustum frustum = Frustum::fromViewProjection( viewProj );
        BVHTree3DInternal::collectOverlapping( _listNode, _rootIndex, [&frustum]( const AABB& box )
        { return frustum.overlapsBox( box._min, box._max ); },
                                               outListHandle );
    }

    size_t BVHTree3D::getHandleCount() const
    {
        return _mapHandleToNode.size();
    }

    size_t BVHTree3D::getNodeCount() const
    {
        return _listNode.size() - _listFreeNode.size();
    }

    int32 BVHTree3D::getTreeHeight() const
    {
        if ( _rootIndex == invalid_index::kInt32 )
            return 0;
        return _listNode[static_cast<size_t>( _rootIndex )]._height;
    }
} // namespace sw
