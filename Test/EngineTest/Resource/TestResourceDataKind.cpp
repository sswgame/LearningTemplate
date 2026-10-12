// 데이터 파일 종류 레지스트리(카탈로그 계약) — 경로 규칙(접두 · 포함 · 접미, 대소문자 무시), 등록 · 중복 · 해제, 그리고 기반 · 키트 모듈이 자기 카탈로그 종류를 올렸는지.
#include "pch.h"

#include "Engine/Resource/ResourceDataKind.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    [[nodiscard]] bool loadNothing( const string& resourceID )
    {
        (void)resourceID;
        return true;
    }
} // namespace

SW_TEST_CASE( ResourceDataKindTest, MatchesAppliesEveryRuleAndIgnoresCase )
{
    const ResourceDataKind kind{ "test.items", "game/", "/data/", "/items.xml", &loadNothing };
    SW_EXPECT_TRUE( kind.matches( "game/farm/data/items.xml" ) );
    SW_EXPECT_TRUE( kind.matches( "GAME/Farm/Data/Items.XML" ) );
    SW_EXPECT_FALSE( kind.matches( "engine/farm/data/items.xml" ) ); // 접두
    SW_EXPECT_FALSE( kind.matches( "game/farm/items.xml" ) );        // 포함
    SW_EXPECT_FALSE( kind.matches( "game/farm/data/crops.xml" ) );   // 접미

    const ResourceDataKind suffixOnly{ "test.cameras", nullptr, nullptr, ".cameras.xml", &loadNothing };
    SW_EXPECT_TRUE( suffixOnly.matches( "engine/cameras/default.cameras.xml" ) );

    const ResourceDataKind noRule{ "test.none", nullptr, nullptr, nullptr, &loadNothing };
    SW_EXPECT_FALSE( noRule.matches( "game/anything.xml" ) ); // 규칙이 없으면 아무 파일에도 맞지 않는다
}

SW_TEST_CASE( ResourceDataKindTest, RegistryRejectsDuplicatesAndUnregisters )
{
    ResourceDataKindRegistry& registry = ResourceDataKindRegistry::get();
    const uint32              before   = registry.getCount();

    const ResourceDataKind first{ "test.registry.kind", "game/", nullptr, "/data/test_registry.xml", &loadNothing };
    const ResourceDataKind sameLabel{ "TEST.REGISTRY.KIND", nullptr, nullptr, ".other.xml", &loadNothing };
    const ResourceDataKind noLoader{ "test.registry.noloader", nullptr, nullptr, ".x.xml", nullptr };
    const ResourceDataKind noLabel{ "", nullptr, nullptr, ".y.xml", &loadNothing };

    SW_ASSERT_TRUE( registry.registerKind( &first ) );
    SW_EXPECT_EQUAL( registry.getCount(), before + 1 );
    {
        test::ScopedDefensiveTestLog expected( "a second registration of the same resource data kind" );
        SW_EXPECT_FALSE( registry.registerKind( &sameLabel ) ); // 이름은 대소문자를 가리지 않는다
    }
    SW_EXPECT_FALSE( registry.registerKind( &noLoader ) );
    SW_EXPECT_FALSE( registry.registerKind( &noLabel ) );
    SW_EXPECT_TRUE( registry.findByLabel( "Test.Registry.Kind" ) == &first );

    vector<const ResourceDataKind*> listMatch;
    registry.collectMatches( "game/farm/data/test_registry.xml", listMatch );
    SW_ASSERT_EQUAL( listMatch.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listMatch[0] == &first );
    registry.collectMatches( "game/farm/data/nothing.xml", listMatch );
    SW_EXPECT_TRUE( listMatch.empty() );

    registry.unregisterKind( &first );
    SW_EXPECT_EQUAL( registry.getCount(), before );
    SW_EXPECT_TRUE( registry.findByLabel( "test.registry.kind" ) == nullptr );
    registry.unregisterKind( &first ); // 두 번 빼도 할 일이 없다
}

SW_TEST_CASE( ResourceDataKindTest, StaticRegistrarRegistersAndUnregisters )
{
    ResourceDataKindRegistry& registry = ResourceDataKindRegistry::get();
    const uint32              before   = registry.getCount();
    {
        const ResourceDataKind          kind{ "test.registrar.kind", nullptr, nullptr, ".registrar.xml", &loadNothing };
        const ResourceDataKindRegistrar registrar{ &kind };
        SW_EXPECT_TRUE( registrar._bRegistered );
        SW_EXPECT_EQUAL( registry.getCount(), before + 1 );
    }
    SW_EXPECT_EQUAL( registry.getCount(), before ); // 모듈을 내릴 때처럼 등록자가 사라지면 종류도 빠진다
}

/**
 * @brief [ResourceDataKindTest] 기반 · 키트 모듈이 자기 카탈로그 종류를 올렸다
 * @details Shipping 은 모듈을 통째로 링크하므로(`sw_linkWholeArchive`) 등록 줄만 있는 번역 단위도 남는다. 이 테스트는 그 가정이 깨졌을 때(종류가 조용히 사라져
 *          `ResourceDataSchemaTest` 가 데이터를 안 읽게 되는 경우) 소리 내어 지게 한다.
 */
SW_TEST_CASE( ResourceDataKindTest, ModulesRegisterTheirCatalogKinds )
{
    const ResourceDataKindRegistry& registry   = ResourceDataKindRegistry::get();
    const utf8* const               arrLabel[] = { "items", "abilities", "quests", "weapons", "appearance", "gamesettings", "schedules", "camerapresets" };
    for ( const utf8* pLabel : arrLabel )
    {
        const ResourceDataKind* pKind = registry.findByLabel( pLabel );
        SW_EXPECT_TRUE_MSG( pKind != nullptr, pLabel );
    }
    const ResourceDataKind* pItems = registry.findByLabel( "items" );
    SW_ASSERT_TRUE( pItems != nullptr );
    SW_EXPECT_TRUE( pItems->matches( "game/shooter3d/data/items.xml" ) );
    SW_EXPECT_FALSE( pItems->matches( "game/shooter3d/data/weapons.xml" ) );
}
