#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Physics/AABB.h"

namespace sw
{
    /**
     * @brief 3D BVH 트리의 내부/리프 노드
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

    /**
     * @brief 3D 씬 가속을 위한 고성능 동적 Bounding Volume Hierarchy (BVH) 트리
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
         * @param outListHandle 결과입니다 — **호출 전 내용은 지워집니다**(`Spatial/README.md` 의 공통 규약).
         */
        void queryAabb( const AABB& queryBox, vector<SlotHandle>& outListHandle ) const;
        /**
         * @brief 광선에 걸리는 핸들을 찾습니다. `outListHandle` 의 기존 내용은 지워집니다.
         * @param direction 방향입니다. 단위 길이가 아니어도 됩니다 — 안에서 맞춥니다.
         * @param maxDist 월드 단위 사거리입니다(방향 벡터의 배수가 아닙니다).
         */
        void queryRay( const float3& origin, const float3& direction, float32 maxDist, vector<SlotHandle>& outListHandle ) const;
        /** @brief 구체에 겹치는 핸들을 찾습니다. `outListHandle` 의 기존 내용은 지워집니다. */
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

        static AABB    combineAabb( const AABB& a, const AABB& b );
        static float32 getSurfaceArea( const AABB& box );

        vector<BVHNode3D>                _listNode;
        vector<int32>                    _listFreeNode;
        unordered_map<SlotHandle, int32> _mapHandleToNode;
        int32                            _rootIndex;
    };
} // namespace sw
