#include "pch.h"

#include "Engine/Utility/Json/JsonDocument.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_Json — 파싱·탐색과 대소문자 무시 키
// ------------------------------------------------------------------------------
/**
 * @brief [JsonDocumentTest] 파싱·탐색과 대소문자 무시 키
 */

SW_TEST_CASE( JsonDocumentTest, ParseAndNavigateIgnoreCaseKeys )
{
    sw::JsonDocument doc;
    SW_EXPECT_TRUE( doc.parse( R"({"Name":"Demo","_score":12,"Items":[{"id":1},{"id":2}]})" ) );

    sw::JsonValue root = doc.getRoot();
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
 * @brief [JsonDocumentTest] 대소문자 구분 키 옵트아웃
 */
SW_TEST_CASE( JsonDocumentTest, CaseSensitiveKeyOptOut )
{
    sw::JsonDocument doc;
    SW_EXPECT_TRUE( doc.parse( R"({"Child":"ok"})" ) );

    sw::JsonValue root = doc.getRoot();
    SW_EXPECT_TRUE( root.get( "child", false ).isValid() == false );
    SW_EXPECT_TRUE( root.get( "Child", false ).isValid() );
    SW_EXPECT_EQUAL( sw::string( "ok" ), root.get( "Child", false ).asString() );
}

/**
 * @brief [JsonDocumentTest] 유니코드 이스케이프와 잘못된 JSON 거부
 */
SW_TEST_CASE( JsonDocumentTest, UnicodeEscapeAndRejectMalformed )
{
    sw::JsonDocument doc;
    SW_EXPECT_TRUE( doc.parse( R"({"Title":"A\u0020B","Nested":{"k":1}})" ) );
    SW_EXPECT_EQUAL( sw::string( "A B" ), doc.getRoot().get( "Title" ).asString() );
    SW_EXPECT_TRUE( sw::JsonDocument::extractStringField( R"({"Title":"Hero"})", "title", true ) == sw::string( "Hero" ) );
    SW_EXPECT_TRUE( sw::JsonDocument::extractStringField( R"({"Title":"Hero"})", "title", false ).empty() );

    const sw::string raw     = "line\n\t\"quote\"\\slash";
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
 * @brief [JsonDocumentTest] 쓰기 후 dump 라운드트립
 */
SW_TEST_CASE( JsonDocumentTest, WriteAndDumpRoundtrip )
{
    sw::JsonDocument doc;
    sw::JsonValue    root = doc.makeObject();
    root.set( "name" ).setString( "Demo" );
    root.set( "count" ).setInt( 3 );
    sw::JsonValue arr = root.set( "items" );
    arr.setArray();
    arr.pushBack().setInt( 1 );
    arr.pushBack().setInt( 2 );

    sw::JsonDocument loaded;
    SW_EXPECT_TRUE( loaded.parse( doc.dump() ) );
    SW_EXPECT_EQUAL( sw::string( "Demo" ), loaded.getRoot().get( "name" ).asString() );
    SW_EXPECT_EQUAL( 3, loaded.getRoot().get( "count" ).asInt() );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( loaded.getRoot().get( "items" ).size() ) );
}

/**
 * @brief [JsonDocumentTest] 불리언, 부동소수점 및 깊은 중첩 구조 파싱 검증
 */
SW_TEST_CASE( JsonDocumentTest, BooleanFloatAndDeepNestedObject )
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

    sw::JsonValue root = doc.getRoot();
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

/**
 * @brief [JsonDocumentTest] 부동소수점 JSON 토큰의 asInt, asUint 변환 안전성 엣지 케이스 검증
 */
SW_TEST_CASE( JsonDocumentTest, FloatToIntTypeSafetyAndCoercion )
{
    const utf8* jsonStr = R"({
		"floatVal": 3.75,
		"intVal": 42,
		"negativeFloat": -10.8,
		"zeroVal": 0.0
	})";

    sw::JsonDocument doc;
    SW_EXPECT_TRUE( doc.parse( jsonStr ) );

    sw::JsonValue root = doc.getRoot();
    SW_EXPECT_EQUAL( 3, root.get( "floatVal" ).asInt() );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( root.get( "floatVal" ).asUint() ) );
    SW_EXPECT_EQUAL( 42, root.get( "intVal" ).asInt() );
    SW_EXPECT_EQUAL( -10, root.get( "negativeFloat" ).asInt() );
    SW_EXPECT_EQUAL( 0, root.get( "zeroVal" ).asInt() );
}

/**
 * @brief [JsonDocumentTest] int64 를 넘는 부호 없는 수가 음수로 돌아오지 않는다
 * @details nlohmann 의 `is_number_integer()` 는 부호 있는 정수와 **부호 없는 정수 둘 다에 참**이다.
 *          `asInt` 와 `asFloat` 은 그 검사를 부호 없는 검사보다 **먼저** 해서, 부호 없는 가지가
 *          영영 돌지 않았다 — `asFloat` 에서는 그 탓에 `18446744073709551615` 가 `-1.0` 이 됐다.
 *          `asUint` 만 순서가 맞아 있었고, 셋이 같은 파일 안에서 어긋나 있었다.
 */
SW_TEST_CASE( JsonDocumentTest, LargeUnsignedNumbersKeepTheirMagnitude )
{
    sw::JsonDocument doc;
    SW_ASSERT_TRUE( doc.parse( R"({"big":18446744073709551615,"color":4289362560})" ) );

    const sw::JsonValue root = doc.getRoot();
    SW_ASSERT_TRUE( root.isObject() );

    SW_EXPECT_EQUAL( uint64( 18446744073709551615ull ), root.get( "big" ).asUint( 0 ) );

    // 크기를 잃지 않았는지만 본다 — int64 에 담을 수 없는 값이므로 정확한 정수 비교는 뜻이 없다.
    const float64 big = root.get( "big" ).asFloat( 0.0 );
    SW_EXPECT_TRUE( big > 1.0e19 );

    // 흔한 크기(ARGB 색)는 세 접근자 모두에서 같은 값이어야 한다.
    SW_EXPECT_EQUAL( uint64( 4289362560ull ), root.get( "color" ).asUint( 0 ) );
    SW_EXPECT_EQUAL( int64( 4289362560ll ), root.get( "color" ).asInt( 0 ) );
    SW_EXPECT_NEAR_EQUAL( 4289362560.0, root.get( "color" ).asFloat( 0.0 ), 1.0 );
}
