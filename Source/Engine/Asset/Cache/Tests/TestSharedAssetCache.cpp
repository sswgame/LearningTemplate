#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/AssetReloadWatch.h"
#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
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

    /** @brief 다른 모듈이 올린 종류 자리입니다 — 한 모듈을 내려도 다른 모듈 캐시가 그대로인지 본다. */
    struct SharedAssetCacheOtherTestAsset
    {
        string _text;

        [[nodiscard]] static bool load( const AssetID& assetID, SharedAssetCacheOtherTestAsset& outAsset ) { return AssetIO::readTextAsset( assetID, outAsset._text ); }
    };
} // namespace

namespace sw
{
    /** 캐시 등록 줄과 같은 모양이다 — 정의 한 벌이 이름 · 로더를 든다(종류 등록과 모듈 서브시스템 등록은 테스트 이미지에서 하지 않는다). */
    template <>
    struct SharedAssetCacheTraits<SharedAssetCacheTestAsset>
    {
        static constexpr AssetTypeDef kType =
            AssetTypeDef::makeCacheOnly( "SharedAssetCacheTestAsset", AssetTypeLoader::with<SharedAssetCacheTestAsset, &SharedAssetCacheTestAsset::load>( MemoryTag::Game ) );
    };

    template class SharedAssetCache<SharedAssetCacheTestAsset>;

    template <>
    struct SharedAssetCacheTraits<SharedAssetCacheOtherTestAsset>
    {
        static constexpr AssetTypeDef kType = AssetTypeDef::makeCacheOnly(
            "SharedAssetCacheOtherTestAsset", AssetTypeLoader::with<SharedAssetCacheOtherTestAsset, &SharedAssetCacheOtherTestAsset::load>( MemoryTag::Game ) );
    };

    template class SharedAssetCache<SharedAssetCacheOtherTestAsset>;
} // namespace sw

namespace
{
    /** @brief `SW_MODULE_SHARED_ASSET_CACHE` 가 만드는 자식과 같은 모양입니다(그 매크로는 모듈 서브시스템 등록까지 해 테스트 이미지에서 쓰지 않는다). */
    class SharedAssetCacheTestSubsystem final : public SharedAssetCacheSubsystem<SharedAssetCacheTestAsset>
    {
    public:
        const utf8* getSubsystemName() const override { return "SharedAssetCache.Test"; }
    };

    class SharedAssetCacheOtherTestSubsystem final : public SharedAssetCacheSubsystem<SharedAssetCacheOtherTestAsset>
    {
    public:
        const utf8* getSubsystemName() const override { return "SharedAssetCache.OtherTest"; }
    };

    struct SharedAssetCacheTestUtil
    {
        using Cache      = SharedAssetCache<SharedAssetCacheTestAsset>;
        using OtherCache = SharedAssetCache<SharedAssetCacheOtherTestAsset>;

        /** @brief 모듈 서브시스템처럼 캐시를 세웠다 내립니다. */
        template <typename SubsystemType>
        class ScopedModuleCacheOf
        {
        public:
            ScopedModuleCacheOf()
                : _subsystem{}
            {
                SW_REQUIRE_TRUE( _subsystem.initializeSubsystem() == SubsystemInitResult::Succeeded );
            }
            ~ScopedModuleCacheOf() { _subsystem.shutdownSubsystem(); }

        private:
            SubsystemType _subsystem;
        };
        using ScopedModuleCache      = ScopedModuleCacheOf<SharedAssetCacheTestSubsystem>;
        using ScopedOtherModuleCache = ScopedModuleCacheOf<SharedAssetCacheOtherTestSubsystem>;
    };
} // namespace

/**
 * @brief [SharedAssetCacheTest] 모듈 캐시는 같은 경로에 같은 객체를 주고, 다시 읽으면 스냅숏은 옛 객체를 지키고 참조와 다시 받은 쪽은 새 내용을 보며, 내리면 레지스트리에서 빠지는지 검증
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

        const shared_ptr<const SharedAssetCacheTestAsset> held  = Util::Cache::acquire( id ).snapshot();
        const shared_ptr<const SharedAssetCacheTestAsset> again = Util::Cache::acquire( id ).snapshot();
        const SharedAssetRef<SharedAssetCacheTestAsset>   ref   = Util::Cache::acquire( id ); // 멤버로 쥐는 쪽의 모양
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
        SW_EXPECT_EQUAL( string( "first" ), held->_text ); // 스냅숏은 옛 객체 그대로
        SW_EXPECT_EQUAL( string( "second" ), ref->_text ); // 참조로 쥔 쪽은 다시 받지 않아도 새 내용을 본다

        // 쓰는 쪽은 횟수가 바뀐 것을 보고 다시 받는다 — 새 내용이다.
        const shared_ptr<const SharedAssetCacheTestAsset> fresh = Util::Cache::acquire( id ).snapshot();
        SW_REQUIRE_TRUE( fresh != nullptr );
        SW_EXPECT_TRUE( fresh.get() != held.get() );
        SW_EXPECT_EQUAL( string( "second" ), fresh->_text );
    }
    SW_EXPECT_TRUE( engine::getSubsystem<AssetManager>().findAssetCache( SharedAssetCacheTestUtil::Cache::kAssetKindName ) == nullptr );
}

/**
 * @brief [SharedAssetCacheTest] 모듈을 내리면 그 모듈이 올린 캐시 항목이 함께 사라지고(쥔 쪽은 옛 값을 그대로 읽는다), 다시 올리면 빈 표에서 새로 읽는지 검증
 * @details 캐시의 수명은 모듈 서브시스템이다(`SharedAssetCacheSubsystem` — 모듈의 자식 범위가 떼일 때 종료 · 소멸). 다시 올린 모듈이 앞 이미지의 항목을 받으면
 *          고친 파일을 보지 못하고, 앞 이미지가 지은 객체(그 모듈 코드의 소멸자)를 쥐게 된다.
 */
SW_TEST_CASE( SharedAssetCacheTest, ModuleUnloadDropsEntriesAndReloadStartsFresh )
{
    using Util = SharedAssetCacheTestUtil;
    const test::ScopedScratchDomain domain;
    const string                    id = test::ScopedScratchDomain::makeID( "module.txt" ).str();
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "module.txt" ), "first" ) );

    shared_ptr<const SharedAssetCacheTestAsset> held;
    SharedAssetRef<SharedAssetCacheTestAsset>   ref;
    {
        const Util::ScopedModuleCache loaded;
        held = Util::Cache::acquire( id ).snapshot();
        ref  = Util::Cache::acquire( id );
        SW_REQUIRE_TRUE( held != nullptr );
        AssetCacheBase* const pCache = engine::getSubsystem<AssetManager>().findAssetCache( Util::Cache::kAssetKindName );
        SW_REQUIRE_TRUE( pCache != nullptr );
        SW_EXPECT_TRUE( pCache->isCached( id ) );
    }
    // 내렸다 — 레지스트리에 없고, 쥔 쪽은 표보다 오래 살아 옛 값을 읽는다.
    SW_EXPECT_TRUE( engine::getSubsystem<AssetManager>().findAssetCache( Util::Cache::kAssetKindName ) == nullptr );
    SW_EXPECT_EQUAL( string( "first" ), held->_text );
    SW_EXPECT_EQUAL( string( "first" ), ref->_text );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "module.txt" ), "second" ) );
    {
        const Util::ScopedModuleCache reloaded;
        AssetCacheBase* const         pCache = engine::getSubsystem<AssetManager>().findAssetCache( Util::Cache::kAssetKindName );
        SW_REQUIRE_TRUE( pCache != nullptr );
        SW_EXPECT_FALSE( pCache->isCached( id ) ); // 앞 이미지의 항목을 이어 받지 않는다
        SW_EXPECT_EQUAL( size_t( 0 ), pCache->getCachedCount() );

        const shared_ptr<const SharedAssetCacheTestAsset> fresh = Util::Cache::acquire( id ).snapshot();
        SW_REQUIRE_TRUE( fresh != nullptr );
        SW_EXPECT_TRUE( fresh.get() != held.get() );
        SW_EXPECT_EQUAL( string( "second" ), fresh->_text );
        SW_EXPECT_EQUAL( string( "first" ), ref->_text ); // 내린 표의 참조는 새 표의 판을 따라가지 않는다 — 다시 받아야 한다
    }
}

/**
 * @brief [SharedAssetCacheTest] 한 모듈을 내렸다 다시 올려도 다른 모듈이 올린 캐시는 등록 · 항목 · 객체가 그대로이고, 다시 올린 모듈만 빈 표에서 새로 읽는지 검증
 * @details 캐시 등록은 종류 이름 하나씩이라(레지스트리) 한 모듈의 종료가 다른 모듈의 칸을 지우면 그 모듈은 다음 요청에서 멈춘다(`SharedAssetCacheLookup::getCache`).
 */
SW_TEST_CASE( SharedAssetCacheTest, ModuleUnloadLeavesOtherModuleCacheIntact )
{
    using Util = SharedAssetCacheTestUtil;
    const test::ScopedScratchDomain domain;
    const string                    id      = test::ScopedScratchDomain::makeID( "mine.txt" ).str();
    const string                    otherID = test::ScopedScratchDomain::makeID( "other.txt" ).str();
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "mine.txt" ), "first" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "other.txt" ), "other" ) );

    AssetManager&                                          assetManager = engine::getSubsystem<AssetManager>();
    const Util::ScopedOtherModuleCache                     otherModule;
    const shared_ptr<const SharedAssetCacheOtherTestAsset> otherHeld = Util::OtherCache::acquire( otherID ).snapshot();
    SW_REQUIRE_TRUE( otherHeld != nullptr );
    AssetCacheBase* const pOtherCache = assetManager.findAssetCache( Util::OtherCache::kAssetKindName );
    SW_REQUIRE_TRUE( pOtherCache != nullptr );

    shared_ptr<const SharedAssetCacheTestAsset> held;
    {
        const Util::ScopedModuleCache module;
        held = Util::Cache::acquire( id ).snapshot();
        SW_REQUIRE_TRUE( held != nullptr );
    }
    // 내린 모듈만 빠지고 다른 모듈의 캐시는 같은 칸 · 같은 항목이다.
    SW_EXPECT_TRUE( assetManager.findAssetCache( Util::Cache::kAssetKindName ) == nullptr );
    SW_EXPECT_TRUE( assetManager.findAssetCache( Util::OtherCache::kAssetKindName ) == pOtherCache );
    SW_EXPECT_TRUE( pOtherCache->isCached( otherID ) );
    SW_EXPECT_EQUAL( size_t( 1 ), pOtherCache->getCachedCount() );
    SW_EXPECT_TRUE( assetManager.findReloadFunction( pOtherCache ) != nullptr ); // 핫 리로드 칸도 남는다
    SW_EXPECT_TRUE( Util::OtherCache::acquire( otherID ).snapshot().get() == otherHeld.get() );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "mine.txt" ), "second" ) );
    {
        const Util::ScopedModuleCache reloaded;
        AssetCacheBase* const         pCache = assetManager.findAssetCache( Util::Cache::kAssetKindName );
        SW_REQUIRE_TRUE( pCache != nullptr );
        SW_EXPECT_FALSE( pCache->isCached( id ) );
        const shared_ptr<const SharedAssetCacheTestAsset> fresh = Util::Cache::acquire( id ).snapshot();
        SW_REQUIRE_TRUE( fresh != nullptr );
        SW_EXPECT_EQUAL( string( "second" ), fresh->_text );
        SW_EXPECT_TRUE( Util::OtherCache::acquire( otherID ).snapshot().get() == otherHeld.get() ); // 다시 올려도 다른 모듈 항목을 건드리지 않는다
    }
    SW_EXPECT_TRUE( assetManager.findAssetCache( Util::OtherCache::kAssetKindName ) == pOtherCache );
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
    const shared_ptr<const SharedAssetCacheTestAsset> held = Util::Cache::acquire( id ).snapshot();
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

/** @brief [SharedAssetRefTest] 빌린 참조의 스냅숏은 빌린 값을 가리킨다(소유하지 않는다) — 비어 있으면 스냅숏을 쥐는 워커 일감이 null 을 읽었다 */
SW_TEST_CASE( SharedAssetRefTest, BorrowedSnapshotPointsAtTheBorrowedValue )
{
    const int32                       value = 7;
    const sw::SharedAssetRef<int32>   ref   = sw::SharedAssetRef<int32>::borrow( &value );
    const sw::shared_ptr<const int32> held  = ref.snapshot();
    SW_REQUIRE_TRUE( held != nullptr );
    SW_EXPECT_EQUAL( &value, held.get() );
    SW_EXPECT_EQUAL( 0L, held.use_count() ); // 소유권 없는 별칭이다
}
