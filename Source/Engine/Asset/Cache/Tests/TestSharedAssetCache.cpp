#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/AssetReloadWatch.h"
#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/EngineServices.h"

using namespace sw;

// SharedAssetCacheTest — 모듈이 자기 종류를 인스턴스화해 모듈 수명으로 올리는 공유 캐시(`SW_MODULE_SHARED_ASSET_CACHE` 의 몸통)를 테스트 이미지가 같은 길로 세워 본다.

namespace
{
    /** @brief 글 한 줄을 그대로 드는 테스트 에셋입니다. */
    struct SharedAssetCacheTestAsset
    {
        string _text;

        [[nodiscard]] static bool load( const AssetID& assetID, SharedAssetCacheTestAsset& outAsset ) { return AssetIO::readTextAsset( assetID, outAsset._text ); }
    };
} // namespace

namespace sw
{
    /** 캐시 등록 줄과 같은 모양이다 — 정의 한 벌이 이름 · 로더 · 다시 읽기 방식을 든다(종류 등록과 모듈 서브시스템 등록은 테스트 이미지에서 하지 않는다). */
    template <>
    struct SharedAssetCacheTraits<SharedAssetCacheTestAsset>
    {
        static constexpr AssetTypeDef kType =
            AssetTypeDef::makeCacheOnly( "SharedAssetCacheTestAsset", AssetTypeLoader::with<SharedAssetCacheTestAsset, &SharedAssetCacheTestAsset::load>( MemoryTag::Game, SharedAssetReloadMode::Detach ) );
    };

    template class SharedAssetCache<SharedAssetCacheTestAsset>;
} // namespace sw

namespace
{
    /** @brief `SW_MODULE_SHARED_ASSET_CACHE` 가 만드는 자식과 같은 모양입니다(그 매크로는 모듈 서브시스템 등록까지 해 테스트 이미지에서 쓰지 않는다). */
    class SharedAssetCacheTestSubsystem final : public SharedAssetCacheSubsystem<SharedAssetCacheTestAsset>
    {
    public:
        const utf8* getSubsystemName() const override { return "SharedAssetCache.Test"; }
    };

    struct SharedAssetCacheTestUtil
    {
        using Cache = SharedAssetCache<SharedAssetCacheTestAsset>;

        /** @brief 모듈 서브시스템처럼 캐시를 세웠다 내립니다. */
        class ScopedModuleCache
        {
        public:
            ScopedModuleCache()
                : _subsystem{}
            {
                SW_REQUIRE_TRUE( _subsystem.initializeSubsystem() == SubsystemInitResult::Succeeded );
            }
            ~ScopedModuleCache() { _subsystem.shutdownSubsystem(); }

        private:
            SharedAssetCacheTestSubsystem _subsystem;
        };
    };
} // namespace

/**
 * @brief [SharedAssetCacheTest] 모듈 캐시는 같은 경로에 같은 객체를 주고, 다시 읽으면 쥔 쪽은 옛 객체를 지키며 다시 받은 쪽은 새 내용을 보고, 내리면 레지스트리에서 빠지는지 검증
 */
SW_TEST_CASE( SharedAssetCacheTest, ModuleCacheSharesAndReloadReachesHolders )
{
    using Util = SharedAssetCacheTestUtil;
    const test::ScopedScratchDomain domain;
    const string                    id = test::ScopedScratchDomain::makeID( "table.txt" ).str();
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "table.txt" ), "first" ) );
    {
        const Util::ScopedModuleCache scope;
        AssetCacheBase* const         pCache = engine::getSubsystem<AssetManager>().findAssetCache( Util::Cache::kAssetKindName );
        SW_REQUIRE_TRUE( pCache != nullptr );

        const shared_ptr<const SharedAssetCacheTestAsset> held  = Util::Cache::acquire( id );
        const shared_ptr<const SharedAssetCacheTestAsset> again = Util::Cache::acquire( id );
        SW_REQUIRE_TRUE( held != nullptr );
        SW_EXPECT_TRUE( held.get() == again.get() );
        SW_EXPECT_TRUE( pCache->isCached( id ) );

        // 파일을 고치고 핫 리로드가 부르는 길(레지스트리의 리로드 함수)로 다시 읽는다.
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "table.txt" ), "second" ) );
        AssetReloadWatch<SharedAssetCacheTestAsset> watch;
        watch.markSeen();
        const uint32 reloadsBefore = watch.getSeenCount();
        SW_REQUIRE_TRUE( Util::Cache::reloadShared( id ) );
        watch.markSeen();
        SW_EXPECT_EQUAL( reloadsBefore + 1, watch.getSeenCount() );
        SW_EXPECT_EQUAL( string( "first" ), held->_text ); // 쥔 쪽은 옛 객체 그대로(Detach)

        // 쓰는 쪽은 횟수가 바뀐 것을 보고 다시 받는다 — 새 내용이다.
        const shared_ptr<const SharedAssetCacheTestAsset> fresh = Util::Cache::acquire( id );
        SW_REQUIRE_TRUE( fresh != nullptr );
        SW_EXPECT_TRUE( fresh.get() != held.get() );
        SW_EXPECT_EQUAL( string( "second" ), fresh->_text );
    }
    SW_EXPECT_TRUE( engine::getSubsystem<AssetManager>().findAssetCache( SharedAssetCacheTestUtil::Cache::kAssetKindName ) == nullptr );
}

/**
 * @brief [SharedAssetCacheTest] 다시 읽기 감시는 받은 뒤에는 낡지 않고, 다시 읽히면 `isStale` 이 알리며(적지 않는다) `takeChanged` 는 다시 읽기마다 한 번만 참인지 검증
 */
SW_TEST_CASE( SharedAssetCacheTest, ReloadWatchReportsEachReloadOnce )
{
    using Util = SharedAssetCacheTestUtil;
    const test::ScopedScratchDomain domain;
    const string                    id = test::ScopedScratchDomain::makeID( "watched.txt" ).str();
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "watched.txt" ), "first" ) );
    const Util::ScopedModuleCache                     scope;
    const shared_ptr<const SharedAssetCacheTestAsset> held = Util::Cache::acquire( id );
    SW_REQUIRE_TRUE( held != nullptr );

    AssetReloadWatch<SharedAssetCacheTestAsset> watch;
    watch.markSeen();
    SW_EXPECT_FALSE( watch.isStale() );
    SW_EXPECT_FALSE( watch.takeChanged() );

    SW_REQUIRE_TRUE( Util::Cache::reloadShared( id ) );
    SW_EXPECT_TRUE( watch.isStale() );
    SW_EXPECT_TRUE( watch.isStale() ); // 묻기만 해서는 적지 않는다
    SW_EXPECT_TRUE( watch.takeChanged() );
    SW_EXPECT_FALSE( watch.takeChanged() );
    SW_EXPECT_FALSE( watch.isStale() );
}
