#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Module/ModuleCodeHolder.h"

#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/IAssetCache.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/SpriteClipCache.h"

#include "TestFramework/TestFramework.h"

// AssetCacheRegistryTest — 에셋 캐시 등록부(종료 · 진단 · 핫 리로드가 훑는 정본). 전역을 건드리지 않는다(지역 ResourceManager).

namespace
{
    /** @brief 등록부가 자기를 훑었는지만 기록하는 가짜 캐시. */
    class ProbeAssetCache final : public sw::IAssetCache
    {
    public:
        const utf8* getAssetKindName() const override { return "ProbeKind"; }
        bool        isCached( sw::string_view ) const override { return _bCleared == false; }
        void        reload( sw::string_view, sw::IRHIDevice* pDevice ) override
        {
            ++_reloadCount;
            _pLastDevice = pDevice;
        }
        size_t getCachedCount() const override { return _bCleared ? 0u : 1u; }
        void   clear() override { _bCleared = true; }

        bool            _bCleared{ false };
        uint32          _reloadCount{ 0 };
        sw::IRHIDevice* _pLastDevice{ nullptr };
    };

    /** @brief 내장 머티리얼 캐시와 같은 종류 이름을 대는 가짜 캐시(모듈이 같은 이름으로 하나 더 올리는 실수). */
    class ImpostorMaterialCache final : public sw::IAssetCache
    {
    public:
        const utf8* getAssetKindName() const override { return "Material"; }
        bool        isCached( sw::string_view ) const override { return false; }
        void        reload( sw::string_view, sw::IRHIDevice* ) override {}
        size_t      getCachedCount() const override { return 0u; }
        void        clear() override {}
    };
} // namespace

/**
 * @brief [AssetCacheRegistryTest] 내장 캐시 넷은 등록부를 통해 보인다
 * @details 종료 · 진단 · 핫 리로드는 이름으로 캐시를 적지 않고 등록부를 훑는다 — 등록부에 빠진 캐시는 그 셋 모두에서 빠진다.
 */
SW_TEST_CASE( AssetCacheRegistryTest, BuiltInCachesAreReachableThroughTheRegistry )
{
    sw::ResourceManager resources;

    SW_ASSERT_EQUAL( size_t( 4 ), resources.getAllAssetCache().size() );
    SW_EXPECT_NOT_NULL( resources.findAssetCache( "Material" ) );
    SW_EXPECT_NOT_NULL( resources.findAssetCache( "Texture" ) );
    SW_EXPECT_NOT_NULL( resources.findAssetCache( "Prefab" ) );
    SW_EXPECT_NOT_NULL( resources.findAssetCache( "SpriteClip" ) );
    SW_EXPECT_NULL( resources.findAssetCache( "NoSuchKind" ) );
    SW_EXPECT_NULL( resources.findAssetCache( "" ) );

    // 찾은 것이 실제로 그 캐시다 — 이름만 맞고 다른 것을 주면 진단이 거짓말을 한다.
    SW_EXPECT_TRUE( resources.findAssetCache( "Material" ) == static_cast<sw::IAssetCache*>( &resources.getMaterialManager() ) );
    SW_EXPECT_TRUE( resources.findAssetCache( "Texture" ) == static_cast<sw::IAssetCache*>( &resources.getTextureManager() ) );
    SW_EXPECT_TRUE( resources.findAssetCache( "Prefab" ) == static_cast<sw::IAssetCache*>( &resources.getPrefabManager() ) );
}

/**
 * @brief [AssetCacheRegistryTest] 같은 캐시를 두 번 올려도 한 번만 들어간다
 * @details 두 번 들어가면 비우기·진단이 그 캐시만 두 번 훑는다. 모듈이 리로드될 때 자기 캐시를
 *          다시 등록하는 것은 정상 경로라 여기서 막는다.
 */
SW_TEST_CASE( AssetCacheRegistryTest, RegisterIgnoresNullAndDuplicates )
{
    sw::ResourceManager resources;
    ProbeAssetCache     probe;

    const size_t builtInCount = resources.getAllAssetCache().size();

    resources.registerAssetCache( nullptr );
    SW_EXPECT_EQUAL( builtInCount, resources.getAllAssetCache().size() );

    resources.registerAssetCache( &probe );
    resources.registerAssetCache( &probe );
    SW_EXPECT_EQUAL( builtInCount + 1, resources.getAllAssetCache().size() );
    SW_EXPECT_TRUE( resources.findAssetCache( "ProbeKind" ) == static_cast<sw::IAssetCache*>( &probe ) );
}

/**
 * @brief [AssetCacheRegistryTest] 같은 종류 이름의 둘째 캐시는 거절하고 알린다 — 이름으로 찾는 쪽은 늘 먼저 것을 받는다
 * @details 공통 등록 목록(`RegistrationList`)이 이름 중복을 거절한다. 둘 다 오르면 `findAssetCache( "Material" )` 이 등록 순서의 첫 것을 조용히
 *          돌려주고, 에셋 핫 리로드는 그 이름으로 캐시를 찾으므로 나중 것은 다시 읽히지도 비워지지도 않는 캐시가 된다.
 */
SW_TEST_CASE( AssetCacheRegistryTest, SecondCacheWithTheSameKindNameIsRejected )
{
    sw::ResourceManager   resources;
    ImpostorMaterialCache impostor;
    const size_t          builtInCount = resources.getAllAssetCache().size();

    {
        test::ScopedDefensiveTestLog expected( "a second asset cache claims the kind name Material" );
        resources.registerAssetCache( &impostor );
    }
    SW_EXPECT_EQUAL( builtInCount, resources.getAllAssetCache().size() );
    SW_EXPECT_TRUE( resources.findAssetCache( "Material" ) == static_cast<sw::IAssetCache*>( &resources.getMaterialManager() ) );
    resources.unregisterAssetCache( &impostor ); // 올라 있지 않아도 조용하다
}

/**
 * @brief [AssetCacheRegistryTest] 비우기는 **등록된 것을 전부** 지나간다
 * @details 종료·재초기화가 이것을 쓴다. 손으로 적던 시절 프리팹이 빠져 있었던 자리라, 가짜 캐시
 *          하나를 더 올려 "이름을 모르는 캐시도 지나가는가" 까지 본다.
 */
SW_TEST_CASE( AssetCacheRegistryTest, ClearAssetCachesReachesEveryRegisteredCache )
{
    sw::ResourceManager resources;
    ProbeAssetCache     probe;
    resources.registerAssetCache( &probe );

    SW_ASSERT_FALSE( probe._bCleared );
    resources.clearAssetCaches();
    SW_EXPECT_TRUE_MSG( probe._bCleared, "등록했는데 비우기가 지나가지 않았다 — 종료가 그 캐시를 잊는다" );

    // 내장 캐시도 같은 호출로 비워진다(비어 있던 것이 비어 있는 것은 성립한다).
    for ( const sw::IAssetCache* pCache : resources.getAllAssetCache() )
    {
        SW_ASSERT_NOT_NULL( pCache );
        SW_EXPECT_EQUAL( size_t( 0 ), pCache->getCachedCount() );
    }
}

/**
 * @brief [AssetCacheRegistryTest] 내린 캐시만 빠지고 나머지는 그대로다
 * @details **모듈이 내려가기 전에 부르는 자리다.** 등록부는 포인터만 들고 있어, 모듈 DLL 이
 *          사라지면 그 포인터도 가상 함수 표도 같이 사라진다 — 남겨 두면 다음 비우기가 죽은
 *          코드로 뛴다("Statics die on hot reload" 와 같은 자리).
 */
SW_TEST_CASE( AssetCacheRegistryTest, UnregisterRemovesOnlyThatCache )
{
    sw::ResourceManager resources;
    ProbeAssetCache     probe;

    const size_t builtInCount = resources.getAllAssetCache().size();
    resources.registerAssetCache( &probe );
    SW_ASSERT_EQUAL( builtInCount + 1, resources.getAllAssetCache().size() );

    resources.unregisterAssetCache( &probe );
    SW_EXPECT_EQUAL( builtInCount, resources.getAllAssetCache().size() );
    SW_EXPECT_NULL( resources.findAssetCache( "ProbeKind" ) );

    // 내장 셋은 그대로 남아 있어야 한다 — 하나를 내렸다고 다른 것이 밀려나면 안 된다.
    SW_EXPECT_NOT_NULL( resources.findAssetCache( "Material" ) );
    SW_EXPECT_NOT_NULL( resources.findAssetCache( "Texture" ) );
    SW_EXPECT_NOT_NULL( resources.findAssetCache( "Prefab" ) );

    // 없는 것을 내려도 조용하다(모듈이 두 번 내릴 수 있다).
    resources.unregisterAssetCache( &probe );
    resources.unregisterAssetCache( nullptr );
    SW_EXPECT_EQUAL( builtInCount, resources.getAllAssetCache().size() );
}

/**
 * @brief [AssetCacheRegistryTest] 두고 간 캐시는 **이름으로** 경고한다
 * @details 조용히 지나가면 다음 실행에서 같은 일이 또 일어난다. 경고는 등록 시점에 복사해 둔
 *          이름을 쓴다 — 포인터는 그때 이미 죽었을 수 있어 역참조하지 않는다.
 */
SW_TEST_CASE( AssetCacheRegistryTest, LeftoverModuleCacheIsNamedInAWarning )
{
    sw::ResourceManager resources;
    ProbeAssetCache     probe;

    sw::string               warningText;
    const sw::DelegateHandle handle = sw::Logger::addGlobalListener(
        SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, [&warningText]( const sw::LogEntry& entry )
    {
        if ( entry._level == sw::LogLevel::Warning )
            warningText += entry._message;
    } ) );

    // 내장 셋만 있을 때는 조용하다 — "성공은 조용한가" 를 같이 본다.
    resources.warnAboutRemainingModuleCaches();
    SW_EXPECT_TRUE_MSG( warningText.empty(), "내장 캐시를 두고 간 것으로 잘못 보고했습니다" );

    resources.registerAssetCache( &probe );
    resources.warnAboutRemainingModuleCaches();
    sw::Logger::removeGlobalListener( handle );

    SW_EXPECT_TRUE_MSG( warningText.find( "ProbeKind" ) != sw::string::npos,
                        "두고 간 캐시를 이름으로 말하지 않았습니다" );
}

#if !defined( SW_SHIPPING )
/**
 * @brief [AssetCacheRegistryTest] 모듈 이미지를 내리기 전의 정리(`engine::releaseModuleCode`)는 그 이미지의 캐시를 등록부에서 내린다
 * @details 모듈이 `unregisterAssetCache` 를 빠뜨린 채 내려가면 등록부에 vtable 이 사라진 포인터가 남고, 다음 비우기 · 종료가 내려간 코드로
 *          뛴다. 여기서는 가짜 캐시의 vtable 한 바이트를 "모듈 이미지" 로 삼는다 — 그 범위 밖인 내장 캐시는 그대로 남아야 한다.
 */
SW_TEST_CASE( AssetCacheRegistryTest, ReleaseModuleCodeDropsCachesOfThatImage )
{
    SW_TEST_DEFENSIVE_SCOPE( "releaseModuleCode warns about a cache the module left registered" );
    sw::ResourceManager resources;
    ProbeAssetCache     probe;
    resources.registerAssetCache( &probe );
    const size_t registeredCount = resources.getAllAssetCache().size();

    const uint8* pVtable = static_cast<const uint8*>( sw::IModuleCodeHolder::findVtableAddress( static_cast<const sw::IAssetCache*>( &probe ) ) );
    SW_ASSERT_NOT_NULL( pVtable );
    test::ScopedLogCollector logCollector;
    SW_EXPECT_TRUE( sw::engine::releaseModuleCode( "ProbeModule", pVtable, pVtable + 1 ) >= 1u );

    SW_EXPECT_NULL( resources.findAssetCache( "ProbeKind" ) );
    SW_EXPECT_EQUAL( registeredCount - 1, resources.getAllAssetCache().size() );
    SW_EXPECT_NOT_NULL( resources.findAssetCache( "Material" ) );
    SW_EXPECT_FALSE_MSG( probe._bCleared, "등록부는 내리기만 한다 — 캐시는 그것을 만든 모듈이 지운다" );
    SW_EXPECT_TRUE_MSG( logCollector.countContaining( "asset caches" ) >= 1u, logCollector.joined().c_str() );
}
#endif
