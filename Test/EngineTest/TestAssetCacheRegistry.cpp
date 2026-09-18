#include "pch.h"

#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/IAssetCache.h"
#include "Engine/Resource/ResourceManager.h"

#include "TestFramework/TestFramework.h"

// AssetCacheRegistryTest — 에셋 종류를 늘리는 자리. 전역을 건드리지 않는다(지역 ResourceManager).
//
// 예전에는 캐시 셋을 **이름으로** 아는 코드가 여럿이었고, 그래서 종료 경로가 프리팹 캐시를 빠뜨렸다.
// 지금은 등록부 하나를 훑는다 — 그 등록부에 내장 셋이 실제로 들어 있는지, 훑는 동작이 등록된 것을
// 빠짐없이 지나가는지가 이 스위트가 보는 것이다.

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
} // namespace

/**
 * @brief [AssetCacheRegistryTest] 내장 캐시 셋은 등록부를 통해 보인다
 * @details 종료·진단이 이름으로 셋을 적지 않으려면 **등록부가 정본**이어야 한다. 프리팹이 여기
 *          들어 있는지가 특히 중요하다 — 종료가 그것만 빠뜨리고 있었다.
 */
SW_TEST_CASE( AssetCacheRegistryTest, BuiltInCachesAreReachableThroughTheRegistry )
{
    sw::ResourceManager resources;

    SW_ASSERT_EQUAL( size_t( 3 ), resources.getAllAssetCache().size() );
    SW_EXPECT_NOT_NULL( resources.findAssetCache( "Material" ) );
    SW_EXPECT_NOT_NULL( resources.findAssetCache( "Texture" ) );
    SW_EXPECT_NOT_NULL( resources.findAssetCache( "Prefab" ) );
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
