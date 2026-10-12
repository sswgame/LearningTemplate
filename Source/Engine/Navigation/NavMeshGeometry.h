/**
 * @file NavMeshGeometry.h
 * @brief 내비메시를 베이크하는 입력입니다. 월드 공간 삼각형(영역 번호 하나씩)과, 영역을 덮어쓰는 볼록 부피(장애물, 영역 표시)를 담습니다.
 * @details 씬에서 모으는 일(메시, 물리 셰이프, 파괴 조각)은 상위 계층(`SceneNavigation`)이 하고, 여기는 값만 가진다. 베이크하는 백엔드는 이것만 읽으므로
 *          워커가 읽는 동안 바꾸지 않는다. 바꿀 일이 있으면 사본을 만들어 바꾸고 교체한다(`shared_ptr<const NavMeshGeometry>`).
 *          타일 하나에 닿는 삼각형은 XZ 셀 색인(`rebuildSpatialIndex`)으로 찾는다. 타일마다 전체를 훑지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/AABB.h"
#include "Core/Math/Math.h"

#include "Engine/Navigation/NavigationTypes.h"

namespace sw
{
    struct PhysicsShapeDesc3D;

    /**
     * @brief 영역을 덮어쓰는 볼록 부피입니다. XZ 다각형(볼록이며 감는 방향은 상관없음) × [최소 Y, 최대 Y] 입니다.
     * @details 영역이 `NavigationConstant::kNotWalkableArea` 이면 그 위치를 뚫는다(장애물, 유니티의 Carve). 아니면 그 영역으로 칠한다(늪, 길).
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
    /**
     * @brief 오프 메시 링크입니다. 걸을 면으로 연결되지 않은 두 점(사다리 · 점프 · 문)을 연결합니다. 언리얼 NavLink Proxy(`FNavigationLink`) · 유니티 `NavMeshLink` 에 해당합니다.
     * @details 링크는 시작점이 포함된 타일에 함께 베이크된다(Detour 규칙). 두 끝은 걸을 면에서 `_radius` 안에 있어야 연결되고, 아니면 그 끝은 버려진다.
     *          `_userID` 는 링크를 가리키는 번호다. 경로(`NavPath::_listLinkCrossing`)와 군중 상태(`NavCrowdAgentState::_link`)가 이 번호로 알리고,
     *          `INavMesh::setOffMeshLinkEnabled` 가 이 번호로 막는다(닫힌 문). 0 은 번호 없는 링크다.
     */
    struct NavOffMeshLink
    {
        float3  _start{};
        float3  _end{};
        float32 _radius{ 0.5f }; ///< 끝을 걸을 면에 붙이는 거리(미터)
        uint32  _userID{ 0 };
        uint8   _area{ 0 };                 ///< 영역 번호(테이블 순서). 경로 비용과 질의 필터의 영역 비트가 따른다
        uint8   _bBidirectional{ SW_TRUE }; ///< 끝에서 시작으로도 건넌다

        /** @brief 두 끝을 반지름만큼 넓힌 상자입니다(바뀌면 이 상자의 타일을 다시 베이크한다). */
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
         * @brief 삼각형을 추가합니다. 점은 @p world(행 벡터, 로컬 × 월드)로 옮기고, 번호는 이 목록의 점 번호입니다.
         * @param area 영역 번호(테이블 순서) 또는 `kNotWalkableArea` 입니다(그 삼각형 위는 걷지 않지만 막기는 합니다. 지붕과 벽이 그렇습니다).
         */
        void addTriangles( const float3* pPoint, uint32 pointCount, const uint32* pIndex, uint32 indexCount, const float4x4& world, uint8 area );
        /** @brief 번호 없는 삼각형 목록(점 셋이 한 삼각형)을 더합니다. 정점 위치만 읽고 @p stride 바이트씩 건넙니다(메시 정점 구조체를 그대로). */
        void addTriangleList( const void* pFirstPosition, uint32 vertexCount, uint32 stride, const float4x4& world, uint8 area );
        /** @brief 축 상자(로컬 반 크기)를 @p world 로 옮겨 열두 삼각형으로 더합니다. */
        void addBox( const float3& halfExtents, const float4x4& world, uint8 area );
        /**
         * @brief 물리 셰이프 하나를 추가합니다. 상자와 삼각 메시는 그대로 쓰고, 구, 캡슐, 볼록 껍질은 로컬 경계 상자로 근사합니다(복셀로 베이크하므로 발자국만 맞으면 됩니다).
         * @details 셰이프의 로컬 자세(`_localPosition`, `_localRotation`)를 @p bodyWorld 앞에 곱한다.
         */
        void addPhysicsShape( const PhysicsShapeDesc3D& shape, const float4x4& bodyWorld, uint8 area );
        /**
         * @brief 높이장(지형)을 삼각형으로 더합니다. 샘플은 @p resolution × @p resolution(행 우선, 행이 z)이고 값은 월드 높이입니다.
         * @details 샘플 (x, z) 는 XZ ( @p origin.x + x × @p cellSize.x, @p origin.z + z × @p cellSize.y ) 다. 셀 하나는 대각선 (x, z)-(x+1, z+1) 로 나눈
         *          삼각형 둘이다(지형 렌더 메시와 같은 나눔). @p pHoleCell(없어도 된다)은 (N-1)² 셀 마스크이고 0 이 아닌 셀은 넣지 않는다(구멍).
         */
        void addHeightfield( const float32* pHeight, uint32 resolution, const float3& origin, const float2& cellSize, const uint8* pHoleCell, uint8 area );
        /** @brief 볼록 부피를 더합니다. */
        void addConvexVolume( const NavConvexVolume& volume );
        /** @brief 오프 메시 링크를 더합니다. */
        void addOffMeshLink( const NavOffMeshLink& link );

        /**
         * @brief 삼각형을 XZ 셀(@p cellSize 미터)에 나눠 둡니다. 다시 호출하면 다시 만듭니다. 베이크 전에 한 번 호출합니다.
         * @details 셀 수가 상한(백만)을 넘지 않게 셀 크기를 키운다.
         */
        void rebuildSpatialIndex( float32 cellSize );
        /**
         * @brief XZ 상자 [@p min, @p max] 와 셀이 겹치는 삼각형 번호를 @p outListTriangle 에 채웁니다(겹친 셀의 삼각형을 한 번씩 담으며, 실제 겹침은 베이크하는 쪽이 다시 확인합니다).
         * @details 색인이 없으면 모든 삼각형을 담는다. 여러 스레드가 같이 호출해도 된다(읽기만 한다).
         */
        void collectTriangles( const float2& min, const float2& max, vector<uint32>& outListTriangle ) const;

        /** @brief 입력 전체의 해시를 반환합니다(점, 번호, 영역, 부피, 링크). 쿠킹본이 낡았는지 판정할 때 씁니다. */
        uint64 computeHash() const;

        const vector<float3>&          getVertices() const { return _listVertex; }
        const vector<uint32>&          getIndices() const { return _listIndex; }
        const vector<uint8>&           getTriangleAreas() const { return _listTriangleArea; }
        const vector<NavConvexVolume>& getConvexVolumes() const { return _listVolume; }
        const vector<NavOffMeshLink>&  getOffMeshLinks() const { return _listLink; }
        uint32                         getTriangleCount() const { return static_cast<uint32>( _listTriangleArea.size() ); }
        /** @brief 모든 삼각형과 부피를 덮는 상자를 반환합니다. 비었으면 `AABB::empty()` 입니다. */
        const AABB& getBounds() const { return _bounds; }
        bool        isEmpty() const { return _listTriangleArea.empty(); }

    private:
        void expandBounds( const float3& point );

        vector<float3>          _listVertex;
        vector<uint32>          _listIndex;        ///< 삼각형마다 점 셋을 가진다.
        vector<uint8>           _listTriangleArea; ///< 삼각형마다 하나를 가진다.
        vector<NavConvexVolume> _listVolume;
        vector<NavOffMeshLink>  _listLink;
        vector<uint32>          _listCellStart; ///< 셀마다 `_listCellTriangle` 의 시작 위치다(셀 수 + 1).
        vector<uint32>          _listCellTriangle;
        AABB                    _bounds;
        float2                  _indexOrigin;
        float32                 _indexCellSize;
        int32                   _indexWidth;
        int32                   _indexHeight;
    };
} // namespace sw
