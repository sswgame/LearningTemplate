/**
 * @file INavMeshTileStore.h
 * @brief 내비메시의 타일 쪽 인터페이스입니다. 격자를 읽고, 타일을 베이크하고, 바꿔 끼우고, 베껴 냅니다.
 * @details 쿠킹 · 재베이크처럼 타일만 다루는 코드는 이 인터페이스만 받는다(`NavMeshAsset` · `NavTileBakeJob`). 언리얼 `FRecastNavMeshGenerator` 가 보는 타일 쪽 자리다.
 *          타일 하나는 백엔드가 아는 불투명 바이트(`NavTileData`)이고, 스레드 규칙은 `INavMesh.h` 머리말과 같다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"
#include "Core/Math/Math.h"

namespace sw
{
    struct NavAgentTypeDef;
    struct NavConvexVolume;

    class NavMeshGeometry;

    /** @brief 타일 하나의 베이크 결과입니다. 격자 위치와 백엔드의 불투명 바이트를 담습니다(바이트가 비면 그 타일에 걸을 곳이 없습니다). */
    struct NavTileData
    {
        vector<uint8> _bytes;
        int32         _tileX{ 0 };
        int32         _tileZ{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 타일 격자의 형태입니다. 타일 (x, z) 는 XZ [원점 + (x, z) × 타일 크기, 원점 + (x + 1, z + 1) × 타일 크기) 를 덮습니다. */
    struct NavTileGrid
    {
        float3  _origin{};
        float32 _tileWorldSize{ 0.0f };
        int32   _tileCountX{ 0 };
        int32   _tileCountZ{ 0 };

        int32 getTileCount() const { return _tileCountX * _tileCountZ; }
        /** @brief 타일 하나의 XZ 상자를 반환합니다(Y 는 @p minY 와 @p maxY 입니다). */
        AABB computeTileBounds( int32 tileX, int32 tileZ, float32 minY, float32 maxY ) const
        {
            const float32 minX = _origin._x + static_cast<float32>( tileX ) * _tileWorldSize;
            const float32 minZ = _origin._z + static_cast<float32>( tileZ ) * _tileWorldSize;
            return AABB{
                float3{                 minX, minY,                  minZ},
                float3{minX + _tileWorldSize, maxY, minZ + _tileWorldSize}
            };
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 타일 격자 · 베이크 · 교체입니다. */
    class INavMeshTileStore
    {
    public:
        virtual ~INavMeshTileStore() = default;

        /** @brief 타일 격자입니다(`initialize` 전에는 빈 격자). */
        virtual const NavTileGrid& getTileGrid() const = 0;
        /** @brief 베이크하는 에이전트 종류입니다. */
        virtual const NavAgentTypeDef& getAgentType() const = 0;
        /** @brief XZ 상자에 닿는 타일 위치를 @p outListTile 에 채웁니다(바깥 기하가 그 타일 가장자리를 바꾸므로 베이크 테두리만큼 넓힙니다). */
        virtual void collectTilesOverlapping( const AABB& bounds, vector<int2>& outListTile ) const = 0;

        /**
         * @brief 타일 하나를 베이크합니다. 입력만 읽고 내비메시를 바꾸지 않으므로 워커 여럿이 같이 돌 수 있습니다.
         * @details @p geometry 는 색인(`rebuildSpatialIndex`)을 만들어 둔 것이어야 빠르다. @p pExtraVolume(없어도 된다)은 입력의 부피 뒤에 칠하는
         *          부피다. 움직이는 장애물을 정적 입력과 따로 들고 다닐 때 쓴다. 걸을 곳이 없으면 바이트가 빈 결과로 true 를 반환한다.
         * @return 베이크하다가 실패(메모리나 정점 상한)하면 false 를 반환합니다.
         */
        [[nodiscard]] virtual bool bakeTile( const NavMeshGeometry& geometry, const vector<NavConvexVolume>* pExtraVolume, int32 tileX, int32 tileZ,
                                             NavTileData& outTile ) const = 0;
        /** @brief 타일을 바꿔 끼웁니다(바이트가 비면 그 타일을 뺀다). 게임 스레드에서, 질의가 없는 동안만 부릅니다. 옛 폴리곤 참조는 무효가 됩니다. */
        [[nodiscard]] virtual bool replaceTile( const NavTileData& tile ) = 0;
        /** @brief 든 타일을 모두 베껴 냅니다(쿠킹). */
        virtual void collectTiles( vector<NavTileData>& outListTile ) const = 0;
        /** @brief 든 타일 수입니다. */
        virtual uint32 getLoadedTileCount() const = 0;
        /** @brief 걸을 폴리곤 수입니다(진단). */
        virtual uint32 getPolygonCount() const = 0;
        /** @brief 바뀔 때마다(타일 끼우기, 링크 막기) 오르는 번호입니다. 경로 캐시가 낡았는지 봅니다. */
        virtual uint32 getRevision() const = 0;
    };
} // namespace sw
