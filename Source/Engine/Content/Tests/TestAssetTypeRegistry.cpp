#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"

using namespace sw;

namespace
{
    /** @brief 로더가 불린 횟수를 세는 종류입니다. */
    struct AssetTypeTestAsset
    {
        static inline int32 s_loadCount = 0;

        [[nodiscard]] bool loadFromAsset( const AssetID& assetID )
        {
            ++s_loadCount;
            return assetID.view() == "game/x/data/good.xml";
        }
    };

    struct AssetTypeRegistryTestUtil
    {
        /** @brief 규칙을 그대로 잇는 대표 경로입니다. 규칙이 맞는 가장 짧은 id 입니다. */
        static string makeSampleID( const AssetPathRule& rule )
        {
            string sample{ rule._prefix };
            sample += rule._contains;
            sample += rule._suffix;
            return sample;
        }
    };
} // namespace

/**
 * @brief [AssetTypeRegistryTest] 경로 규칙은 적은 칸을 모두 보고(대소문자 무시), 규칙이 없으면 어떤 파일에도 맞지 않는지 검증
 */
SW_TEST_CASE( AssetTypeRegistryTest, PathRuleMatchesAllWrittenParts )
{
    const AssetPathRule rule{ "game/", "/data/", ".xml" };
    SW_EXPECT_TRUE( rule.matches( "game/x/data/items.xml" ) );
    SW_EXPECT_TRUE( rule.matches( "GAME/x/DATA/items.XML" ) );
    SW_EXPECT_FALSE( rule.matches( "common/x/data/items.xml" ) );
    SW_EXPECT_FALSE( rule.matches( "game/x/items.xml" ) );
    SW_EXPECT_FALSE( rule.matches( "game/x/data/items.json" ) );

    const AssetPathRule noRule{};
    SW_EXPECT_TRUE( noRule.isEmpty() );
    SW_EXPECT_FALSE( noRule.matches( "game/x/data/items.xml" ) );
}

/**
 * @brief [AssetTypeRegistryTest] 등록 줄 하나로 경로 · 이름 찾기가 되고 검사가 진짜 로더를 부르며, 내리면 사라지는지 검증
 */
SW_TEST_CASE( AssetTypeRegistryTest, RegistrarCarriesRuleAndLoader )
{
    AssetTypeTestAsset::s_loadCount = 0;
    {
        const AssetTypeRegistrar registrar{ AssetTypeDef::makeDataFile( "assettypetest", AssetPathRule{ {}, {}, ".assettypetest.xml" }, AssetTypeLoader::of<AssetTypeTestAsset>( MemoryTag::Game ) ) };

        const AssetTypeDef* pType = AssetTypeRegistry::findType( "AssetTypeTest" ); // 이름은 대소문자를 가리지 않는다
        SW_REQUIRE_TRUE( pType != nullptr );
        SW_EXPECT_TRUE( pType->_loader._memoryTag == MemoryTag::Game );

        vector<const AssetTypeDef*> listMatch;
        AssetTypeRegistry::collectPathMatches( "game/x/a.assettypetest.xml", listMatch );
        SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listMatch.size() );
        SW_EXPECT_TRUE( listMatch[0] == pType );

        SW_EXPECT_FALSE( AssetTypeRegistry::checkLoad( *pType, AssetID{ "game/x/data/bad.xml" } ) );
        SW_EXPECT_TRUE( AssetTypeRegistry::checkLoad( *pType, AssetID{ "game/x/data/good.xml" } ) );
        SW_EXPECT_EQUAL( 2, AssetTypeTestAsset::s_loadCount );
    }
    SW_EXPECT_TRUE( AssetTypeRegistry::findType( "assettypetest" ) == nullptr );
}

/**
 * @brief [AssetTypeRegistryTest] 로더 없는 종류의 검사는 실패이고, 올라온 데이터 종류는 모두 로더가 있으며 대표 경로가 자기 한 종류에만 맞는지 검증(한 파일은 정확히 한 종류)
 */
SW_TEST_CASE( AssetTypeRegistryTest, EveryDataKindHasALoaderAndOwnsItsPaths )
{
    const AssetTypeDef noLoader{ "noLoader" };
    SW_EXPECT_FALSE( AssetTypeRegistry::checkLoad( noLoader, AssetID{ "game/x.xml" } ) );

    vector<string> listProblem;
    AssetTypeRegistry::collectProblems( listProblem );
    SW_EXPECT_TRUE( listProblem.empty() );

    vector<const AssetTypeDef*> listMatch;
    for ( const AssetTypeDef* pType : AssetTypeRegistry::collectTypes() )
    {
        if ( pType->_pathRule.isEmpty() )
            continue;
        SW_EXPECT_TRUE( pType->_loader._pCheckLoad != nullptr );
        AssetTypeRegistry::collectPathMatches( AssetTypeRegistryTestUtil::makeSampleID( pType->_pathRule ), listMatch );
        SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), listMatch.size() );
    }
}
