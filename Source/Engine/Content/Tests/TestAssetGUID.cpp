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
    const optional<AssetGUID> parsedAgain = AssetGUID::tryParse( first.toString() );
    SW_REQUIRE_TRUE( parsedAgain.has_value() );
    SW_EXPECT_TRUE( *parsedAgain == first );
    SW_EXPECT_EQUAL( first.computeHash(), parsedAgain->computeHash() );
}

/**
 * @brief [AssetGUIDTest] 이름 기반 GUID 는 같은 이름(대소문자 무관)이면 같고, 이름이 다르면 다르며, 글 표기로 왕복한다 — 임포트 결과의 정체성이다
 */
SW_TEST_CASE( AssetGUIDTest, FromNameIsDeterministicAndDistinct )
{
    const AssetGUID first = AssetGUID::fromName( "common/models/kaykit/knight/knight.skeleton.xml" );
    SW_EXPECT_TRUE( first == AssetGUID::fromName( "common/models/kaykit/knight/knight.skeleton.xml" ) );
    SW_EXPECT_TRUE( first == AssetGUID::fromName( "Common/Models/KayKit/Knight/Knight.skeleton.xml" ) );
    SW_EXPECT_TRUE( first != AssetGUID::fromName( "common/models/kaykit/knight/knight.animset.xml" ) );
    SW_EXPECT_TRUE( first != AssetGUID::fromName( "common/models/kaykit/rogue/rogue.skeleton.xml" ) );
    const optional<AssetGUID> parsed = AssetGUID::tryParse( first.toString() );
    SW_REQUIRE_TRUE( parsed.has_value() );
    SW_EXPECT_TRUE( *parsed == first );
}

/**
 * @brief [AssetGUIDTest] 이름공간 + 이름의 GUID(임포트 산출물)는 원본 GUID 와 산출물 이름만 본다 — 값은 파이썬 도구(`asset_identity.py` 의 자체 검사)와 같은 글이다
 */
SW_TEST_CASE( AssetGUIDTest, OutputIdentityComesFromSourceAndOutputName )
{
    const AssetGUID source = *AssetGUID::tryParse( "0123456789abcdef0011223344556677" );
    const AssetGUID mesh   = AssetGUID::fromName( source, ".mesh" );
    SW_EXPECT_TRUE( mesh == AssetGUID::fromName( source, ".MESH" ) );
    SW_EXPECT_TRUE( mesh != AssetGUID::fromName( source, "/materials/mi_eyes.material" ) );
    SW_EXPECT_TRUE( mesh != AssetGUID::fromName( AssetGUID::create(), ".mesh" ) );
    SW_EXPECT_TRUE( mesh == AssetGUID::fromName( "0123456789abcdef0011223344556677.mesh" ) );
    SW_EXPECT_EQUAL( string( "28016f06802dfbf1a257d90c3347b2fb" ), mesh.toString() );
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

SW_TEST_CASE( AssetGUIDTest, CarryKeepsTheIdentityAcrossAWholeRewrite )
{
    const AssetGUID guid     = *AssetGUID::tryParse( kSampleText );
    const string    previous = AssetGUIDHeader::write( "<?xml version=\"1.0\"?>\n<Root a=\"1\"/>\n", guid );
    const string    saved    = AssetGUIDHeader::carryOver( previous, "<?xml version=\"1.0\"?>\n<Root a=\"2\"/>\n" ); // 파서가 다시 쓴 글에는 머리가 없다
    SW_EXPECT_EQUAL( string( "<?xml version=\"1.0\"?>\n<?asset guid=\"0123456789abcdef0011223344556677\"?>\n<Root a=\"2\"/>\n" ), saved );
    // 있던 글에 머리가 없으면(새 파일 · 정체성이 없는 글) 그대로, 새 글이 이미 머리를 들면(복사 · 내용 바꾸기가 정한 GUID) 그 머리가 이긴다
    SW_EXPECT_EQUAL( string( "<Root/>" ), AssetGUIDHeader::carryOver( "<Root/>", "<Root/>" ) );
    const AssetGUID other = AssetGUID::create();
    SW_EXPECT_TRUE( *AssetGUIDHeader::read( AssetGUIDHeader::carryOver( previous, AssetGUIDHeader::write( "<Root/>", other ) ) )._guid == other );
}

SW_TEST_CASE( AssetGUIDTest, HeaderStatesSeparateMissingFromBroken )
{
    SW_EXPECT_TRUE( AssetGUIDHeader::read( "<?xml version=\"1.0\"?>\n<Root/>" )._state == AssetGUIDHeaderState::Missing );
    SW_EXPECT_TRUE( AssetGUIDHeader::read( "<?asset guid=\"nope\"?>\n<Root/>" )._state == AssetGUIDHeaderState::Invalid );
    SW_EXPECT_TRUE( AssetGUIDHeader::read( "<?asset guid=\"0123456789abcdef0011223344556677\" extra=\"1\"?>\n<Root/>" )._state == AssetGUIDHeaderState::Invalid );
    // 머리는 맨 앞 자리의 것 하나다 — 그 뒤(주석이 적은 머리 모양 · 루트 뒤의 처리 지시)는 보지 않는다
    SW_EXPECT_TRUE( AssetGUIDHeader::read( "<Root/>\n<?asset guid=\"0123456789abcdef0011223344556677\"?>" )._state == AssetGUIDHeaderState::Missing );
    SW_EXPECT_TRUE( AssetGUIDHeader::read( "<?asset guid=\"0123456789abcdef0011223344556677\"?>\n<!-- <?asset guid?> -->\n<Root/>" )._state == AssetGUIDHeaderState::Found );
    SW_EXPECT_TRUE( AssetGUIDHeader::read( "<?xml version=\"1.0\"\n<Root/>" )._state == AssetGUIDHeaderState::Invalid );
}
