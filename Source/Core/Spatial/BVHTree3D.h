#pragma once
#include "Core/Container/SlotHandle.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"

namespace sw
{
    /**
     * @brief 3D BVH 트리의 내부 · 잎 노드입니다.
     */
    struct BVHNode3D
    {
        AABB       _bounds{};
        SlotHandle _handle{};
        int32      _parent{ invalid_index::kInt32 };
        int32      _leftChild{ invalid_index::kInt32 };
        int32      _rightChild{ invalid_index::kInt32 };
        int32      _height{ 0 };

        bool isLeaf() const { return _leftChild == invalid_index::kInt32; }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 3D 씬의 공간 질의를 빠르게 하는 동적 BVH(Bounding Volume Hierarchy) 트리입니다.
     */
    class SW_API BVHTree3D
    {
    public:
        BVHTree3D();
        ~BVHTree3D()                                 = default;
        BVHTree3D( const BVHTree3D& )                = default;
        BVHTree3D& operator=( const BVHTree3D& )     = default;
        BVHTree3D( BVHTree3D&& ) noexcept            = default;
        BVHTree3D& operator=( BVHTree3D&& ) noexcept = default;

        int32 insert( SlotHandle handle, const AABB& bounds );
        void  update( SlotHandle handle, const AABB& bounds );
        void  remove( SlotHandle handle );
        void  clear();

        /**
         * @brief 상자에 겹치는 핸들을 찾습니다.
         * @param outListHandle 결과입니다. **부르기 전 내용은 지워집니다**(`Spatial/README.md` 의 공통 규약).
         */
        void queryAABB( const AABB& queryBox, vector<SlotHandle>& outListHandle ) const;
        /**
         * @brief 광선에 걸리는 핸들을 찾습니다. `outListHandle` 의 기존 내용은 지워집니다.
         * @param direction 방향입니다. 단위 길이가 아니어도 됩니다. 안에서 맞춥니다.
         * @param maxDist 월드 단위 사거리입니다(방향 벡터의 배수가 아닙니다).
         */
        void queryRay( const float3& origin, const float3& direction, float32 maxDist, vector<SlotHandle>& outListHandle ) const;
        /** @brief 구에 겹치는 핸들을 찾습니다. `outListHandle` 의 기존 내용은 지워집니다. */
        void querySphere( const float3& center, float32 radius, vector<SlotHandle>& outListHandle ) const;
        /** @brief 절두체 안의 핸들을 찾습니다. `outListHandle` 의 기존 내용은 지워집니다. */
        void queryFrustum( const float4x4& viewProj, vector<SlotHandle>& outListHandle ) const;

        size_t getHandleCount() const;
        size_t getNodeCount() const;
        int32  getTreeHeight() const;

    private:
        int32 allocateNode();
        void  freeNode( int32 nodeIndex );
        void  insertLeaf( int32 leafIndex );
        void  removeLeaf( int32 leafIndex );
        int32 balance( int32 nodeIndex );
        /** @brief 새 잎의 형제로 가장 좋은 노드(표면적 휴리스틱)를 찾습니다. 트리가 비어 있으면 안 됩니다. */
        int32 findBestSibling( const AABB& leafAABB ) const;
        /** @brief 노드 @p childIndex 로 내려갈 때 잎이 더하는 비용(표면적 증가분 + 상속 비용)입니다. */
        float32 computeDescentCost( int32 childIndex, const AABB& leafAABB, float32 inheritanceCost ) const;
        /** @brief @p startIndex 부터 뿌리까지 올라가며 균형을 잡고 경계와 높이를 다시 맞춥니다. */
        void refitUpward( int32 startIndex );
        /** @brief 노드 @p nodeIndex 의 자식 @p upIndex 를 위로 회전합니다(@p stayIndex 는 남는 다른 자식, @p bUpIsRight 는 올린 쪽이 오른쪽 자식인지). */
        void rotateUp( int32 nodeIndex, int32 upIndex, int32 stayIndex, bool bUpIsRight );

        static AABB    combineAABB( const AABB& a, const AABB& b );
        static float32 getSurfaceArea( const AABB& box );

        vector<BVHNode3D>                _listNode;
        vector<uint32>                   _listFreeNode;
        unordered_map<SlotHandle, int32> _mapHandleToNode;
        int32                            _rootIndex;
    };
} // namespace sw
