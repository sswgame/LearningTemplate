// FormatUpgrade — 판 올림 사슬: 한 판씩 순서대로 올리기, 빠진 단계 · 새 판 · 실패한 단계는 오류, 바이트 형식의 머리(nogpu).
#include "pch.h"

#include "Core/Serialization/ByteStream.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Serialization/Format/FormatDef.h"
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
        };
    } // namespace

    SW_ASSET_FORMAT_UPGRADE( formatUpgradeTest0, FormatUpgradeTestSteps::kKind, 0, 1, &FormatUpgradeTestSteps::renameSpeed );
    SW_ASSET_FORMAT_UPGRADE( formatUpgradeTest1, FormatUpgradeTestSteps::kKind, 1, 2, &FormatUpgradeTestSteps::addRunSpeed );
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
 * @brief [FormatUpgradeTest] 바이트 형식의 머리는 다른 표 · 새 판 · 옛 판 · 잘린 바이트를 같은 오류 하나로 거절하고, 판은 단계 수다
 */
SW_TEST_CASE( FormatUpgradeTest, HeadIsWrittenReadAndRefusedInOnePlace )
{
    constexpr sw::FormatDef kFormat{ "TFMT", 1 };
    sw::vector<uint8>       bytes;
    kFormat.write( bytes );
    const auto check = [&bytes]( const sw::FormatDef& format )
    {
        sw::ByteReader reader{ bytes.data(), bytes.size() };
        return format.check( reader );
    };
    SW_EXPECT_TRUE( check( kFormat ).has_value() );
    SW_EXPECT_FALSE( check( sw::FormatDef{ "TFMT", 0 } ).has_value() ); // 새 판
    SW_EXPECT_FALSE( check( sw::FormatDef{ "TFMT", 2 } ).has_value() ); // 옛 판
    SW_EXPECT_FALSE( check( sw::FormatDef{ "OTHR", 1 } ).has_value() );
    SW_EXPECT_LOG( "Format.HeadRefused" );
    sw::ByteReader cut{ bytes.data(), bytes.size() - 1 };
    SW_EXPECT_FALSE( kFormat.read( cut, "cut" ) );
    SW_EXPECT_LOG( "Format.HeadRefused" );
    SW_EXPECT_FALSE( kFormat.accept( 2, "newer" ) );
    SW_EXPECT_TRUE( kFormat.accept( 1, "current" ) );
}
