// MaterialCacheTest — 머티리얼 캐시의 키가 리다이렉트를 푼 지금 id 인지(nogpu — `acquire` 는 초기화하지 않는다).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Serialization/Inherit/AssetRedirect.h"

using namespace sw;

/**
 * @brief [MaterialCacheTest] 옮긴 머티리얼의 옛 id 와 지금 id 로 받으면 한 칸의 같은 객체인지 검증(A-20 — 요청 글이 키이면 두 벌 올랐다)
 */
SW_TEST_CASE( MaterialCacheTest, RedirectedAndCanonicalIdsShareOneEntry )
{
    const string oldID = "engine/test/a20/old.material.xml";
    const string newID = "engine/test/a20/new.material.xml";
    SW_REQUIRE_TRUE( AssetRedirect::loadFromXMLTexts( vector<string>{ "<Redirects><Redirect from=\"" + oldID + "\" to=\"" + newID + "\"/></Redirects>" },
                                                      vector<string>{ "test/redirects.xml" } ) );
    MaterialCache& cache       = engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>();
    const size_t   countBefore = cache.getCachedCount();
    {
        const Material* const pByOld = cache.acquire( oldID );
        const Material* const pByNew = cache.acquire( "Engine/Test\\A20/New.material.xml" );
        SW_REQUIRE_TRUE( pByOld != nullptr );
        SW_EXPECT_TRUE( pByOld == pByNew );
        SW_EXPECT_TRUE( cache.isCached( oldID ) );
        SW_EXPECT_TRUE( cache.isCached( newID ) );
        SW_EXPECT_EQUAL( countBefore + 1, cache.getCachedCount() );
        cache.release( newID );
        cache.release( oldID );
    }
    SW_EXPECT_FALSE( cache.isCached( newID ) );
    SW_EXPECT_EQUAL( countBefore, cache.getCachedCount() );
    AssetRedirect::clear();
}
