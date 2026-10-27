// 데이터 파일 종류 레지스트리(카탈로그 계약) — 경로 규칙(접두 · 포함 · 접미, 대소문자 무시), 등록 · 중복 · 해제, 그리고 기반 · 키트 모듈이 자기 카탈로그 종류를 올렸는지.
#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Resource/ResourceDataKind.h"
#include "Engine/Resource/ResourceID.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    [[nodiscard]] bool loadNothing( const ResourceID& resourceID )
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
    ResourceDataKindRegistry& registry = engine::getSubsystem<ResourceDataKindRegistry>();
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

/**
 * @brief [ResourceDataKindTest] 정적 등록자는 링크 때 목록에만 달리고, 모듈을 올린 쪽이 걷어 올리고 모듈 이름으로 뺀다
 * @details 등록자를 만들기만 해서는 레지스트리가 변하지 않는다. 걷으면 만든 순서(= 목록의 뒤에서부터)대로 올라가고, 모듈 이름으로 빼면 그 모듈의 종류만 빠진다.
 */
SW_TEST_CASE( ResourceDataKindTest, StaticRegistrarsAreTakenByTheModuleLoaderAndRemovedByModuleName )
{
    ResourceDataKindRegistry&        registry   = engine::getSubsystem<ResourceDataKindRegistry>();
    const uint32                     before     = registry.getCount();
    ResourceDataKindRegistrar* const pSavedHead = ResourceDataKindRegistrar::getHead();
    ResourceDataKindRegistrar::getHead()        = nullptr; // 이 테스트가 만드는 등록자만 걷는다

    const ResourceDataKind          kindFirst{ "test.registrar.first", nullptr, nullptr, ".registrar-first.xml", &loadNothing };
    const ResourceDataKind          kindSecond{ "test.registrar.second", nullptr, nullptr, ".registrar-second.xml", &loadNothing };
    const ResourceDataKindRegistrar registrarFirst{ &kindFirst };
    const ResourceDataKindRegistrar registrarSecond{ &kindSecond };
    SW_EXPECT_TRUE( ResourceDataKindRegistrar::getHead() == &registrarSecond ); // 나중에 만든 것이 앞이다
    SW_EXPECT_EQUAL( registry.getCount(), before );                             // 만들기만으로는 올라가지 않는다

    ResourceDataKindRegistrar* const pTakenHead = ResourceDataKindRegistrar::getHead();
    ResourceDataKindRegistrar::getHead()        = pSavedHead; // 걷었으니 되돌린다(스택 등록자는 이 함수를 벗어나기 전에 목록에서 빠져 있어야 한다)

    SW_EXPECT_EQUAL( registry.registerModuleKinds( "TestKindModule", pTakenHead ), 2u );
    SW_EXPECT_EQUAL( registry.getCount(), before + 2 );
    vector<const ResourceDataKind*> listKind;
    registry.collectKinds( listKind );
    int32 firstIndex  = -1;
    int32 secondIndex = -1;
    for ( size_t index = 0; index < listKind.size(); ++index )
    {
        if ( listKind[index] == &kindFirst )
            firstIndex = static_cast<int32>( index );
        if ( listKind[index] == &kindSecond )
            secondIndex = static_cast<int32>( index );
    }
    SW_EXPECT_TRUE( firstIndex >= 0 && firstIndex < secondIndex ); // 올린 순서는 만든 순서다

    SW_EXPECT_EQUAL( registry.unregisterModuleKinds( "TestKindModule" ), 2u );
    SW_EXPECT_EQUAL( registry.getCount(), before ); // 모듈을 내릴 때처럼 그 모듈 이름의 종류가 빠진다
    SW_EXPECT_EQUAL( registry.unregisterModuleKinds( "TestKindModule" ), 0u );
}

/**
 * @brief [ResourceDataKindTest] 기반 · 키트 모듈이 자기 카탈로그 종류를 올렸다
 * @details Shipping 은 모듈을 통째로 링크하므로(`sw_linkWholeArchive`) 등록 줄만 있는 번역 단위도 남는다. 이 테스트는 그 가정이 깨졌을 때(종류가 조용히 사라져
 *          `ResourceDataSchemaTest` 가 데이터를 안 읽게 되는 경우) 소리 내어 지게 한다.
 */
SW_TEST_CASE( ResourceDataKindTest, ModulesRegisterTheirCatalogKinds )
{
    const ResourceDataKindRegistry& registry   = engine::getSubsystem<ResourceDataKindRegistry>();
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
