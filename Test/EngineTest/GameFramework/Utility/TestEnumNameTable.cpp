// 열거자 이름 표 — 이름(철자 그대로) → 열거자, 열거자 → 이름, 범위 밖, 이름 목록 글. 그리고 표를 쓰는 카탈로그가 옛 철자를 그대로 읽는지.
#include "pch.h"

#include "Core/String/EnumNameTable.h"

#include "GameFramework/Base/Actor/AI/Director/AIDirectorProfile.h"
#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/HorrorCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/ConquestCatalog.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    enum class TestShade : uint8
    {
        Light = 0,
        Mid,
        Dark
    };

    constexpr const utf8*              kArrShadeName[] = { "light", "mid", "dark" };
    constexpr EnumNameTable<TestShade> kShadeTable{ kArrShadeName };
} // namespace

SW_TEST_CASE( EnumNameTableTest, ParsesExactNamesAndKeepsTheValueOnAMiss )
{
    TestShade shade = TestShade::Mid;
    SW_EXPECT_FALSE( kShadeTable.parse( "DARK", shade ) ); // 철자 그대로만 — 대소문자가 다르면 모르는 이름
    SW_EXPECT_TRUE( kShadeTable.parse( "dark", shade ) );
    SW_EXPECT_TRUE( shade == TestShade::Dark );
    SW_EXPECT_TRUE( kShadeTable.parse( "light", shade ) );
    SW_EXPECT_TRUE( shade == TestShade::Light );
    shade = TestShade::Mid;
    SW_EXPECT_FALSE( kShadeTable.parse( "darker", shade ) );
    SW_EXPECT_FALSE( kShadeTable.parse( "", shade ) );
    SW_EXPECT_TRUE( shade == TestShade::Mid ); // 못 찾으면 값은 그대로다 - 부르는 쪽의 기본값이 남는다
}

SW_TEST_CASE( EnumNameTableTest, ToStringAndJoinNamesFollowTheTable )
{
    SW_EXPECT_EQUAL( kShadeTable.getCount(), size_t( 3 ) );
    SW_EXPECT_EQUAL( string( kShadeTable.toString( TestShade::Mid ) ), string( "mid" ) );
    SW_EXPECT_EQUAL( string( kShadeTable.toString( static_cast<TestShade>( 9 ) ) ), string( "?" ) ); // 범위 밖
    SW_EXPECT_EQUAL( kShadeTable.joinNames(), string( "light, mid, dark" ) );
    SW_EXPECT_EQUAL( kShadeTable.joinNames( "|" ), string( "light|mid|dark" ) );
}

SW_TEST_CASE( EnumNameTableTest, CatalogParsersKeepTheirSpellings )
{
    HorrorItemKind kind = HorrorItemKind::None;
    SW_EXPECT_FALSE( parseHorrorItemKind( "saveitem", kind ) );
    SW_EXPECT_TRUE( parseHorrorItemKind( "SaveItem", kind ) );
    SW_EXPECT_TRUE( kind == HorrorItemKind::SaveItem );
    SW_EXPECT_TRUE( parseHorrorItemKind( "Document", kind ) );
    SW_EXPECT_TRUE( kind == HorrorItemKind::Document ); // 마지막 열거자도 읽힌다(표 개수가 열거자를 다 덮는다)
    SW_EXPECT_FALSE( parseHorrorItemKind( "Grenade", kind ) );
    SW_EXPECT_EQUAL( string( toString( HorrorItemKind::Battery ) ), string( "Battery" ) );

    ConquestTeam team = ConquestTeam::Neutral;
    SW_EXPECT_TRUE( parseConquestTeam( "Enemy", team ) );
    SW_EXPECT_TRUE( team == ConquestTeam::Enemy );
    SW_EXPECT_FALSE( parseConquestTeam( "Ally", team ) );

    SW_EXPECT_EQUAL( string( toString( AIDirectorSignalKind::Rate ) ), string( "rate" ) );
    SW_EXPECT_EQUAL( string( toString( AIDirectorPoolTrigger::Budget ) ), string( "budget" ) );
    SW_EXPECT_EQUAL( string( toString( AIDirectorPoolKind::Reward ) ), string( "reward" ) );
}
