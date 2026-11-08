#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Lighting/ShadowCache.h"

// 그림자 캐시의 규칙(타일마다 DrawAll · Rebuild · Reuse)을 GPU 없이 본다. 그림은 시나리오 game.empty.shadow_cache 가 본다.

namespace sw
{
    namespace
    {
        struct ShadowCacheTestInternal
        {
            static constexpr uint32 kAtlasResolution = 2048;

            /** @brief 칸 @p cellIndex 의 타일입니다. 행렬의 이동 칸 하나가 빛 위치를 대신한다. */
            static ShadowTileDraw makeTile( uint32 cellIndex, float32 lightX )
            {
                ShadowTileDraw tile{};
                tile._viewProj     = float4x4::Identity;
                tile._viewProj._41 = lightX;
                tile._cell._x      = cellIndex * 512u;
                tile._cell._y      = 1024u;
                tile._cell._size   = 512u;
                return tile;
            }
        };
    } // namespace
} // namespace sw

/**
 * @brief [ShadowCacheTest] 캐시를 끄면 모두 DrawAll 이고, 켠 첫 프레임은 모두 Rebuild, 그 뒤로 아무것도 바뀌지 않으면 Reuse 다
 */
SW_TEST_CASE( ShadowCacheTest, StationaryLightsRebuildOnceThenReuse )
{
    using Internal = sw::ShadowCacheTestInternal;
    using Action   = sw::ShadowCacheTileAction;
    const sw::vector<sw::ShadowTileDraw> listTile{ Internal::makeTile( 0, 1.0f ), Internal::makeTile( 1, 2.0f ) };
    sw::ShadowCache                      cache;

    cache.update( listTile, Internal::kAtlasResolution, 7, false );
    SW_EXPECT_TRUE( cache.getAction( 0 ) == Action::DrawAll && cache.getAction( 1 ) == Action::DrawAll );
    SW_EXPECT_FALSE( cache.usesCacheLayer() );
    SW_EXPECT_FALSE( cache.needsRebuild() );

    cache.update( listTile, Internal::kAtlasResolution, 7, true );
    SW_EXPECT_TRUE( cache.getAction( 0 ) == Action::Rebuild && cache.getAction( 1 ) == Action::Rebuild );
    SW_EXPECT_TRUE( cache.needsRebuild() );
    SW_EXPECT_TRUE( cache.usesCacheLayer() );

    cache.update( listTile, Internal::kAtlasResolution, 7, true );
    SW_EXPECT_TRUE( cache.getAction( 0 ) == Action::Reuse && cache.getAction( 1 ) == Action::Reuse );
    SW_EXPECT_FALSE( cache.needsRebuild() );
    SW_EXPECT_TRUE( cache.usesCacheLayer() );
    SW_EXPECT_TRUE( cache.getAction( 9 ) == Action::DrawAll ); // 범위 밖
}

/**
 * @brief [ShadowCacheTest] 빛이 움직인 프레임은 그 타일만 DrawAll(층은 그대로)이고, 멈춘 다음 프레임에 층 전체를 다시 그린다
 */
SW_TEST_CASE( ShadowCacheTest, MovingLightDrawsAllAndRebuildsWhenItStops )
{
    using Internal = sw::ShadowCacheTestInternal;
    using Action   = sw::ShadowCacheTileAction;
    sw::ShadowCache cache;
    cache.update( { Internal::makeTile( 0, 1.0f ), Internal::makeTile( 1, 2.0f ) }, Internal::kAtlasResolution, 1, true );
    cache.update( { Internal::makeTile( 0, 1.0f ), Internal::makeTile( 1, 2.0f ) }, Internal::kAtlasResolution, 1, true );

    // 빛 0 이 움직인다 — 그 타일은 다 그리고, 빛 1 은 캐시를 그대로 쓴다.
    cache.update( { Internal::makeTile( 0, 1.5f ), Internal::makeTile( 1, 2.0f ) }, Internal::kAtlasResolution, 1, true );
    SW_EXPECT_TRUE( cache.getAction( 0 ) == Action::DrawAll );
    SW_EXPECT_TRUE( cache.getAction( 1 ) == Action::Reuse );
    SW_EXPECT_FALSE( cache.needsRebuild() );

    // 계속 움직이면 계속 DrawAll 이다.
    cache.update( { Internal::makeTile( 0, 1.75f ), Internal::makeTile( 1, 2.0f ) }, Internal::kAtlasResolution, 1, true );
    SW_EXPECT_TRUE( cache.getAction( 0 ) == Action::DrawAll );

    // 멈추면 층을 통째로 다시 그린다(빛 1 도 다시 그린다 — 층을 지우므로).
    cache.update( { Internal::makeTile( 0, 1.75f ), Internal::makeTile( 1, 2.0f ) }, Internal::kAtlasResolution, 1, true );
    SW_EXPECT_TRUE( cache.getAction( 0 ) == Action::Rebuild && cache.getAction( 1 ) == Action::Rebuild );
    SW_EXPECT_TRUE( cache.needsRebuild() );
}

/**
 * @brief [ShadowCacheTest] 정적 메시가 움직이거나 더해지고 빠지면(정적 그림자 세대) · 아틀라스 해상도가 바뀌면 모든 타일을 다시 그린다. 칸이 바뀐 타일은 움직인 빛과 같다
 */
SW_TEST_CASE( ShadowCacheTest, StaticSceneChangeAndResolutionChangeRebuildEveryTile )
{
    using Internal = sw::ShadowCacheTestInternal;
    using Action   = sw::ShadowCacheTileAction;
    const sw::vector<sw::ShadowTileDraw> listTile{ Internal::makeTile( 0, 1.0f ), Internal::makeTile( 1, 2.0f ) };
    sw::ShadowCache                      cache;
    cache.update( listTile, Internal::kAtlasResolution, 3, true ); // 처음 본 빛 — 다 그린다
    cache.update( listTile, Internal::kAtlasResolution, 3, true ); // 멈췄다 — 층을 채운다(Rebuild)
    cache.update( listTile, Internal::kAtlasResolution, 3, true ); // 층이 유효하다
    SW_REQUIRE_TRUE( cache.getAction( 0 ) == Action::Reuse );

    cache.update( listTile, Internal::kAtlasResolution, 4, true ); // 정적 메시가 바뀌었다
    SW_EXPECT_TRUE( cache.getAction( 0 ) == Action::Rebuild && cache.getAction( 1 ) == Action::Rebuild );
    cache.update( listTile, Internal::kAtlasResolution, 4, true );
    SW_EXPECT_TRUE( cache.getAction( 0 ) == Action::Reuse );

    cache.update( listTile, Internal::kAtlasResolution / 2u, 4, true ); // 그림자 품질이 바뀌었다
    SW_EXPECT_TRUE( cache.getAction( 0 ) == Action::Rebuild && cache.getAction( 1 ) == Action::Rebuild );

    // 우선순위가 바뀌어 빛이 다른 칸을 받으면 키가 다르다 — 그 프레임은 DrawAll, 그다음 Rebuild.
    const sw::vector<sw::ShadowTileDraw> listSwapped{ Internal::makeTile( 1, 1.0f ), Internal::makeTile( 0, 2.0f ) };
    cache.update( listSwapped, Internal::kAtlasResolution / 2u, 4, true );
    SW_EXPECT_TRUE( cache.getAction( 0 ) == Action::DrawAll && cache.getAction( 1 ) == Action::DrawAll );
    SW_EXPECT_FALSE( cache.usesCacheLayer() );
    cache.update( listSwapped, Internal::kAtlasResolution / 2u, 4, true );
    SW_EXPECT_TRUE( cache.getAction( 0 ) == Action::Rebuild && cache.getAction( 1 ) == Action::Rebuild );

    // 타일이 늘면 새 타일은 지난 키가 없어 DrawAll 이다. 줄면 남은 것만 본다.
    const sw::vector<sw::ShadowTileDraw> listGrown{ Internal::makeTile( 1, 1.0f ), Internal::makeTile( 0, 2.0f ), Internal::makeTile( 2, 3.0f ) };
    cache.update( listGrown, Internal::kAtlasResolution / 2u, 4, true );
    SW_EXPECT_TRUE( cache.getAction( 0 ) == Action::Reuse && cache.getAction( 2 ) == Action::DrawAll );

    // 끄면 무효가 되어 다시 켠 첫 프레임은 Rebuild 다.
    cache.update( listGrown, Internal::kAtlasResolution / 2u, 4, false );
    cache.update( listGrown, Internal::kAtlasResolution / 2u, 4, true );
    SW_EXPECT_TRUE( cache.getAction( 0 ) == Action::Rebuild && cache.getAction( 2 ) == Action::Rebuild );
}
