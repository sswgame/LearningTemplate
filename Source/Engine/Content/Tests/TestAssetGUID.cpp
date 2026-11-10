#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetGUID.h"

using namespace sw;

namespace
{
    constexpr string_view kSampleText = "0123456789abcdef0011223344556677";
} // namespace

SW_TEST_CASE( AssetGUIDTest, TextRoundTripsAndCreateGivesDistinctValues )
{
    const optional<AssetGUID> parsed = AssetGUID::tryParse( kSampleText );
    SW_REQUIRE_TRUE( parsed.has_value() );
    SW_EXPECT_EQUAL( string( kSampleText ), parsed->toString() );

    const AssetGUID first  = AssetGUID::create();
    const AssetGUID second = AssetGUID::create();
    SW_EXPECT_TRUE( first != second );
    const optional<AssetGUID> reparsed = AssetGUID::tryParse( first.toString() );
    SW_REQUIRE_TRUE( reparsed.has_value() );
    SW_EXPECT_TRUE( *reparsed == first );
    SW_EXPECT_EQUAL( first.computeHash(), reparsed->computeHash() );
}

SW_TEST_CASE( AssetGUIDTest, RejectsOtherSpellingsAndTheZeroValue )
{
    SW_EXPECT_FALSE( AssetGUID::tryParse( "0123456789ABCDEF0011223344556677" ).has_value() );     // 대문자
    SW_EXPECT_FALSE( AssetGUID::tryParse( "01234567-89ab-cdef-0011-223344556677" ).has_value() ); // 하이픈
    SW_EXPECT_FALSE( AssetGUID::tryParse( "0123456789abcdef001122334455667" ).has_value() );      // 짧다
    SW_EXPECT_FALSE( AssetGUID::tryParse( "0123456789abcdef0011223344556677a" ).has_value() );    // 길다
    SW_EXPECT_FALSE( AssetGUID::tryParse( "0123456789abcdeg0011223344556677" ).has_value() );     // 16진 아님
    SW_EXPECT_FALSE( AssetGUID::tryParse( "00000000000000000000000000000000" ).has_value() );     // 빈 값
    SW_EXPECT_FALSE( AssetGUID::tryParse( "" ).has_value() );
}

SW_TEST_CASE( AssetGUIDTest, HeaderGoesRightAfterTheDeclarationAndReadsBack )
{
    const AssetGUID guid = *AssetGUID::tryParse( kSampleText );

    const string withDeclaration = AssetGUIDHeader::write( "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!-- note -->\n<Root/>\n", guid );
    SW_EXPECT_EQUAL( string( "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<?asset guid=\"0123456789abcdef0011223344556677\"?>\n<!-- note -->\n<Root/>\n" ), withDeclaration );
    const AssetGUIDHeader read = AssetGUIDHeader::read( withDeclaration );
    SW_EXPECT_TRUE( read._state == AssetGUIDHeaderState::Found );
    SW_REQUIRE_TRUE( read._guid.has_value() );
    SW_EXPECT_TRUE( *read._guid == guid );

    const string bare = AssetGUIDHeader::write( "<Root/>\r\n", guid );
    SW_EXPECT_EQUAL( string( "<?asset guid=\"0123456789abcdef0011223344556677\"?>\r\n<Root/>\r\n" ), bare );

    // 다시 쓰면 그 자리를 바꾼다(줄이 늘지 않는다)
    const AssetGUID other     = AssetGUID::create();
    const string    rewritten = AssetGUIDHeader::write( withDeclaration, other );
    SW_EXPECT_EQUAL( withDeclaration.size(), rewritten.size() );
    SW_EXPECT_TRUE( *AssetGUIDHeader::read( rewritten )._guid == other );
}

SW_TEST_CASE( AssetGUIDTest, HeaderStatesSeparateMissingFromBroken )
{
    SW_EXPECT_TRUE( AssetGUIDHeader::read( "<?xml version=\"1.0\"?>\n<Root/>" )._state == AssetGUIDHeaderState::Missing );
    SW_EXPECT_TRUE( AssetGUIDHeader::read( "<?asset guid=\"nope\"?>\n<Root/>" )._state == AssetGUIDHeaderState::Invalid );
    SW_EXPECT_TRUE( AssetGUIDHeader::read( "<?asset guid=\"0123456789abcdef0011223344556677\" extra=\"1\"?>\n<Root/>" )._state == AssetGUIDHeaderState::Invalid );
    // 머리 자리(맨 앞) 밖에 있는 것 · 둘인 것은 고르지 않는다
    SW_EXPECT_TRUE( AssetGUIDHeader::read( "<Root/>\n<?asset guid=\"0123456789abcdef0011223344556677\"?>" )._state == AssetGUIDHeaderState::Invalid );
    SW_EXPECT_TRUE( AssetGUIDHeader::read( "<?asset guid=\"0123456789abcdef0011223344556677\"?>\n<?asset guid=\"0123456789abcdef0011223344556677\"?>\n<Root/>" )._state ==
                    AssetGUIDHeaderState::Invalid );
    SW_EXPECT_TRUE( AssetGUIDHeader::read( "<?xml version=\"1.0\"\n<Root/>" )._state == AssetGUIDHeaderState::Invalid );
}
