#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/JSON/JSONWriter.h"

// JSONDocumentTest — 네트워크 · 데이터 파일의 JSON 입구: 형 읽기(정수 범위 · 없는 멤버는 폴백 없이 오류), 키 순서 보존, 쓰기기와의 왕복,
// 깨진 글과 아주 깊은 중첩(믿을 수 없는 입력이 스택을 다 쓰는 모양), 잘못된 UTF-8 쓰기(예외 대신 오류 값)(nogpu).

/**
 * @brief [JSONDocumentTest] 형 읽기는 맞는 칸만 읽고, 범위 밖 정수 · 다른 형 · 없는 필수 멤버는 오류와 함께 비운다
 */
SW_TEST_CASE( JSONDocumentTest, TypedReadsRefuseWrongValues )
{
    sw::JSONDocument document;
    SW_REQUIRE_TRUE( document.parse( R"({"count": 7, "big": 3000000000, "ratio": 0.5, "name": "x", "flag": true})" ) );
    const sw::JSONValue root = document.getRoot();
    SW_EXPECT_EQUAL( 7, root.readRequired<int32>( "count" ).value_or( -1 ) );
    SW_EXPECT_EQUAL( int64( 3000000000ll ), root.readRequired<int64>( "big" ).value_or( -1 ) );
    SW_EXPECT_EQUAL( sw::string( "x" ), root.readRequired<sw::string>( "name" ).value_or( "" ) );
    SW_EXPECT_TRUE( root.readRequired<bool>( "flag" ).value_or( false ) );

    SW_EXPECT_LOG( "JSON.WrongType" );
    SW_EXPECT_FALSE( root.readRequired<int32>( "big" ).has_value() );   // int32 범위 밖
    SW_EXPECT_FALSE( root.readRequired<int32>( "ratio" ).has_value() ); // 정수 칸의 실수
    SW_EXPECT_FALSE( root.readRequired<sw::string>( "count" ).has_value() );
    SW_EXPECT_LOG( "JSON.MissingMember" );
    SW_EXPECT_FALSE( root.readRequired<int32>( "absent" ).has_value() );

    sw::optional<int32> maybe{ 5 };
    SW_EXPECT_TRUE( root.readOptional( "absent", maybe ) ); // 없으면 비우고 true
    SW_EXPECT_FALSE( maybe.has_value() );
}

/**
 * @brief [JSONDocumentTest] 키 순서를 지키고, 쓰기기로 쓴 글을 다시 읽으면 같은 값이다
 */
SW_TEST_CASE( JSONDocumentTest, KeepsOrderAndRoundTripsTheWriter )
{
    sw::JSONWriter writer;
    writer.beginObject();
    writer.key( "zeta" );
    writer.valueInt( -3 );
    writer.key( "alpha" );
    writer.valueString( "quote\" and \\ and \n" );
    writer.key( "list" );
    writer.beginArray();
    writer.valueFloat( 1.25 );
    writer.valueNull();
    writer.endArray();
    writer.endObject();
    const sw::optional<sw::JSONText> text = writer.finish();
    SW_REQUIRE_TRUE( text.has_value() );

    sw::JSONDocument document;
    SW_REQUIRE_TRUE( document.parse( text->view() ) );
    const sw::vector<sw::string> listName = document.getRoot().getMemberNames();
    SW_REQUIRE_EQUAL( size_t( 3 ), listName.size() );
    SW_EXPECT_EQUAL( sw::string( "zeta" ), listName[0] );
    SW_EXPECT_EQUAL( sw::string( "quote\" and \\ and \n" ), document.getRoot().get( "alpha" ).asString() );
    SW_EXPECT_TRUE( document.getRoot().get( "list" ).at( 1 ).isNull() );
    SW_EXPECT_FALSE( document.getRoot().get( "list" ).at( 2 ).isValid() ); // 범위 밖은 무효 핸들
}

/**
 * @brief [JSONDocumentTest] 깨진 글은 거절하고, 아주 깊은 중첩(10 만 겹)도 프로세스를 죽이지 않는다
 */
SW_TEST_CASE( JSONDocumentTest, SurvivesMalformedAndDeepInput )
{
    sw::JSONDocument document;
    SW_EXPECT_FALSE( document.tryParse( "{\"a\": }" ) );
    SW_EXPECT_FALSE( document.tryParse( "[1, 2" ) );
    SW_EXPECT_FALSE( document.tryParse( "{\"a\": 1} trailing" ) );

    constexpr size_t kDepth = 100000;
    sw::string       deep( kDepth, '[' );
    deep.append( kDepth, ']' );
    (void)document.tryParse( deep ); // 받든 거절하든 — 스택을 다 써 죽지만 않으면 된다
    sw::JSONDocument after;
    SW_EXPECT_TRUE( after.parse( "{}" ) );
}

/**
 * @brief [JSONDocumentTest] 잘못된 UTF-8 이 든 문서는 dump 가 오류 값을 돌려주고(예외로 프로세스가 끝나지 않는다), 파일을 덮지 않으며, describe 는 U+FFFD 로 바꿔 쓴다
 */
SW_TEST_CASE( JSONDocumentTest, DumpSurvivesInvalidUTF8 )
{
    sw::JSONDocument    document;
    const sw::JSONValue root = document.makeObject();
    root.set( "ok" ).setString( "\xED\x95\x9C" ); // "한" - 올바른 3 바이트
    SW_EXPECT_TRUE( document.dump().has_value() );

    root.set( "name" ).setString( "cut\xED\x95" ); // 잘린 UTF-8 (setString 은 바이트를 검사하지 않는다)
    const sw::expected<sw::string> dumped = document.dump( 2 );
    SW_REQUIRE_FALSE( dumped.has_value() );
    SW_EXPECT_TRUE( dumped.error().kind() == sw::ErrorKind::InvalidArgument );
    SW_EXPECT_FALSE( root.get( "name" ).dump().has_value() ); // 서브트리도 같은 규칙

    const sw::string described = root.describe();
    SW_EXPECT_TRUE( described.find( "\xEF\xBF\xBD" ) != sw::string::npos ); // U+FFFD

    sw::JSONDocument    badKey;
    const sw::JSONValue badKeyRoot = badKey.makeObject();
    badKeyRoot.set( "\xFF" ).setInt( 1 ); // 키도 같은 규칙
    SW_EXPECT_FALSE( badKey.dump().has_value() );

    const sw::string path = sw::test::makeScratchPath( "InvalidUTF8.json" );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( path, "{\"keep\":1}" ) );
    SW_EXPECT_LOG( "JSONDocument.DumpFailed" );
    SW_EXPECT_FALSE( document.saveToFile( path, 2 ) );
    sw::string kept;
    SW_REQUIRE_TRUE( sw::FileUtil::readTextFile( path, kept ) );
    SW_EXPECT_EQUAL( sw::string( "{\"keep\":1}" ), kept ); // 있던 파일을 덮지 않는다
}
