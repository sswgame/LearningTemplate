// FormatUpgrade — 판 올림 사슬: 한 판씩 순서대로 올리기, 빠진 단계 · 새 판 · 실패한 단계는 오류, 자리(에셋 XML · 세이브 바이트)마다 따로(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/FormatUpgrade.h"

namespace sw
{
    namespace
    {
        struct FormatUpgradeTestSteps
        {
            static constexpr const utf8* kKind = "FormatUpgradeTestKind";

            /** @brief v0 → v1: 옛 칸(`speed`)의 값을 새 칸(`walkSpeed`)으로 옮겨 적는다. 옛 칸이 없으면 고칠 수 없다. */
            static bool renameSpeed( FormatUpgradePayload& payload )
            {
                const utf8* pSpeed = payload._root.findAttribute( "speed" );
                if ( pSpeed == nullptr )
                    return false;
                const string value{ pSpeed };
                payload._root.appendAttribute( "walkSpeed", value.c_str() );
                return true;
            }

            /** @brief v1 → v2: 새 필수 칸을 데이터로 채운다. */
            static bool addRunSpeed( FormatUpgradePayload& payload )
            {
                payload._root.appendAttribute( "runSpeed", "6" );
                return true;
            }

            /** @brief 세이브 v1 → v2: 바이트 머리에 판 표지를 더한다. */
            static bool prefixMarker( FormatUpgradePayload& payload )
            {
                payload._pBytes->insert( payload._pBytes->begin(), uint8{ 0xAB } );
                return true;
            }
        };
    } // namespace

    SW_ASSET_FORMAT_UPGRADE( formatUpgradeTest0, FormatUpgradeTestSteps::kKind, 0, 1, &FormatUpgradeTestSteps::renameSpeed );
    SW_ASSET_FORMAT_UPGRADE( formatUpgradeTest1, FormatUpgradeTestSteps::kKind, 1, 2, &FormatUpgradeTestSteps::addRunSpeed );
    SW_SAVE_FORMAT_UPGRADE( formatUpgradeTestSave1, FormatUpgradeTestSteps::kKind, 1, 2, &FormatUpgradeTestSteps::prefixMarker );
} // namespace sw

/**
 * @brief [FormatUpgradeTest] 옛 판을 현재 판까지 한 판씩 순서대로 올린다 — 같은 판은 그대로다
 */
SW_TEST_CASE( FormatUpgradeTest, RaisesOneVersionAtATime )
{
    sw::XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( "<Walker speed=\"3\"/>" ) );
    sw::FormatUpgradePayload payload;
    payload._root = doc.getRoot( "Walker" );
    SW_REQUIRE_TRUE( sw::FormatUpgradeStep::upgrade( sw::FormatUpgradeDomain::AssetXML, "FormatUpgradeTestKind", 0, 2, payload ) );
    SW_EXPECT_EQUAL( sw::string( "3" ), sw::string( payload._root.findAttribute( "walkSpeed" ) ) );
    SW_EXPECT_EQUAL( sw::string( "6" ), sw::string( payload._root.findAttribute( "runSpeed" ) ) );
    SW_EXPECT_TRUE( sw::FormatUpgradeStep::upgrade( sw::FormatUpgradeDomain::AssetXML, "FormatUpgradeTestKind", 2, 2, payload ) );
    SW_EXPECT_EQUAL( uint32{ 2 }, sw::FormatUpgradeStep::countSteps( sw::FormatUpgradeDomain::AssetXML, "FormatUpgradeTestKind" ) );
}

/**
 * @brief [FormatUpgradeTest] 빠진 단계 · 새 판 · 실패한 단계는 오류다(폴백으로 옛 판을 읽지 않는다)
 */
SW_TEST_CASE( FormatUpgradeTest, MissingNewerAndFailingStepsAreErrors )
{
    sw::XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( "<Walker/>" ) );
    sw::FormatUpgradePayload payload;
    payload._root = doc.getRoot( "Walker" );
    SW_EXPECT_LOG( "FormatUpgrade.StepMissing" );
    SW_EXPECT_FALSE( sw::FormatUpgradeStep::upgrade( sw::FormatUpgradeDomain::AssetXML, "FormatUpgradeTestKind", 2, 3, payload ) );
    SW_EXPECT_LOG( "FormatUpgrade.NewerThanSupported" );
    SW_EXPECT_FALSE( sw::FormatUpgradeStep::upgrade( sw::FormatUpgradeDomain::AssetXML, "FormatUpgradeTestKind", 3, 2, payload ) );
    SW_EXPECT_LOG( "FormatUpgrade.StepFailed" );
    SW_EXPECT_FALSE( sw::FormatUpgradeStep::upgrade( sw::FormatUpgradeDomain::AssetXML, "FormatUpgradeTestKind", 0, 1, payload ) ); // `speed` 가 없다
}

/**
 * @brief [FormatUpgradeTest] 자리마다 따로 센다 — 같은 종류 이름이라도 세이브 바이트 단계는 에셋 XML 단계가 아니다
 */
SW_TEST_CASE( FormatUpgradeTest, DomainsAreSeparate )
{
    sw::vector<uint8>        bytes{ 1, 2 };
    sw::FormatUpgradePayload payload;
    payload._pBytes = &bytes;
    SW_REQUIRE_TRUE( sw::FormatUpgradeStep::upgrade( sw::FormatUpgradeDomain::SaveBytes, "FormatUpgradeTestKind", 1, 2, payload ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, bytes.size() );
    SW_EXPECT_EQUAL( uint8{ 0xAB }, bytes[0] );
    SW_EXPECT_LOG( "FormatUpgrade.StepMissing" );
    SW_EXPECT_FALSE( sw::FormatUpgradeStep::upgrade( sw::FormatUpgradeDomain::SaveBytes, "FormatUpgradeTestKind", 0, 1, payload ) );
}
