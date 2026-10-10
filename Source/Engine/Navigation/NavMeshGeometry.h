/**
 * @file NavMeshGeometry.h
 * @brief 내비메시를 베이크하는 입력 — 월드 공간 삼각형(영역 번호 하나씩)과 영역을 덮어쓰는 볼록 부피(장애물 · 영역 표시)입니다.
 * @details 씬에서 모으는 일(메시 · 물리 셰이프 · 파괴 조각)은 위층(`SceneNavigation`)이 하고, 여기는 값만 듭니다. 베이크하는 백엔드는 이것만 읽으므로
 *          워커가 읽는 동안 바꾸지 않습니다 — 바꿀 일이 있으면 사본을 만들어 바꾸고 바꿔 끼웁니다(`shared_ptr<const NavMeshGeometry>`).
 *          타일 하나에 닿는 삼각형은 XZ 칸 색인(`rebuildSpatialIndex`)으로 찾습니다 — 타일마다 전체를 훑지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Navigation/NavigationTypes.h"
#include "Engine/Physics/Collision/AABB.h"

namespace sw
{
    struct PhysicsShapeDesc3D;

    /**
     * @brief 영역을 덮어쓰는 볼록 부피입니다 — XZ 다각형(볼록, 어느 감는 방향이든) × [최소 Y, 최대 Y].
     * @details 영역이 `NavigationConstant::kNotWalkableArea` 이면 그 자리를 뚫습니다(장애물 · 유니티 Carve). 아니면 그 영역으로 칠합니다(늪 · 길).
     */
    struct NavConvexVolume
    {
        vector<float3> _listPoint; ///< Y 는 쓰지 않는다
        float32        _minY{ 0.0f };
        float32        _maxY{ 0.0f };
        uint8          _area{ NavigationConstant::kNotWalkableArea };

        /** @brief XZ 경계 상자(Y 는 부피의 높이)입니다. */
        AABB computeBounds() const;
    };
} // namespace sw

namespace sw
{
    /** @class NavMeshGeometry @brief 베이크 입력 하나입니다. 파일 머리말 참고. */
    class SW_API NavMeshGeometry
    {
    public:
        NavMeshGeometry();

        /** @brief 모두 비웁니다. */
        void clear();

        /**
         * @brief 삼각형을 더합니다. 점은 @p world(행 벡터, 로컬 × 월드)로 옮기고, 번호는 이 목록의 점 번호입니다.
         * @param area 영역 번호(표의 순서) 또는 `kNotWalkableArea`(그 삼각형 위는 걷지 않지만 막기는 한다 — 지붕 · 벽)
         */
        void addTriangles( const float3* pPoint, uint32 pointCount, const uint32* pIndex, uint32 indexCount, const float4x4& world, uint8 area );
        /** @brief 번호 없는 삼각형 목록(점 셋이 한 삼각형)을 더합니다. 정점 위치만 읽고 @p stride 바이트씩 건넙니다(메시 정점 구조체를 그대로). */
        void addTriangleList( const void* pFirstPosition, uint32 vertexCount, uint32 stride, const float4x4& world, uint8 area );
        /** @brief 축 상자(로컬 반 크기)를 @p world 로 옮겨 열두 삼각형으로 더합니다. */
        void addBox( const float3& halfExtents, const float4x4& world, uint8 area );
        /**
         * @brief 물리 셰이프 하나를 더합니다. 상자 · 삼각 메시는 그대로, 구 · 캡슐 · 볼록 껍질은 로컬 경계 상자로 근사합니다(복셀로 베이크하므로 발자국만 맞으면 된다).
         * @details 셰이프의 로컬 자세(`_localPosition` · `_localRotation`)를 @p bodyWorld 앞에 겁니다.
         */
        void addPhysicsShape( const PhysicsShapeDesc3D& shape, const float4x4& bodyWorld, uint8 area );
        /** @brief 볼록 부피를 더합니다. */
        void addConvexVolume( const NavConvexVolume& volume );

        /**
         * @brief 삼각형을 XZ 칸(@p cellSize 미터)에 나눠 둡니다. 다시 부르면 다시 짓습니다. 베이크 전에 한 번 부릅니다.
         * @details 칸 수는 상한(백만)을 넘지 않게 칸 크기를 키웁니다.
         */
        void rebuildSpatialIndex( float32 cellSize );
        /**
         * @brief XZ 상자 [@p min, @p max] 와 칸이 겹치는 삼각형 번호를 @p outListTriangle 에 채웁니다(겹친 칸의 삼각형을 한 번씩 — 실제 겹침은 베이크하는 쪽이 다시 본다).
         * @details 색인이 없으면 모든 삼각형입니다. 여러 스레드가 같이 불러도 됩니다(읽기만 한다).
         */
        void collectTriangles( const float2& min, const float2& max, vector<uint32>& outListTriangle ) const;

        /** @brief 입력 전체의 해시입니다(점 · 번호 · 영역 · 부피). 쿠킹본이 낡았는지 봅니다. */
        uint64 computeHash() const;

        const vector<float3>&          getVertices() const { return _listVertex; }
        const vector<uint32>&          getIndices() const { return _listIndex; }
        const vector<uint8>&           getTriangleAreas() const { return _listTriangleArea; }
        const vector<NavConvexVolume>& getConvexVolumes() const { return _listVolume; }
        uint32                         getTriangleCount() const { return static_cast<uint32>( _listTriangleArea.size() ); }
        /** @brief 모든 삼각형 · 부피를 덮는 상자입니다. 비었으면 `AABB::empty()` 입니다. */
        const AABB& getBounds() const { return _bounds; }
        bool        isEmpty() const { return _listTriangleArea.empty(); }

    private:
        void expandBounds( const float3& point );

        vector<float3>          _listVertex;
        vector<uint32>          _listIndex;        ///< 삼각형마다 셋
        vector<uint8>           _listTriangleArea; ///< 삼각형마다 하나
        vector<NavConvexVolume> _listVolume;
        vector<uint32>          _listCellStart; ///< 칸마다 `_listCellTriangle` 의 시작(칸 수 + 1)
        vector<uint32>          _listCellTriangle;
        AABB                    _bounds;
        float2                  _indexOrigin;
        float32                 _indexCellSize;
        int32                   _indexWidth;
        int32                   _indexHeight;
    };
} // namespace sw
