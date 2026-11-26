// MaterialCacheTest — 머티리얼 캐시의 키가 한 곳(`AssetRegistry::toCacheKey`)을 지나는지(nogpu — `acquire` 는 초기화하지 않는다).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/Material/MaterialCache.h"

using namespace sw;

/**
 * @brief [MaterialCacheTest] 다른 철자와 GUID 참조 글로 받아도 한 칸의 같은 객체인지 검증(A-20 — 요청 글이 키이면 두 벌 올랐다)
 */
SW_TEST_CASE( MaterialCacheTest, SpellingsAndGUIDReferencesShareOneEntry )
{
    const AssetGUID               guid     = *AssetGUID::tryParse( "0000000000000000000000000000a020" );
    const string                  assetID  = "engine/test/a20/new.material.xml";
    const expected<AssetRegistry> registry = AssetRegistry::merge( vector<AssetRegistryLayer>{
        AssetRegistryLayer{ "one", { AssetRegistryEntry{ guid, assetID } } }
    } );
    SW_REQUIRE_TRUE( registry.has_value() );
    AssetRegistry  previous    = AssetRegistry::install( *registry );
    MaterialCache& cache       = engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>();
    const size_t   countBefore = cache.getCachedCount();
    const string   byGUIDText  = AssetRegistry::makeReferenceText( guid );
    {
        const Material* const pByPath = cache.acquire( "Engine/Test\\A20/New.material.xml" );
        const Material* const pByGUID = cache.acquire( byGUIDText );
        SW_REQUIRE_TRUE( pByPath != nullptr );
        SW_EXPECT_TRUE( pByPath == pByGUID );
        SW_EXPECT_TRUE( cache.isCached( assetID ) );
        SW_EXPECT_TRUE( cache.isCached( byGUIDText ) );
        SW_EXPECT_EQUAL( countBefore + 1, cache.getCachedCount() );
        cache.release( byGUIDText );
        cache.release( assetID );
    }
    SW_EXPECT_FALSE( cache.isCached( assetID ) );
    SW_EXPECT_EQUAL( countBefore, cache.getCachedCount() );
    (void)AssetRegistry::install( std::move( previous ) );
}
