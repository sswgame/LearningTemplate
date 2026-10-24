#include "pch.h"

#include "Core/Spatial/BVHTree3D.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/IndexFreeList.h"
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
             * @brief 순회 스택의 칸 수입니다. 트리는 삽입 · 삭제마다 회전으로 균형을 잡으므로 높이는 원소 수의 로그로 자라고,
             *        깊이 우선 스택은 높이 + 1 을 넘지 않습니다. 256 이면 원소 수와 상관없이 남습니다.
             */
            static constexpr int32 kTraversalStackCapacity = static_cast<int32>( constant::kMaxBuffer256 );

            /**
             * @brief 경계가 `overlaps` 를 만족하는 잎의 핸들을 모읍니다. 네 질의(상자 · 광선 · 구 · 절두체)의 공통 순회입니다.
             * @details 스택이 차면 자식을 버리게 되므로 단언이 알립니다(균형 트리에서는 닿지 않는 자리지만, 닿으면 질의 결과가 빠집니다).
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
             * @brief 광선을 축 하나의 슬랩으로 잘라 [inoutNear, inoutFar] 를 좁힙니다. 이 축에서 빗나가면 false 입니다.
             * @details 슬랩 검사는 축마다 똑같아 세 축이 이것 하나를 씁니다(`Physics/Collision/ContinuousCollision.cpp` 의 `clipSlab` 과 같은 모양).
             *          이 축으로 나아가지 않으면 시작 좌표가 슬랩 안에 있는지만 봅니다.
             */
            static bool clipRaySlab( float32 origin, float32 direction, float32 slabMin, float32 slabMax, float32& inoutNear,
                                     float32& inoutFar )
            {
                if ( MathUtil::abs( direction ) < MathUtil::kEpsilon )
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

    AABB BVHTree3D::combineAABB( const AABB& a, const AABB& b )
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
        // 재사용 칸도 새 칸도 기본 노드로 채운다.
        return static_cast<int32>( allocateFreeListIndex( _listNode, _listFreeNode, BVHNode3D{} ) );
    }

    void BVHTree3D::freeNode( int32 nodeIndex )
    {
        if ( 0 <= nodeIndex && static_cast<size_t>( nodeIndex ) < _listNode.size() )
        {
            _listNode[static_cast<size_t>( nodeIndex )]._parent     = invalid_index::kInt32;
            _listNode[static_cast<size_t>( nodeIndex )]._leftChild  = invalid_index::kInt32;
            _listNode[static_cast<size_t>( nodeIndex )]._rightChild = invalid_index::kInt32;
            _listNode[static_cast<size_t>( nodeIndex )]._handle     = SlotHandle{};
            _listFreeNode.push_back( static_cast<uint32>( nodeIndex ) );
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

    float32 BVHTree3D::computeDescentCost( int32 childIndex, const AABB& leafAABB, float32 inheritanceCost ) const
    {
        const BVHNode3D& child = _listNode[static_cast<size_t>( childIndex )];
        const AABB       aabb  = combineAABB( child._bounds, leafAABB );
        if ( child.isLeaf() )
            return getSurfaceArea( aabb ) + inheritanceCost;
        return ( getSurfaceArea( aabb ) - getSurfaceArea( child._bounds ) ) + inheritanceCost;
    }

    int32 BVHTree3D::findBestSibling( const AABB& leafAABB ) const
    {
        // 표면적 휴리스틱(SAH)으로 가장 좋은 형제 노드를 찾는다
        int32 index = _rootIndex;
        while ( _listNode[static_cast<size_t>( index )].isLeaf() == false )
        {
            const int32 leftChild  = _listNode[static_cast<size_t>( index )]._leftChild;
            const int32 rightChild = _listNode[static_cast<size_t>( index )]._rightChild;

            const float32 area         = getSurfaceArea( _listNode[static_cast<size_t>( index )]._bounds );
            const AABB    combinedAABB = combineAABB( _listNode[static_cast<size_t>( index )]._bounds, leafAABB );
            const float32 combinedArea = getSurfaceArea( combinedAABB );

            const float32 cost            = BVHTree3DInternal::kSurfaceAreaFactor * combinedArea;
            const float32 inheritanceCost = BVHTree3DInternal::kSurfaceAreaFactor * ( combinedArea - area );

            // 각 자식으로 내려갈 때의 비용
            const float32 costLeft  = computeDescentCost( leftChild, leafAABB, inheritanceCost );
            const float32 costRight = computeDescentCost( rightChild, leafAABB, inheritanceCost );
            if ( cost < costLeft && cost < costRight )
                break;

            index = ( costLeft < costRight ) ? leftChild : rightChild;
        }
        return index;
    }

    void BVHTree3D::refitUpward( int32 startIndex )
    {
        int32 index = startIndex;
        while ( index != invalid_index::kInt32 )
        {
            index = balance( index );

            const int32 leftChild  = _listNode[static_cast<size_t>( index )]._leftChild;
            const int32 rightChild = _listNode[static_cast<size_t>( index )]._rightChild;

            _listNode[static_cast<size_t>( index )]._height = 1 + MathUtil::max( _listNode[static_cast<size_t>( leftChild )]._height, _listNode[static_cast<size_t>( rightChild )]._height );
            _listNode[static_cast<size_t>( index )]._bounds = combineAABB( _listNode[static_cast<size_t>( leftChild )]._bounds, _listNode[static_cast<size_t>( rightChild )]._bounds );

            index = _listNode[static_cast<size_t>( index )]._parent;
        }
    }

    void BVHTree3D::insertLeaf( int32 leafIndex )
    {
        if ( _rootIndex == invalid_index::kInt32 )
        {
            _rootIndex                                           = leafIndex;
            _listNode[static_cast<size_t>( _rootIndex )]._parent = invalid_index::kInt32;
            return;
        }

        const AABB  leafAABB = _listNode[static_cast<size_t>( leafIndex )]._bounds;
        const int32 sibling  = findBestSibling( leafAABB );

        // 새 부모 노드를 만든다
        const int32 oldParent = _listNode[static_cast<size_t>( sibling )]._parent;
        const int32 newParent = allocateNode();

        _listNode[static_cast<size_t>( newParent )]._parent     = oldParent;
        _listNode[static_cast<size_t>( newParent )]._bounds     = combineAABB( leafAABB, _listNode[static_cast<size_t>( sibling )]._bounds );
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

        // 트리를 거슬러 올라가며 AABB 를 다시 맞추고 균형을 잡는다
        refitUpward( _listNode[static_cast<size_t>( leafIndex )]._parent );
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
            refitUpward( grandParent );
        }
        else
        {
            _rootIndex                                        = sibling;
            _listNode[static_cast<size_t>( sibling )]._parent = invalid_index::kInt32;
            freeNode( parent );
        }
    }

    void BVHTree3D::rotateUp( int32 nodeIndex, int32 upIndex, int32 stayIndex, bool bUpIsRight )
    {
        BVHNode3D&  A  = _listNode[static_cast<size_t>( nodeIndex )];
        BVHNode3D&  U  = _listNode[static_cast<size_t>( upIndex )];
        BVHNode3D&  S  = _listNode[static_cast<size_t>( stayIndex )];
        const int32 iX = U._leftChild;
        const int32 iY = U._rightChild;
        BVHNode3D&  X  = _listNode[static_cast<size_t>( iX )];
        BVHNode3D&  Y  = _listNode[static_cast<size_t>( iY )];

        U._leftChild = nodeIndex;
        U._parent    = A._parent;
        A._parent    = upIndex;

        if ( U._parent != invalid_index::kInt32 )
        {
            if ( _listNode[static_cast<size_t>( U._parent )]._leftChild == nodeIndex )
                _listNode[static_cast<size_t>( U._parent )]._leftChild = upIndex;
            else
                _listNode[static_cast<size_t>( U._parent )]._rightChild = upIndex;
        }
        else
        {
            _rootIndex = upIndex;
        }

        // 키가 더 큰 손자는 올라간 노드에 남고, 작은 손자가 A 로 옮겨 A 의 빈 자리(올라간 쪽)를 채운다.
        const bool  bKeepX = X._height > Y._height;
        BVHNode3D&  kept   = bKeepX ? X : Y;
        BVHNode3D&  moved  = bKeepX ? Y : X;
        const int32 iKept  = bKeepX ? iX : iY;
        const int32 iMoved = bKeepX ? iY : iX;
        U._rightChild      = iKept;
        if ( bUpIsRight )
            A._rightChild = iMoved;
        else
            A._leftChild = iMoved;
        moved._parent = nodeIndex;
        A._bounds     = combineAABB( S._bounds, moved._bounds );
        U._bounds     = combineAABB( A._bounds, kept._bounds );
        A._height     = 1 + MathUtil::max( S._height, moved._height );
        U._height     = 1 + MathUtil::max( A._height, kept._height );
    }

    int32 BVHTree3D::balance( int32 nodeIndex )
    {
        const BVHNode3D& A = _listNode[static_cast<size_t>( nodeIndex )];
        if ( A.isLeaf() || A._height < 2 )
            return nodeIndex;

        const int32 iB            = A._leftChild;
        const int32 iC            = A._rightChild;
        const int32 balanceFactor = _listNode[static_cast<size_t>( iC )]._height - _listNode[static_cast<size_t>( iB )]._height;
        if ( balanceFactor > 1 )
        {
            rotateUp( nodeIndex, iC, iB, true ); // C 를 위로 회전한다
            return iC;
        }
        if ( balanceFactor < -1 )
        {
            rotateUp( nodeIndex, iB, iC, false ); // B 를 위로 회전한다
            return iB;
        }
        return nodeIndex;
    }

    void BVHTree3D::queryAABB( const AABB& queryBox, vector<SlotHandle>& outListHandle ) const
    {
        // **먼저 비운다**(트리가 비어도). 덧붙이기만 하면 부르는 쪽이 벡터 하나를 돌려 쓸 때 지난 질의의 답이 이번 답인 척 남는다.
        // 형제들(`SpatialHashGrid2D`)도 비우고 시작한다.
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
        // 배수로 쓴다. 정규화하지 않으면 같은 인자가 방향 길이에 따라 다른 사거리를 뜻한다(길이 2 짜리 방향이면 사거리가
        // 두 배가 된다). 형제 `SpatialHashGrid2D::queryRay` 도 정규화한다 — 두 자료구조의 같은 인자가 같은 뜻이어야 한다.
        float3 unitDirection = direction;
        if ( unitDirection.getLengthSquared() <= MathUtil::kEpsilon )
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
        // 평면 추출은 렌더러의 GPU 컬링과 같은 `Frustum` 하나다.
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
