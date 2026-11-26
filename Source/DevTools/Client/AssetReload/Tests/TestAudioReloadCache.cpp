// AudioReloadCacheTest — 오디오 데이터의 리로드 어댑터("AudioData")는 캐시 레지스트리에 있고, 오디오 서비스가 쓰지 않는 파일은 자기 것이 아니라고 답한다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Serialization/Serializer/ReloadResult.h"

using namespace sw;

/** @brief [AudioReloadCacheTest] 쓰지 않는 경로는 캐시에 없고, 리로드는 다른 곳으로 넘긴다(NotTracked) — 빈 경로도 같다 */
SW_TEST_CASE( AudioReloadCacheTest, UnusedFilesAreNotTracked )
{
    AssetManager&   assets = engine::getSubsystem<AssetManager>();
    AssetCacheBase* pCache = assets.findAssetCache( "AudioData" );
    SW_REQUIRE_NOT_NULL( pCache );
    SW_EXPECT_FALSE( pCache->isCached( "engine/no_such_file.audiomixer.xml" ) );
    SW_EXPECT_FALSE( pCache->isCached( "" ) );
    const AssetCacheReloadFunction pfnReload = assets.findReloadFunction( pCache );
    SW_REQUIRE_TRUE( pfnReload != nullptr );
    SW_EXPECT_TRUE( pfnReload( *pCache, AssetID{ "engine/no_such_file.audioevents.xml" } ) == ReloadResult::NotTracked );
    const size_t cachedCount = pCache->getCachedCount();
    pCache->clear(); // 데이터는 오디오 서비스의 것이라 비우지 않는다
    SW_EXPECT_EQUAL( cachedCount, pCache->getCachedCount() );
}
