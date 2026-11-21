// 에셋 캐시 다시 읽기의 결과 — 깨진 파일을 저장하면 리로드 함수가 Failed 를 돌려주고 캐시는 이전 판을 지키며, 집계가 그 경로를 낡음(STALE)으로 센다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/AssetReload/ReloadSource.h"
#include "DevTools/AssetReload/ReloadTracker.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/TileMap/TileSetAsset.h"

using namespace sw;

namespace
{
    constexpr const utf8* kMaterialXML = "<MaterialDef formatVersion=\"0\" name=\"Probe\" shaderPath=\"engine/shaders/forward_lit.hlsl\" surfaceModel=\"Lit\" alphaMode=\"Opaque\"><_properties>"
                                         "<item name=\"_roughness\" type=\"Range\" defaultValue=\"0.3\" min=\"0.0\" max=\"1.0\" /></_properties></MaterialDef>";
    // 뿌리 요소가 틀린 머티리얼 — 저장 도중 다른 글이 덮인 파일과 같다(읽기는 "MaterialDef 뿌리 없음" 으로 진다).
    constexpr const utf8* kBrokenMaterialXML = "<NotAMaterial name=\"Probe\" />";
    // 브러시 목록(카탈로그) 하나 — 공유 에셋 캐시(`SharedAssetCache`)로 도는 데이터 파일의 대표다. 키트 카탈로그(몬스터 · 상점 · 하우징)가 같은 캐시 틀이다.
    constexpr const utf8* kCatalogXML       = "<TileSet atlas=\"engine/textures/test/quadrants.dds\" columns=\"4\" rows=\"4\" tileSize=\"1\"><Tile name=\"grass\" cell=\"0\"/></TileSet>";
    constexpr const utf8* kBrokenCatalogXML = "<TileSet atlas=\"engine/textures/test/quadrants.dds\" columns=\"4\" rows=\"4\" tileSize=\"1\"><Tile name=\"grass\"/></TileSet>"; // 모습(cell · frames) 없는 칸

    /** @brief 레지스트리에서 그 종류의 리로드 함수를 찾아 부릅니다(`AssetHotReload` 의 길과 같다). */
    ReloadResult reloadThroughRegistry( const utf8* pKindName, const AssetID& assetID )
    {
        AssetManager&                  assets    = engine::getSubsystem<AssetManager>();
        AssetCacheBase* const          pCache    = assets.findAssetCache( pKindName );
        const AssetCacheReloadFunction pfnReload = pCache != nullptr ? assets.findReloadFunction( pCache ) : nullptr;
        SW_EXPECT_TRUE( pfnReload != nullptr );
        return pfnReload != nullptr ? pfnReload( *pCache, assetID ) : ReloadResult::NotTracked;
    }
} // namespace

/**
 * @brief [AssetCacheReloadResultTest] 깨진 머티리얼을 저장하면 리로드가 Failed 이고 쥔 머티리얼은 이전 값을 지키며, 집계의 낡은 수가 1 이다
 * @details 예전에는 리로드 함수가 `void` 라 부르는 쪽(`AssetHotReload`)이 실패도 Reloaded 로 적었다 — `reload.status` 가 깨진 파일을 OK 로 보였다.
 */
SW_TEST_CASE( AssetCacheReloadResultTest, BrokenMaterialFailsAndKeepsPreviousVersion )
{
    const test::ScopedScratchDomain domain;
    const AssetID                   id = test::ScopedScratchDomain::makeID( "probe.material" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( domain.makeDiskPath( "probe.material" ) ), kMaterialXML ) );

    MaterialCache& cache     = engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>();
    Material*      pMaterial = cache.acquire( id.view() );
    SW_REQUIRE_NOT_NULL( pMaterial );
    SW_REQUIRE_TRUE( pMaterial->loadFromAsset( id ) );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( domain.makeDiskPath( "probe.material" ) ), kBrokenMaterialXML ) );
    SW_EXPECT_LOG( "MaterialCache.HotReloadParseFailed" );
    const ReloadResult result = reloadThroughRegistry( MaterialCache::kAssetKindName, id );
    SW_EXPECT_TRUE( result == ReloadResult::Failed );
    const MaterialProperty* pRoughness = pMaterial->findProperty( hashed_string( "_roughness" ) );
    SW_REQUIRE_NOT_NULL( pRoughness );
    SW_EXPECT_EQUAL( string( "0.3" ), pRoughness->_defaultValue ); // 이전 판 그대로

    ReloadTracker tracker;
    tracker.record( ReloadSource::Asset, id.view(), result, "parse failed" );
    SW_EXPECT_EQUAL( uint32( 1 ), tracker.countStale() );
    cache.release( id.view() );
}

/**
 * @brief [AssetCacheReloadResultTest] 깨진 카탈로그를 저장하면 공유 캐시 리로드가 Failed 이고 다시 받은 쪽도 이전 판이며, 집계의 낡은 수가 1 이다 — 고쳐 저장하면 풀린다
 */
SW_TEST_CASE( AssetCacheReloadResultTest, BrokenCatalogFailsAndKeepsPreviousVersion )
{
    const test::ScopedScratchDomain domain;
    const AssetID                   id = test::ScopedScratchDomain::makeID( "probe.tileset.xml" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( domain.makeDiskPath( "probe.tileset.xml" ) ), kCatalogXML ) );
    const shared_ptr<const TileSetAsset> pHeld = SharedAssetCache<TileSetAsset>::acquire( id.view() ).snapshot();
    SW_REQUIRE_NOT_NULL( pHeld );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( domain.makeDiskPath( "probe.tileset.xml" ) ), kBrokenCatalogXML ) );
    SW_EXPECT_LOG( "TileSet.Invalid" );
    const ReloadResult broken = reloadThroughRegistry( SharedAssetCache<TileSetAsset>::kAssetKindName, id );
    SW_EXPECT_TRUE( broken == ReloadResult::Failed );
    const shared_ptr<const TileSetAsset> pAfter = SharedAssetCache<TileSetAsset>::acquire( id.view() ).snapshot();
    SW_EXPECT_TRUE( pAfter.get() == pHeld.get() ); // 새 판을 넣지 않았다

    ReloadTracker tracker;
    tracker.record( ReloadSource::Asset, id.view(), broken, "parse failed" );
    SW_EXPECT_EQUAL( uint32( 1 ), tracker.countStale() );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( domain.makeDiskPath( "probe.tileset.xml" ) ), kCatalogXML ) );
    const ReloadResult fixed = reloadThroughRegistry( SharedAssetCache<TileSetAsset>::kAssetKindName, id );
    SW_EXPECT_TRUE( fixed == ReloadResult::Reloaded );
    tracker.record( ReloadSource::Asset, id.view(), fixed );
    SW_EXPECT_EQUAL( uint32( 0 ), tracker.countStale() );
}

/** @brief [AssetCacheReloadResultTest] 아무도 쥐지 않은 경로는 NotTracked 다 — 집계는 적지 않는다(다음에 읽는 쪽이 새 파일을 읽는다) */
SW_TEST_CASE( AssetCacheReloadResultTest, UnheldPathIsNotTracked )
{
    const test::ScopedScratchDomain domain;
    const AssetID                   id = test::ScopedScratchDomain::makeID( "unheld.tileset.xml" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( domain.makeDiskPath( "unheld.tileset.xml" ) ), kBrokenCatalogXML ) );
    SW_EXPECT_TRUE( reloadThroughRegistry( SharedAssetCache<TileSetAsset>::kAssetKindName, id ) == ReloadResult::NotTracked );
}
