/**
 * @file ConformSurface.h
 * @brief Conform 계열이 쓰는 삼각형 표면 BVH 입니다. 질의가 상태 없이 const 라서 정점 덩어리마다 다른 스레드에서 물을 수 있고, 같은 삼각형 목록이면 BVH 를 다시 만들지 않습니다.
 * @details 엔진의 `SurfaceBVH` 는 질의 후보를 멤버 스크래치에 받아 한 스레드만 묻는다. 이 클래스는 같은 규칙(상자 여유 · 양면 교차 · 최근접점)을 쓰되
 *          후보 목록을 부르는 쪽이 넘기게 해서 그 제약을 없앴다. 모디파이어 인스턴스마다 하나를 가지고(`mutable`), 적용 때마다
 *          `assign` 으로 삼각형을 넘기면 지난번과 한 바이트도 다르지 않을 때만 BVH 를 재사용한다 — 해시가 아니라 값 비교라 충돌로 낡은 BVH 를 쓰는 일이 없다.
 *          `assign` 은 한 스레드에서(적용 시작에), 질의는 여러 스레드에서 부른다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/Spatial/BVHTree3D.h"

#include "Engine/Character/Fit/CharacterGeometry.h"

namespace sw::editor
{
    /** @brief 삼각형 목록(정점 3 개씩) 하나의 BVH 입니다. */
    class ConformSurface
    {
    public:
        ConformSurface()
            : _listTriangle{}
            , _tree{}
            , _buildCount{ 0 }
        {
        }

        /**
         * @brief 삼각형 목록을 넘깁니다. 지난번 목록과 같으면 BVH 를 그대로 두고, 다르면 다시 만듭니다.
         * @return BVH 를 새로 만들었으면 true 입니다.
         */
        bool assign( const vector<float3>& listTriangle )
        {
            const size_t byteCount = listTriangle.size() * sizeof( float3 );
            const bool   bSameSize = _buildCount > 0 && listTriangle.size() == _listTriangle.size();
            if ( bSameSize && std::memcmp( listTriangle.data(), _listTriangle.data(), byteCount ) == 0 )
                return false;
            _listTriangle = listTriangle;
            _tree.clear();
            const float3 padding( kBoundsPadding );
            for ( uint32 triangle = 0; static_cast<size_t>( triangle ) * 3 + 2 < _listTriangle.size(); ++triangle )
            {
                const float3& a = _listTriangle[triangle * 3];
                const float3& b = _listTriangle[triangle * 3 + 1];
                const float3& c = _listTriangle[triangle * 3 + 2];
                AABB          bounds;
                bounds._min = float3::min( float3::min( a, b ), c ) - padding;
                bounds._max = float3::max( float3::max( a, b ), c ) + padding;
                (void)_tree.insert( SlotHandle::make( triangle, 1 ), bounds );
            }
            ++_buildCount;
            return true;
        }

        /** @brief `assign` 이 BVH 를 실제로 만든 횟수입니다(캐시 적중 확인용). */
        uint32 getBuildCount() const { return _buildCount; }
        uint32 getTriangleCount() const { return static_cast<uint32>( _listTriangle.size() / 3 ); }

        /**
         * @brief 가장 가까운 교차를 찾습니다(양면). @p inoutListScratch 는 부르는 스레드가 가진 후보 버퍼입니다.
         * @details `SurfaceBVH::findNearestHit` 와 같은 규칙이다. 상태를 바꾸지 않으므로 스크래치만 다르면 여러 스레드가 동시에 물어도 된다.
         */
        [[nodiscard]] bool findNearestHit( const float3& origin, const float3& direction, float32 maxDistance, vector<SlotHandle>& inoutListScratch, GeometryRayHit& outHit ) const
        {
            const float3 unitDirection = CharacterGeometryUtil::makeUnitOr( direction, float3::Zero );
            if ( unitDirection == float3::Zero )
                return false;
            _tree.queryRay( origin, unitDirection, maxDistance, inoutListScratch );
            bool    bFound       = false;
            float32 bestDistance = maxDistance;
            for ( const SlotHandle handle : inoutListScratch )
            {
                float3 a;
                float3 b;
                float3 c;
                getTriangle( handle.index(), a, b, c );
                float32 distance   = 0.0f;
                float32 u          = 0.0f;
                float32 v          = 0.0f;
                bool    bFrontFace = false;
                if ( CharacterGeometryUtil::intersectRayTriangle( origin, unitDirection, a, b, c, distance, u, v, bFrontFace ) == false )
                    continue;
                if ( distance > bestDistance )
                    continue;
                bestDistance       = distance;
                bFound             = true;
                outHit._distance   = distance;
                outHit._baryU      = u;
                outHit._baryV      = v;
                outHit._triangle   = handle.index();
                outHit._normal     = CharacterGeometryUtil::makeUnitOr( ( b - a ).cross( c - a ), float3::UnitY );
                outHit._bFrontFace = bFrontFace ? SW_TRUE : SW_FALSE;
            }
            return bFound;
        }

        /** @brief @p maxDistance 안에서 가장 가까운 표면 점을 찾습니다. 스크래치 규칙은 `findNearestHit` 와 같습니다. */
        [[nodiscard]] bool findClosestPoint( const float3& point, float32 maxDistance, vector<SlotHandle>& inoutListScratch, GeometryClosestPoint& outClosest ) const
        {
            _tree.querySphere( point, maxDistance, inoutListScratch );
            bool    bFound              = false;
            float32 bestDistanceSquared = maxDistance * maxDistance;
            for ( const SlotHandle handle : inoutListScratch )
            {
                float3 a;
                float3 b;
                float3 c;
                getTriangle( handle.index(), a, b, c );
                float32       u               = 0.0f;
                float32       v               = 0.0f;
                const float3  closest         = CharacterGeometryUtil::findClosestPointOnTriangle( point, a, b, c, u, v );
                const float32 distanceSquared = float3::getDistanceSquared( point, closest );
                if ( distanceSquared > bestDistanceSquared )
                    continue;
                bestDistanceSquared  = distanceSquared;
                bFound               = true;
                outClosest._point    = closest;
                outClosest._normal   = CharacterGeometryUtil::makeUnitOr( ( b - a ).cross( c - a ), float3::UnitY );
                outClosest._triangle = handle.index();
                outClosest._baryU    = u;
                outClosest._baryV    = v;
            }
            if ( bFound )
                outClosest._distance = MathUtil::sqrt( bestDistanceSquared );
            return bFound;
        }

    private:
        /** @brief 평평한 삼각형도 부피가 있게 상자를 조금 키운다(광선 슬랩 판정의 0 두께 모서리). `SurfaceBVH` 와 같은 값이다. */
        static constexpr float32 kBoundsPadding = 1.0e-4f;

        void getTriangle( uint32 triangle, float3& outA, float3& outB, float3& outC ) const
        {
            outA = _listTriangle[triangle * 3];
            outB = _listTriangle[triangle * 3 + 1];
            outC = _listTriangle[triangle * 3 + 2];
        }

    private:
        vector<float3> _listTriangle; ///< 마지막으로 넘겨받은 삼각형 목록(비교 · 질의용 사본)
        BVHTree3D      _tree;
        uint32         _buildCount;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 삼각형 목록을 GPU 프로그램에 싣는 평탄한 BVH(노드 표 + 삼각형 표)로 만들고, 같은 목록이면 지난번 결과를 그대로 씁니다.
     * @details 노드 = float4 둘: [0] xyz 상자 최소 · w = 안쪽이면 첫 자식 번호(둘째 자식은 +1), 잎이면 첫 삼각형 번호, [1] xyz 상자 최대 · w = 잎의 삼각형 수(0 이면 안쪽).
     *          번호는 float 로 정확한 정수(2^24 미만)다. 삼각형은 잎 순서로 다시 정렬해 float4 셋씩(a · b · c) 싣는다. 중앙값 분할이라 깊이가 log2(삼각형 수 / 잎 크기)이고
     *          셰이더 스택(32)에 넉넉히 들어간다. 상자는 `ConformSurface` 와 같이 평평한 삼각형이 부피를 갖게 조금 키운다.
     *          별도 GPU 버퍼가 아니라 프로그램 목록(`g_DeformPrograms`, 구조 버퍼) 안의 구간이다 - 모디파이어마다 새 바인딩을 만들지 않으려는 같은 이유다.
     *          `assign` 은 지난번 목록과 한 바이트도 다르지 않을 때만 다시 만들지 않는다(해시가 아니라 값 비교) - 미리보기가 시간마다 프로그램을 다시 만들어도 BVH 는 한 번만 짓는다.
     */
    class ConformGPUTable
    {
    public:
        static constexpr uint32 kLeafSize = 4;

        /** @brief 목록을 넘깁니다. 지난번과 같으면 그대로 두고 false, 다시 지었으면 true 입니다. */
        bool assign( const vector<float3>& listTriangle )
        {
            const size_t byteCount = listTriangle.size() * sizeof( float3 );
            if ( _bBuilt && listTriangle.size() == _listSource.size() && std::memcmp( listTriangle.data(), _listSource.data(), byteCount ) == 0 )
                return false;
            _listSource = listTriangle;
            _bBuilt     = true;
            build();
            return true;
        }

        uint32 getTriangleCount() const { return _triangleCount; }
        uint32 getNodeCount() const { return _nodeCount; }
        /** @brief 노드 표(노드마다 float4 둘) 뒤에 삼각형 표(삼각형마다 float4 셋)를 이은 목록입니다. */
        const vector<float4>& getPacked() const { return _listPacked; }

    private:
        struct Node
        {
            float3 _min{ 0.0f, 0.0f, 0.0f };
            float3 _max{ 0.0f, 0.0f, 0.0f };
            uint32 _first{ 0 }; ///< 안쪽이면 첫 자식 번호, 잎이면 첫 삼각형 번호
            uint32 _count{ 0 }; ///< 잎의 삼각형 수(0 이면 안쪽)
        };

        void build()
        {
            _triangleCount = static_cast<uint32>( _listSource.size() / 3 );
            _listPacked.clear();
            _nodeCount = 0;
            if ( _triangleCount == 0 )
                return;
            const float3   padding( 1.0e-4f );
            vector<float3> listCentroid( _triangleCount );
            vector<uint32> listOrder( _triangleCount );
            for ( uint32 index = 0; index < _triangleCount; ++index )
            {
                listOrder[index]    = index;
                listCentroid[index] = ( _listSource[index * 3] + _listSource[index * 3 + 1] + _listSource[index * 3 + 2] ) * ( 1.0f / 3.0f );
            }
            vector<Node> listNode;
            listNode.reserve( static_cast<size_t>( _triangleCount ) / 2 + 2 );
            listNode.emplace_back();
            buildNode( listNode, 0, 0, _triangleCount, listOrder, listCentroid, padding );
            _nodeCount = static_cast<uint32>( listNode.size() );
            _listPacked.reserve( static_cast<size_t>( _nodeCount ) * 2 + static_cast<size_t>( _triangleCount ) * 3 );
            for ( const Node& node : listNode )
            {
                _listPacked.push_back( float4{ node._min._x, node._min._y, node._min._z, static_cast<float32>( node._first ) } );
                _listPacked.push_back( float4{ node._max._x, node._max._y, node._max._z, static_cast<float32>( node._count ) } );
            }
            for ( const uint32 triangle : listOrder )
            {
                for ( uint32 corner = 0; corner < 3; ++corner )
                {
                    const float3& position = _listSource[triangle * 3 + corner];
                    _listPacked.push_back( float4{ position._x, position._y, position._z, 0.0f } );
                }
            }
        }

        /** @brief 순서 표의 [first, first + count) 를 노드 @p nodeIndex 로 만듭니다(재귀 깊이 = 트리 깊이). */
        void buildNode( vector<Node>& inoutListNode, uint32 nodeIndex, uint32 first, uint32 count, vector<uint32>& inoutListOrder, const vector<float3>& listCentroid,
                        const float3& padding )
        {
            float3 boundsMin   = _listSource[inoutListOrder[first] * 3];
            float3 boundsMax   = boundsMin;
            float3 centroidMin = listCentroid[inoutListOrder[first]];
            float3 centroidMax = centroidMin;
            for ( uint32 index = first; index < first + count; ++index )
            {
                const uint32 triangle = inoutListOrder[index];
                for ( uint32 corner = 0; corner < 3; ++corner )
                {
                    boundsMin = float3::min( boundsMin, _listSource[triangle * 3 + corner] );
                    boundsMax = float3::max( boundsMax, _listSource[triangle * 3 + corner] );
                }
                centroidMin = float3::min( centroidMin, listCentroid[triangle] );
                centroidMax = float3::max( centroidMax, listCentroid[triangle] );
            }
            Node& node = inoutListNode[nodeIndex];
            node._min  = boundsMin - padding;
            node._max  = boundsMax + padding;
            if ( count <= kLeafSize )
            {
                node._first = first;
                node._count = count;
                return;
            }
            // 무게중심 상자의 가장 긴 축에서 중앙값으로 가른다
            const float3 extent = centroidMax - centroidMin;
            uint32       axis   = 0;
            if ( extent._y > extent._x && extent._y >= extent._z )
                axis = 1;
            else if ( extent._z > extent._x && extent._z > extent._y )
                axis = 2;
            const uint32 middle = first + count / 2;
            std::nth_element( inoutListOrder.begin() + first, inoutListOrder.begin() + middle, inoutListOrder.begin() + first + count,
                              [&listCentroid, axis]( uint32 left, uint32 right )
            {
                const float3& a = listCentroid[left];
                const float3& b = listCentroid[right];
                return ( axis == 0 ? a._x < b._x : ( axis == 1 ? a._y < b._y : a._z < b._z ) );
            } );
            const uint32 childBase = static_cast<uint32>( inoutListNode.size() );
            inoutListNode.emplace_back();
            inoutListNode.emplace_back();
            inoutListNode[nodeIndex]._first = childBase; // 재할당으로 node 참조가 낡았을 수 있어 다시 쓴다
            inoutListNode[nodeIndex]._count = 0;
            buildNode( inoutListNode, childBase, first, middle - first, inoutListOrder, listCentroid, padding );
            buildNode( inoutListNode, childBase + 1, middle, first + count - middle, inoutListOrder, listCentroid, padding );
        }

        vector<float3> _listSource;
        vector<float4> _listPacked;
        uint32         _triangleCount{ 0 };
        uint32         _nodeCount{ 0 };
        bool           _bBuilt{ false };
    };
} // namespace sw::editor
