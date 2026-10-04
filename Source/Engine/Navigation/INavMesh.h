/**
 * @file INavMesh.h
 * @brief 엔진 쪽 내비메시 인터페이스 — 타일 베이크 · 바꿔 끼우기 · 경로 · 가장 가까운 점 · 레이캐스트 · 디버그 선 · 군중을 만드는 창구입니다.
 * @details 라이브러리(Recast · Detour)는 `Navigation/Recast/` 안에만 있고 여기에는 그 타입이 하나도 없습니다(`CheckThirdPartyIsolation.py`).
 *          백엔드를 바꾸면 이 인터페이스를 구현하는 두 클래스(내비메시 · 군중)와 `NavMeshBackend::createNavMesh` 의 한 줄이 바뀝니다.
 *
 *          **타일.** 내비메시는 XZ 타일 격자입니다. 타일 하나는 백엔드가 아는 불투명 바이트(`NavTileData`)이고, 베이크(`bakeTile`)는 입력만 읽는
 *          순수 함수라 워커에서 여럿이 같이 돕니다. 끼우기(`replaceTile`)는 게임 스레드에서, 질의가 없는 동안 합니다. 쿠킹본(`.navmesh`)은 이 바이트를
 *          그대로 싣습니다(`NavMeshAsset`).
 *
 *          **질의.** 질의는 const 이고 여러 스레드가 같이 불러도 됩니다 — 스레드마다 자기 질의 객체(스크래치 슬롯)를 씁니다. 다만 끼우기와
 *          겹치면 안 됩니다: 끼우기는 씬 틱 밖(컴포넌트 틱이 끝난 뒤)에서만 합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Navigation/NavigationTypes.h"
#include "Engine/Physics/AABB.h"

namespace sw
{
    struct NavAgentTypeDef;
    struct NavConvexVolume;
    struct NavMeshSettings;

    class IPhysicsDebugRenderer;
    class NavMeshGeometry;

    /** @brief 타일 하나의 베이크한 결과입니다 — 격자 자리와 백엔드의 불투명 바이트(비면 그 타일에 걸을 곳이 없다). */
    struct NavTileData
    {
        vector<uint8> _bytes;
        int32         _tileX{ 0 };
        int32         _tileZ{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 타일 격자의 모양입니다. 타일 (x, z) 는 XZ [원점 + (x, z) × 타일 크기, 원점 + (x + 1, z + 1) × 타일 크기) 를 덮습니다. */
    struct NavTileGrid
    {
        float3  _origin{};
        float32 _tileWorldSize{ 0.0f };
        int32   _tileCountX{ 0 };
        int32   _tileCountZ{ 0 };

        int32 getTileCount() const { return _tileCountX * _tileCountZ; }
        /** @brief 타일 하나의 XZ 상자입니다(Y 는 @p minY · @p maxY). */
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
    /**
     * @class INavCrowd
     * @brief 내비메시 하나 위의 군중(경로 따라가기 + 이웃 회피 + 떨어지기)입니다 — 언리얼 `UCrowdManager` · DetourCrowd 의 자리.
     * @details 한 스레드(게임 스레드)가 다룹니다. `update` 가 모든 에이전트의 경로 · 회피 · 이동을 한 번에 진행합니다. 에이전트의 자리를 다른 것이
     *          움직이면(캐릭터 컨트롤러) 갱신 전에 `syncAgentPosition` 으로 알려 군중이 그 자리에서 이어 가게 합니다.
     */
    class INavCrowd
    {
    public:
        virtual ~INavCrowd() = default;

        /** @brief 에이전트를 더합니다. 자리는 가장 가까운 내비메시 위로 붙습니다. 자리가 다 찼으면 `kInvalidAgentId` 입니다. */
        virtual NavCrowdAgentId addAgent( const float3& position, const NavCrowdAgentParams& params ) = 0;
        /** @brief 에이전트를 뺍니다. */
        virtual void removeAgent( NavCrowdAgentId agentId ) = 0;
        /** @brief 몸 · 움직임 값을 바꿉니다. */
        virtual void updateAgentParams( NavCrowdAgentId agentId, const NavCrowdAgentParams& params ) = 0;
        /** @brief 목적지를 겁니다. 목적지를 내비메시에서 찾지 못하면 false 이고 상태가 `Failed` 입니다. */
        virtual bool requestMoveTarget( NavCrowdAgentId agentId, const float3& target ) = 0;
        /** @brief 목적지를 지우고 멈춥니다. */
        virtual void resetMoveTarget( NavCrowdAgentId agentId ) = 0;
        /** @brief 순간이동 — 경로를 버리고 새 자리에서 다시 시작합니다(목적지는 다시 건다). */
        virtual void teleportAgent( NavCrowdAgentId agentId, const float3& position ) = 0;
        /** @brief 바깥이 옮긴 자리를 알립니다(경로는 지킨다). 다음 `update` 가 그 자리에서 이어 갑니다. */
        virtual void syncAgentPosition( NavCrowdAgentId agentId, const float3& position ) = 0;
        /** @brief 군중을 @p deltaTime 만큼 진행합니다. */
        virtual void update( float32 deltaTime ) = 0;
        /** @brief 에이전트의 상태입니다. 없는 번호면 false 입니다. */
        [[nodiscard]] virtual bool findAgentState( NavCrowdAgentId agentId, NavCrowdAgentState& outState ) const = 0;
        /** @brief 군중의 질의 거름(영역 비용 · 막을 영역)을 바꿉니다. 모든 에이전트가 같이 씁니다. */
        virtual void setQueryFilter( const NavQueryFilter& filter ) = 0;
        /** @brief 쓰는 중인 에이전트 수입니다. */
        virtual uint32 getActiveAgentCount() const = 0;
        /** @brief 담을 수 있는 에이전트 수입니다. */
        virtual uint32 getMaxAgentCount() const = 0;
        /** @brief 에이전트의 경로(모퉁이) · 속도를 선으로 냅니다. */
        virtual void drawDebug( IPhysicsDebugRenderer& renderer, bool bPath, bool bVelocity ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @class INavMesh @brief 에이전트 종류 하나의 내비메시입니다. 파일 머리말 참고. */
    class INavMesh
    {
    public:
        virtual ~INavMesh() = default;

        /**
         * @brief 빈 타일 격자를 @p bounds 위에 깝니다(타일은 아직 없다). 다시 부르면 모든 타일을 버리고 다시 깝니다.
         * @param areaTable 영역 이름 표(번호만 쓴다)
         */
        [[nodiscard]] virtual bool initialize( const NavAgentTypeDef& agentType, const NavMeshSettings& areaTable, const AABB& bounds ) = 0;
        /** @brief 모든 타일과 질의 객체를 놓습니다. */
        virtual void shutdown() = 0;

        /** @brief 타일 격자입니다(`initialize` 전에는 빈 격자). */
        virtual const NavTileGrid& getTileGrid() const = 0;
        /** @brief 베이크하는 에이전트 종류입니다. */
        virtual const NavAgentTypeDef& getAgentType() const = 0;
        /** @brief XZ 상자에 닿는 타일 자리를 @p outListTile 에 채웁니다(베이크 테두리만큼 넓혀 — 바깥 기하가 그 타일 가장자리를 바꾸므로). */
        virtual void collectTilesOverlapping( const AABB& bounds, vector<int2>& outListTile ) const = 0;

        /**
         * @brief 타일 하나를 베이크합니다. 입력만 읽고 내비메시를 바꾸지 않으므로 워커에서 여럿이 같이 돌 수 있습니다.
         * @details @p geometry 는 색인(`buildSpatialIndex`)을 지어 둔 것이어야 빠릅니다. @p pExtraVolume(없어도 된다)은 입력의 부피 뒤에 칠하는
         *          부피입니다 — 움직이는 장애물을 정적 입력과 따로 들고 다닌다. 걸을 곳이 없으면 바이트가 빈 결과로 true 입니다.
         * @return 베이크하다가 실패(메모리 · 정점 상한)하면 false 입니다.
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
        /** @brief 바뀔 때마다(타일 끼우기) 오르는 번호입니다. 경로 캐시가 낡았는지 봅니다. */
        virtual uint32 getRevision() const = 0;

        /** @brief @p position 에서 반 크기 @p searchExtent 상자 안의 가장 가까운 내비메시 위 점입니다. 없으면 false 입니다. */
        [[nodiscard]] virtual bool findNearestPoint( const float3& position, const float3& searchExtent, const NavQueryFilter& filter, NavLocation& outLocation ) const = 0;
        /**
         * @brief @p start 에서 @p end 까지의 경로를 구해 다듬습니다(줄 당기기 — 모퉁이만).
         * @details 두 점은 반 크기 @p searchExtent 안의 가장 가까운 폴리곤으로 붙습니다. 끝에 닿지 못하면 가장 가까운 곳까지의 `Partial` 입니다.
         */
        virtual NavPathStatus findPath( const float3& start, const float3& end, const float3& searchExtent, const NavQueryFilter& filter, NavPath& outPath ) const = 0;
        /** @brief @p start 에서 @p end 쪽으로 걸을 면을 따라 곧게 갑니다(시선). 경계에 막히면 `_bHit` 입니다. 시작이 내비메시 밖이면 false 입니다. */
        [[nodiscard]] virtual bool raycast( const float3& start, const float3& end, const float3& searchExtent, const NavQueryFilter& filter, NavRaycastHit& outHit ) const = 0;

        /** @brief 이 내비메시 위의 군중을 만듭니다. */
        virtual unique_ptr<INavCrowd> createCrowd( uint32 maxAgentCount, float32 maxAgentRadius ) = 0;
        /** @brief 폴리곤 테두리를 선으로 냅니다. */
        virtual void drawDebug( IPhysicsDebugRenderer& renderer, const float4& color ) const = 0;
        /**
         * @brief 걷는 면을 삼각형으로 냅니다(높이 디테일까지 — 바닥에 붙는다). 삼각형마다 점 셋을 @p outListCorner 에, 그 폴리곤의 영역 번호를
         *        @p outListArea 에 하나씩 더합니다. 그리는 쪽(디버그 메시)이 색을 고릅니다.
         */
        virtual void collectDebugTriangles( vector<float3>& outListCorner, vector<uint8>& outListArea ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 쓰는 백엔드를 고르는 자리 — 바꾸면 여기 한 줄이 바뀝니다. */
    struct SW_API NavMeshBackend
    {
        /** @brief 쿠킹본이 어느 백엔드의 바이트인지 적는 이름입니다(다른 백엔드의 쿠킹본은 읽지 않는다). */
        static const utf8* getBackendName();
        /** @brief 쿠킹본 바이트의 형식 판입니다(백엔드 라이브러리 판이 바뀌면 올린다). */
        static uint32 getBackendFormatVersion();
        /** @brief 빈 내비메시를 만듭니다. */
        static unique_ptr<INavMesh> createNavMesh();
    };
} // namespace sw
