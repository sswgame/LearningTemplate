#include "pch.h"

#include "Core/String/Name/hashed_string.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshSettings.h"

using namespace sw;

namespace
{
    struct RecastNavMeshTestUtil
    {
        /** @brief 에이전트 종류 하나 · 영역 하나의 설정입니다. 타일 한 변은 `_tileSize * _cellSize`(기본 48 × 0.2 = 9.6 m)입니다. */
        static NavMeshSettings makeSettings( NavAgentTypeDef& outAgentType, NavObstacleMode obstacleMode )
        {
            outAgentType._name         = hashed_string{ "Walker" };
            outAgentType._obstacleMode = obstacleMode;
            NavAreaDef area;
            area._name = hashed_string{ "Walkable" };
            NavMeshSettings settings;
            settings._listAgentType.push_back( outAgentType );
            settings._listArea.push_back( area );
            settings._defaultAgentType = outAgentType._name;
            return settings;
        }

        /** @brief 가로 @p tileCountX × 세로 @p tileCountZ 타일이 되는 경계입니다(한 변을 타일 크기보다 조금 작게 잡아 올림이 정확히 그 수가 되게). */
        static AABB makeBounds( const NavAgentTypeDef& agentType, int32 tileCountX, int32 tileCountZ )
        {
            const float32 tileWorldSize = static_cast<float32>( agentType._tileSize ) * agentType._cellSize;
            return AABB{
                float3{                                                     0.0f, 0.0f,                                                      0.0f},
                float3{static_cast<float32>( tileCountX ) * tileWorldSize - 1.0f, 1.0f, static_cast<float32>( tileCountZ ) * tileWorldSize - 1.0f}
            };
        }
    };
    using Util = RecastNavMeshTestUtil;
} // namespace

/**
 * @brief [RecastNavMeshTest] 타일 자리가 2^14 를 넘는 월드도 선다 — 32 비트 폴리 참조(타일 · 폴리 22 비트)는 16384 자리에서 초기화를 거절했다(감사 S-6).
 *        다시 굽기 모드 160 × 128 = 20480 타일, 장애물 캐시 모드 128 × 64 = 8192 타일(층 4 개라 자리 32768).
 */
SW_TEST_CASE( RecastNavMeshTest, LargeGridFitsPolyRef )
{
    NavAgentTypeDef       rebakeAgent;
    const NavMeshSettings rebakeSettings = Util::makeSettings( rebakeAgent, NavObstacleMode::RebakeTiles );
    unique_ptr<INavMesh>  pRebake        = NavMeshBackend::createNavMesh();
    SW_REQUIRE_NOT_NULL( pRebake );
    SW_REQUIRE_TRUE( pRebake->initialize( rebakeAgent, rebakeSettings, Util::makeBounds( rebakeAgent, 160, 128 ) ) );
    SW_EXPECT_EQUAL( 20480, pRebake->getTileGrid().getTileCount() );

    NavAgentTypeDef       cacheAgent;
    const NavMeshSettings cacheSettings = Util::makeSettings( cacheAgent, NavObstacleMode::TileCache );
    unique_ptr<INavMesh>  pCache        = NavMeshBackend::createNavMesh();
    SW_REQUIRE_NOT_NULL( pCache );
    SW_REQUIRE_TRUE( pCache->initialize( cacheAgent, cacheSettings, Util::makeBounds( cacheAgent, 128, 64 ) ) );
    SW_EXPECT_EQUAL( 8192, pCache->getTileGrid().getTileCount() );
}

/** @brief [RecastNavMeshTest] 쿠킹본 백엔드 판은 폴리 참조 폭(64)을 싣는다 — 32 비트 참조로 구운 옛 판(Detour 판 7 그대로)과 같으면 옛 타일 바이트를 그대로 읽는다 */
SW_TEST_CASE( RecastNavMeshTest, BackendFormatCarriesPolyRefWidth )
{
    constexpr uint32 kOldPolyRef32Version = 7; // 64 비트 참조 전의 판(`DT_NAVMESH_VERSION`)
    SW_EXPECT_NOT_EQUAL( kOldPolyRef32Version, NavMeshBackend::getBackendFormatVersion() );
    SW_EXPECT_EQUAL( 64u, NavMeshBackend::getBackendFormatVersion() % 1000u );
}
