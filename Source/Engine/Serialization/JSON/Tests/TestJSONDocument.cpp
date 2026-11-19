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

/**
 * @brief [JSONDocumentTest] 같은 객체의 중복 키와 256 단계를 넘는 중첩은 읽기 실패다
 * @details 예전에는 중복 키를 받아 나중 값으로 덮었고(손으로 고친 데이터의 두 번째 칸이 조용히 이긴다), 중첩 깊이에 한도가 없었다.
 */
SW_TEST_CASE( JSONDocumentTest, StrictReadRejectsDuplicateKeysAndDeepNesting )
{
    sw::JSONDocument document;
    SW_EXPECT_TRUE( document.tryParse( R"({"a":1,"b":{"a":2}})" ) ); // 다른 객체의 같은 이름은 중복이 아니다
    SW_EXPECT_FALSE( document.tryParse( R"({"a":1,"a":2})" ) );
    SW_EXPECT_FALSE( document.tryParse( R"([{"x":{"k":1,"k":1}}])" ) );

    sw::string deep;
    for ( int32 level = 0; level < 300; ++level )
    {
        deep += '[';
    }
    for ( int32 level = 0; level < 300; ++level )
    {
        deep += ']';
    }
    SW_EXPECT_FALSE( document.tryParse( deep ) );
    {
        SW_EXPECT_LOG( "JSON.ParseError" );
        SW_EXPECT_FALSE( document.parse( R"({"a":1,"a":2})", "dup.json" ) );
    }
}

/**
 * @brief [JSONDocumentTest] `setNull` 은 있던 값을 null 로 바꾼다 — 형이 바뀌고 이웃 멤버는 그대로다
 */
SW_TEST_CASE( JSONDocumentTest, SetNullReplacesTheValue )
{
    sw::JSONDocument document;
    SW_REQUIRE_TRUE( document.parse( R"({"name": "x", "count": 7})" ) );
    const sw::JSONValue name = document.getRoot().get( "name" );
    SW_REQUIRE_TRUE( name.isString() );
    name.setNull();
    SW_EXPECT_TRUE( document.getRoot().get( "name" ).isNull() );
    SW_EXPECT_TRUE( document.getRoot().get( "count" ).isNumber() );
}

/**
 * @brief [JSONDocumentTest] `extractStringField` 는 최상위 객체의 스칼라 필드만 글로 낸다 — 글은 내용, 수는 적힌 값이고, 객체 · 배열 · 없는 필드 · 객체가 아닌 글은 빈 글이다
 */
SW_TEST_CASE( JSONDocumentTest, ExtractStringFieldReadsTopLevelScalars )
{
    constexpr const utf8* kText = R"({"name": "knight", "count": 7, "nested": {"name": "inner"}, "list": [1, 2]})";
    SW_EXPECT_EQUAL( sw::string( "knight" ), sw::JSONDocument::extractStringField( kText, "name" ) );
    SW_EXPECT_EQUAL( sw::string( "7" ), sw::JSONDocument::extractStringField( kText, "count" ) );
    SW_EXPECT_TRUE( sw::JSONDocument::extractStringField( kText, "nested" ).empty() );
    SW_EXPECT_TRUE( sw::JSONDocument::extractStringField( kText, "list" ).empty() );
    SW_EXPECT_TRUE( sw::JSONDocument::extractStringField( kText, "absent" ).empty() );
    SW_EXPECT_TRUE( sw::JSONDocument::extractStringField( "[1, 2]", "name" ).empty() ); // 최상위가 객체가 아니다
}

/**
 * @brief [JSONDocumentTest] `forEachObjectInArray` 는 이름 붙은 배열의 객체 원소만 원래 순번과 함께 넘긴다 — 객체가 아닌 원소는 건너뛰고, 배열이 없거나 배열이 아니면 부르지 않는다
 * @details 순번은 건너뛴 원소도 센 배열 안 자리다(id 가 없는 원소에 순번을 기본값으로 쓰는 에셋이 있다).
 */
SW_TEST_CASE( JSONDocumentTest, ForEachObjectInArraySkipsNonObjects )
{
    sw::JSONDocument document;
    SW_REQUIRE_TRUE( document.parse( R"({"nodes": [{"id": 1}, 5, "text", {"id": 2}], "scalar": 3})" ) );
    const sw::JSONValue root = document.getRoot();

    sw::vector<size_t> listIndex;
    sw::vector<int32>  listID;
    sw::forEachObjectInArray( root, "nodes", [&]( const sw::JSONValue& element, size_t index )
    {
        listIndex.push_back( index );
        listID.push_back( element.readRequired<int32>( "id" ).value_or( -1 ) );
    } );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listIndex.size() );
    SW_EXPECT_EQUAL( size_t{ 0 }, listIndex[0] );
    SW_EXPECT_EQUAL( size_t{ 3 }, listIndex[1] );
    SW_EXPECT_EQUAL( 1, listID[0] );
    SW_EXPECT_EQUAL( 2, listID[1] );

    uint32 callCount = 0;
    sw::forEachObjectInArray( root, "scalar", [&]( const sw::JSONValue&, size_t )
    {
        ++callCount;
    } );
    sw::forEachObjectInArray( root, "absent", [&]( const sw::JSONValue&, size_t )
    {
        ++callCount;
    } );
    SW_EXPECT_EQUAL( 0u, callCount );
}
