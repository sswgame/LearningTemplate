#include "pch.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_JSON — 파싱·탐색과 대소문자 무시 키
// ------------------------------------------------------------------------------
/**
 * @brief [JSONDocumentTest] 파싱·탐색과 대소문자 무시 키
 */
SW_TEST_CASE( JSONDocumentTest, ParseAndNavigateIgnoreCaseKeys )
{
    sw::JSONDocument doc;
    SW_EXPECT_TRUE( doc.parse( R"({"Name":"Demo","_score":12,"Items":[{"id":1},{"id":2}]})" ) );

    sw::JSONValue root = doc.getRoot();
    SW_EXPECT_TRUE( root.isObject() );
    SW_EXPECT_EQUAL( sw::string( "Demo" ), root.get( "name" ).asString() );
    SW_EXPECT_EQUAL( 12, root.get( "_SCORE" ).asInt() );
    SW_EXPECT_TRUE( root.get( "missing" ).isValid() == false );

    sw::JSONValue items = root.get( "items" );
    SW_EXPECT_TRUE( items.isArray() );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( items.size() ) );
    SW_EXPECT_EQUAL( 1, items.at( 0 ).get( "ID" ).asInt() );
    SW_EXPECT_EQUAL( 2, items.at( 1 ).get( "id" ).asInt() );
}

/**
 * @brief [JSONDocumentTest] 대소문자 구분 키 옵트아웃
 */
SW_TEST_CASE( JSONDocumentTest, CaseSensitiveKeyOptOut )
{
    sw::JSONDocument doc;
    SW_EXPECT_TRUE( doc.parse( R"({"Child":"ok"})" ) );

    sw::JSONValue root = doc.getRoot();
    SW_EXPECT_TRUE( root.get( "child", false ).isValid() == false );
    SW_EXPECT_TRUE( root.get( "Child", false ).isValid() );
    SW_EXPECT_EQUAL( sw::string( "ok" ), root.get( "Child", false ).asString() );
}

/**
 * @brief [JSONDocumentTest] 유니코드 이스케이프와 잘못된 JSON 거부
 */
SW_TEST_CASE( JSONDocumentTest, UnicodeEscapeAndRejectMalformed )
{
    sw::JSONDocument doc;
    SW_EXPECT_TRUE( doc.parse( R"({"Title":"A\u0020B","Nested":{"k":1}})" ) );
    SW_EXPECT_EQUAL( sw::string( "A B" ), doc.getRoot().get( "Title" ).asString() );
    SW_EXPECT_TRUE( sw::JSONDocument::extractStringField( R"({"Title":"Hero","HP":"10"})", "title", true ) == sw::string( "Hero" ) );
    SW_EXPECT_TRUE( sw::JSONDocument::extractStringField( R"({"Title":"Hero"})", "title", false ).empty() );

    const sw::string raw     = "line\n\t\"quote\"\\slash";
    const sw::string escaped = sw::JSONDocument::escapeString( raw );
    SW_EXPECT_TRUE( escaped.find( '\n' ) == sw::string::npos );
    SW_EXPECT_TRUE( escaped.find( '\t' ) == sw::string::npos );
    SW_EXPECT_TRUE( escaped.find( "\\\"" ) != sw::string::npos );
    SW_EXPECT_TRUE( escaped.find( "\\\\" ) != sw::string::npos );
    SW_EXPECT_EQUAL( raw, sw::JSONDocument::unescapeString( escaped ) );

    {
        SW_TEST_DEFENSIVE_SCOPE( "Testing malformed JSON parse rejection" );
        sw::JSONDocument bad;
        SW_EXPECT_FALSE( bad.parse( R"({"not_a_pair","_id":1})" ) );
    }
}

/**
 * @brief [JSONDocumentTest] 쓰기 후 dump 라운드트립
 */
SW_TEST_CASE( JSONDocumentTest, WriteAndDumpRoundtrip )
{
    sw::JSONDocument doc;
    sw::JSONValue    root = doc.makeObject();
    root.set( "name" ).setString( "Demo" );
    root.set( "count" ).setInt( 3 );
    sw::JSONValue arr = root.set( "items" );
    arr.setArray();
    arr.pushBack().setInt( 1 );
    arr.pushBack().setInt( 2 );

    sw::JSONDocument loaded;
    SW_EXPECT_TRUE( loaded.parse( doc.dump() ) );
    SW_EXPECT_EQUAL( sw::string( "Demo" ), loaded.getRoot().get( "name" ).asString() );
    SW_EXPECT_EQUAL( 3, loaded.getRoot().get( "count" ).asInt() );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( loaded.getRoot().get( "items" ).size() ) );
}

/**
 * @brief [JSONDocumentTest] 불리언, 부동소수점 및 깊은 중첩 구조 파싱 검증
 */
SW_TEST_CASE( JSONDocumentTest, BooleanFloatAndDeepNestedObject )
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

    sw::JSONDocument doc;
    SW_EXPECT_TRUE( doc.parse( jsonStr ) );

    sw::JSONValue root = doc.getRoot();
    SW_EXPECT_TRUE( root.get( "bEnabled" ).asBool() );
    SW_EXPECT_FALSE( root.get( "bPaused" ).asBool() );
    SW_EXPECT_NEAR_EQUAL( 2.75, root.get( "scale" ).asFloat(), 1e-4 );

    sw::JSONValue subArr = root.get( "nested" ).get( "sub" ).get( "array" );
    SW_EXPECT_TRUE( subArr.isArray() );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( subArr.size() ) );
    SW_EXPECT_NEAR_EQUAL( 10.5, subArr.at( 0 ).asFloat(), 1e-4 );
    SW_EXPECT_NEAR_EQUAL( 20.25, subArr.at( 1 ).asFloat(), 1e-4 );
    SW_EXPECT_NEAR_EQUAL( 30.125, subArr.at( 2 ).asFloat(), 1e-4 );
}

/**
 * @brief [JSONDocumentTest] 부동소수점 JSON 토큰의 asInt, asUint 변환 안전성 엣지 케이스 검증
 */
SW_TEST_CASE( JSONDocumentTest, FloatToIntTypeSafetyAndCoercion )
{
    const utf8* jsonStr = R"({
		"floatVal": 3.75,
		"intVal": 42,
		"negativeFloat": -10.8,
		"zeroVal": 0.0
	})";

    sw::JSONDocument doc;
    SW_EXPECT_TRUE( doc.parse( jsonStr ) );

    sw::JSONValue root = doc.getRoot();
    SW_EXPECT_EQUAL( 3, root.get( "floatVal" ).asInt() );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( root.get( "floatVal" ).asUint() ) );
    SW_EXPECT_EQUAL( 42, root.get( "intVal" ).asInt() );
    SW_EXPECT_EQUAL( -10, root.get( "negativeFloat" ).asInt() );
    SW_EXPECT_EQUAL( 0, root.get( "zeroVal" ).asInt() );
}

/**
 * @brief [JSONDocumentTest] int64 를 넘는 부호 없는 수가 음수로 돌아오지 않는다
 * @details nlohmann 의 `is_number_integer()` 는 부호 있는 정수와 **부호 없는 정수 둘 다에 참**이다.
 *          그 검사를 부호 없는 검사보다 **먼저** 하면 부호 없는 가지가 영영 돌지 않는다 — `asFloat` 에서는
 *          `18446744073709551615` 가 `-1.0` 이 된다. `asInt` · `asFloat` · `asUint` 셋이 같은 순서여야 한다.
 */
SW_TEST_CASE( JSONDocumentTest, LargeUnsignedNumbersKeepTheirMagnitude )
{
    sw::JSONDocument doc;
    SW_ASSERT_TRUE( doc.parse( R"({"big":18446744073709551615,"color":4289362560})" ) );

    const sw::JSONValue root = doc.getRoot();
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

/**
 * @brief [JSONDocumentTest] 구문 오류는 `이름:줄:열: 이유` 로 알린다 — nlohmann 의 예외 머리말 없이
 * @details "Parse error in json text" 한 줄로는 어느 파일의 어디인지 알 수 없다.
 */
SW_TEST_CASE( JSONDocumentTest, ParseErrorNamesSourceLineAndColumn )
{
    sw::JSONDocument doc;
    {
        test::ScopedDefensiveTestLog expected( "malformed JSON" );
        SW_EXPECT_FALSE( doc.parse( "{\n  \"a\": 1,\n}\n", "data.json" ) );
    }
    const sw::string& error = doc.getLastError();
    SW_EXPECT_TRUE_MSG( sw::StringUtil::startsWith( error, "data.json:3:1: " ), error.c_str() );
    SW_EXPECT_TRUE_MSG( error.find( "json.exception" ) == sw::string::npos, error.c_str() );

    // 줄 가운데 · 한글 뒤의 자리 — 열은 글자 수다.
    {
        test::ScopedDefensiveTestLog expected( "malformed JSON" );
        SW_EXPECT_FALSE( doc.parse( "{\"\xED\x95\x9C\": tru}", "korean.json" ) );
    }
    SW_EXPECT_TRUE_MSG( sw::StringUtil::startsWith( doc.getLastError(), "korean.json:1:" ), doc.getLastError().c_str() );

    SW_EXPECT_FALSE( doc.loadPath( "no/such/dir/missing.json" ) );
    SW_EXPECT_TRUE_MSG( doc.getLastError().find( "missing.json: not found" ) != sw::string::npos, doc.getLastError().c_str() );

    SW_EXPECT_TRUE( doc.parse( "{}" ) );
    SW_EXPECT_TRUE( doc.getLastError().empty() );
}
