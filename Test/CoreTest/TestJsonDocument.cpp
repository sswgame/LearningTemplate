#include "pch.h"

#include "Engine/Utility/Json/JsonDocument.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_Json — 파싱·탐색과 대소문자 무시 키
// ------------------------------------------------------------------------------
/**
 * @brief [Core_Json] 파싱·탐색과 대소문자 무시 키
 */

SW_TEST_CASE( Core_Json, ParseAndNavigateIgnoreCaseKeys )
{
	sw::JsonDocument doc;
	SW_EXPECT_TRUE( doc.parse( R"({"Name":"Demo","_score":12,"Items":[{"id":1},{"id":2}]})" ) );

	sw::JsonValue root = doc.root();
	SW_EXPECT_TRUE( root.isObject() );
	SW_EXPECT_EQUAL( sw::string( "Demo" ), root.get( "name" ).asString() );
	SW_EXPECT_EQUAL( 12, root.get( "_SCORE" ).asInt() );
	SW_EXPECT_TRUE( root.get( "missing" ).isValid() == false );

	sw::JsonValue items = root.get( "items" );
	SW_EXPECT_TRUE( items.isArray() );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( items.size() ) );
	SW_EXPECT_EQUAL( 1, items.at( 0 ).get( "ID" ).asInt() );
	SW_EXPECT_EQUAL( 2, items.at( 1 ).get( "id" ).asInt() );
}

/**
 * @brief [Core_Json] 대소문자 구분 키 옵트아웃
 */
SW_TEST_CASE( Core_Json, CaseSensitiveKeyOptOut )
{
	sw::JsonDocument doc;
	SW_EXPECT_TRUE( doc.parse( R"({"Child":"ok"})" ) );

	sw::JsonValue root = doc.root();
	SW_EXPECT_TRUE( root.get( "child", false ).isValid() == false );
	SW_EXPECT_TRUE( root.get( "Child", false ).isValid() );
	SW_EXPECT_EQUAL( sw::string( "ok" ), root.get( "Child", false ).asString() );
}

/**
 * @brief [Core_Json] 유니코드 이스케이프와 잘못된 JSON 거부
 */
SW_TEST_CASE( Core_Json, UnicodeEscapeAndRejectMalformed )
{
	sw::JsonDocument doc;
	SW_EXPECT_TRUE( doc.parse( R"({"Title":"A\u0020B","Nested":{"k":1}})" ) );
	SW_EXPECT_EQUAL( sw::string( "A B" ), doc.root().get( "Title" ).asString() );
	SW_EXPECT_TRUE( sw::JsonDocument::extractStringField( R"({"Title":"Hero"})", "title", true ) == sw::string( "Hero" ) );
	SW_EXPECT_TRUE( sw::JsonDocument::extractStringField( R"({"Title":"Hero"})", "title", false ).empty() );

	const sw::string raw	 = "line\n\t\"quote\"\\slash";
	const sw::string escaped = sw::JsonDocument::escapeString( raw );
	SW_EXPECT_TRUE( escaped.find( '\n' ) == sw::string::npos );
	SW_EXPECT_EQUAL( raw, sw::JsonDocument::unescapeString( escaped ) );

	{
		SW_TEST_DEFENSIVE_SCOPE( "Testing malformed JSON parse rejection" );
		sw::JsonDocument bad;
		SW_EXPECT_FALSE( bad.parse( R"({"not_a_pair","_id":1})" ) );
	}
}

/**
 * @brief [Core_Json] 쓰기 후 dump 라운드트립
 */
SW_TEST_CASE( Core_Json, WriteAndDumpRoundtrip )
{
	sw::JsonDocument doc;
	sw::JsonValue	 root = doc.makeObject();
	root.set( "name" ).setString( "Demo" );
	root.set( "count" ).setInt( 3 );
	sw::JsonValue arr = root.set( "items" );
	arr.setArray();
	arr.pushBack().setInt( 1 );
	arr.pushBack().setInt( 2 );

	sw::JsonDocument loaded;
	SW_EXPECT_TRUE( loaded.parse( doc.dump() ) );
	SW_EXPECT_EQUAL( sw::string( "Demo" ), loaded.root().get( "name" ).asString() );
	SW_EXPECT_EQUAL( 3, loaded.root().get( "count" ).asInt() );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( loaded.root().get( "items" ).size() ) );
}

/**
 * @brief [Core_Json] 불리언, 부동소수점 및 깊은 중첩 구조 파싱 검증
 */
SW_TEST_CASE( Core_Json, BooleanFloatAndDeepNestedObject )
{
	const utf8* jsonStr = R"({
		"bEnabled": true,
		"bPaused": false,
		"scale": 2.75,
		"nested": {
			"sub": {
				"array": [10.5, 20.25, 30.125]
			}
		}
	})";

	sw::JsonDocument doc;
	SW_EXPECT_TRUE( doc.parse( jsonStr ) );

	sw::JsonValue root = doc.root();
	SW_EXPECT_TRUE( root.get( "bEnabled" ).asBool() );
	SW_EXPECT_FALSE( root.get( "bPaused" ).asBool() );
	SW_EXPECT_NEAR_EQUAL( 2.75, root.get( "scale" ).asFloat(), 1e-4 );

	sw::JsonValue subArr = root.get( "nested" ).get( "sub" ).get( "array" );
	SW_EXPECT_TRUE( subArr.isArray() );
	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( subArr.size() ) );
	SW_EXPECT_NEAR_EQUAL( 10.5, subArr.at( 0 ).asFloat(), 1e-4 );
	SW_EXPECT_NEAR_EQUAL( 20.25, subArr.at( 1 ).asFloat(), 1e-4 );
	SW_EXPECT_NEAR_EQUAL( 30.125, subArr.at( 2 ).asFloat(), 1e-4 );
}
