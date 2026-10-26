#include "pch.h"

#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/JSON/JSONWriter.h"

#include "TestFramework/TestFramework.h"

#include <limits>

// JSONWriter — 이스케이프 · 중첩 · 빈 컨테이너 · 들여쓰기 · 수 표기 · 고정 버퍼 · 순서 위반.

namespace
{
    /** @brief 문자열 값 하나만 쓴 결과입니다(따옴표 포함). */
    sw::string writeStringValue( sw::string_view text )
    {
        sw::string     json;
        sw::JSONWriter writer{ json };
        writer.valueString( text );
        SW_EXPECT_TRUE( writer.isComplete() );
        return json;
    }
} // namespace

/**
 * @brief [JSONWriterTest] 따옴표 · 역슬래시 · 제어 문자는 이스케이프하고 UTF-8 은 그대로 두며, 결과는 `JSONDocument::escapeString`(nlohmann)과 같은 바이트다
 */
SW_TEST_CASE( JSONWriterTest, EscapesQuotesBackslashesAndControlCharacters )
{
    SW_EXPECT_EQUAL( sw::string( R"("say \"hi\" \\ c:\\path")" ), writeStringValue( "say \"hi\" \\ c:\\path" ) );
    SW_EXPECT_EQUAL( sw::string( R"("\b\f\n\r\t")" ), writeStringValue( "\b\f\n\r\t" ) );
    SW_EXPECT_EQUAL( sw::string( R"("\u0000\u0001\u001f")" ), writeStringValue( sw::string_view{ "\x00\x01\x1F", 3 } ) );
    SW_EXPECT_EQUAL( sw::string( "\"/\x7F\"" ), writeStringValue( "/\x7F" ) );                                                                   // 슬래시와 DEL 은 그대로
    SW_EXPECT_EQUAL( sw::string( "\"\xEA\xB0\x80 \xC2\xB7 \xF0\x9F\x98\x80\"" ), writeStringValue( "\xEA\xB0\x80 \xC2\xB7 \xF0\x9F\x98\x80" ) ); // 가 · 😀

    const sw::string mixed = sw::string( "a\"b\\c\nd\x01" ) + "\xEA\xB0\x80";
    SW_EXPECT_EQUAL( "\"" + sw::JSONDocument::escapeString( mixed ) + "\"", writeStringValue( mixed ) );

    // 키도 같은 규칙으로 이스케이프한다.
    sw::string     json;
    sw::JSONWriter writer{ json };
    writer.beginObject();
    writer.key( "k\"ey" );
    writer.valueInt( 1 );
    writer.endObject();
    SW_EXPECT_EQUAL( sw::string( R"({"k\"ey":1})" ), json );
}

/**
 * @brief [JSONWriterTest] 잘못된 UTF-8 바이트는 U+FFFD 로 바꿔 출력이 늘 올바른 JSON 이다
 */
SW_TEST_CASE( JSONWriterTest, ReplacesInvalidUtf8 )
{
    SW_EXPECT_EQUAL( sw::string( R"("a\ufffdb")" ), writeStringValue( "a\xFF"
                                                                      "b" ) );
    SW_EXPECT_EQUAL( sw::string( R"("\ufffd")" ), writeStringValue( "\xEA\xB0" ) ); // 끝에서 잘린 글자

    sw::JSONDocument document;
    SW_ASSERT_TRUE( document.parse( writeStringValue( "x\xC0\xAFy" ) ) ); // 지나치게 긴 표기
    SW_EXPECT_TRUE( document.getRoot().isString() );
}

/**
 * @brief [JSONWriterTest] 객체 · 배열을 중첩하면 쉼표와 콜론을 쓰기기가 놓고, 다시 읽으면 같은 값이다
 */
SW_TEST_CASE( JSONWriterTest, NestsObjectsAndArrays )
{
    sw::string     json;
    sw::JSONWriter writer{ json };
    writer.beginObject();
    writer.key( "name" );
    writer.valueString( "n" );
    writer.key( "list" );
    writer.beginArray();
    writer.valueInt( -3 );
    writer.valueUint( 18446744073709551615ull );
    writer.valueBool( false );
    writer.valueNull();
    writer.beginObject();
    writer.key( "deep" );
    writer.beginArray();
    writer.valueString( "x" );
    writer.endArray();
    writer.endObject();
    writer.endArray();
    writer.key( "ok" );
    writer.valueBool( true );
    writer.endObject();
    SW_EXPECT_TRUE( writer.isComplete() );
    SW_EXPECT_EQUAL( sw::string( R"({"name":"n","list":[-3,18446744073709551615,false,null,{"deep":["x"]}],"ok":true})" ), json );
    SW_EXPECT_EQUAL( json.size(), writer.getWrittenSize() );

    sw::JSONDocument document;
    SW_ASSERT_TRUE( document.parse( json ) );
    SW_EXPECT_EQUAL( sw::string( "x" ), document.getRoot().get( "list" ).at( 4 ).get( "deep" ).at( 0 ).asString() );
}

/**
 * @brief [JSONWriterTest] 빈 객체 · 배열은 한 줄이든 들여쓰기든 `{}` · `[]` 이고, 들여쓰기는 `JSONDocument::dump( 2 )` 와 같은 모양이다
 */
SW_TEST_CASE( JSONWriterTest, EmptyContainersAndIndentation )
{
    {
        sw::string     json;
        sw::JSONWriter writer{ json };
        writer.beginArray();
        writer.beginObject();
        writer.endObject();
        writer.beginArray();
        writer.endArray();
        writer.endArray();
        SW_EXPECT_TRUE( writer.isComplete() );
        SW_EXPECT_EQUAL( sw::string( "[{},[]]" ), json );
    }

    sw::string     json;
    sw::JSONWriter writer{ json, 2 };
    writer.beginObject();
    writer.key( "a" );
    writer.valueInt( 1 );
    writer.key( "empty" );
    writer.beginArray();
    writer.endArray();
    writer.key( "list" );
    writer.beginArray();
    writer.valueString( "x" );
    writer.beginObject();
    writer.endObject();
    writer.endArray();
    writer.endObject();
    SW_EXPECT_TRUE( writer.isComplete() );
    SW_EXPECT_EQUAL( sw::string( "{\n  \"a\": 1,\n  \"empty\": [],\n  \"list\": [\n    \"x\",\n    {}\n  ]\n}" ), json );

    sw::JSONDocument document;
    SW_ASSERT_TRUE( document.parse( json ) );
    SW_EXPECT_EQUAL( document.dump( 2 ), json );
}

/**
 * @brief [JSONWriterTest] 고정 소수는 자리를 채우고, 실수는 다시 읽어도 같으며, NaN · 무한대는 null 을 쓰고 실패로 남는다
 */
SW_TEST_CASE( JSONWriterTest, NumberRules )
{
    sw::string     json;
    sw::JSONWriter writer{ json };
    writer.beginArray();
    writer.valueFixedPoint( 1500, 3 );
    writer.valueFixedPoint( 1, 3 );
    writer.valueFixedPoint( 42, 0 );
    writer.valueFloat( 0.1 );
    writer.valueFloat( 2.0 );
    writer.endArray();
    SW_EXPECT_TRUE( writer.isComplete() );
    SW_EXPECT_EQUAL( sw::string( "[1.500,0.001,42,0.1,2.0]" ), json );

    sw::string     nanJSON;
    sw::JSONWriter nanWriter{ nanJSON };
    nanWriter.beginArray();
    nanWriter.valueFloat( std::numeric_limits<float64>::quiet_NaN() );
    nanWriter.valueFloat( std::numeric_limits<float64>::infinity() );
    nanWriter.endArray();
    SW_EXPECT_EQUAL( sw::string( "[null,null]" ), nanJSON );
    SW_EXPECT_FALSE( nanWriter.isComplete() );
}

/**
 * @brief [JSONWriterTest] 고정 버퍼는 넘치면 자르고 0 으로 끝내며 실패로 남는다. 넉넉하면 문자열 버퍼와 같은 바이트다
 */
SW_TEST_CASE( JSONWriterTest, FixedBufferTruncatesAndTerminates )
{
    utf8           arrLarge[64]{};
    sw::JSONWriter large{ arrLarge, 64 };
    large.beginObject();
    large.key( "k" );
    large.valueString( "v\n" );
    large.endObject();
    SW_EXPECT_TRUE( large.isComplete() );
    SW_EXPECT_STREQ( R"({"k":"v\n"})", arrLarge );
    SW_EXPECT_EQUAL( static_cast<size_t>( 11 ), large.getWrittenSize() );

    utf8           arrSmall[8]{};
    sw::JSONWriter small{ arrSmall, 8 };
    small.beginObject();
    small.key( "key" );
    small.valueString( "long value" );
    small.endObject();
    SW_EXPECT_FALSE( small.isComplete() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 7 ), small.getWrittenSize() );
    SW_EXPECT_EQUAL( '\0', arrSmall[7] );
}

/**
 * @brief [JSONWriterTest] 순서 위반(배열 안의 키, 키 없는 객체 값, 짝이 안 맞는 닫기, 두 번째 루트)은 단언이고 완성되지 않는다
 */
SW_TEST_CASE( JSONWriterTest, MisuseIsReported )
{
    test::ScopedAssertCapture assertCapture;
    sw::string                json;
    sw::JSONWriter            writer{ json };
    writer.beginArray();
    writer.key( "no" );
    writer.endObject();
    writer.endArray();
    writer.valueInt( 1 );
    SW_EXPECT_FALSE( writer.isComplete() );
    if ( test::ScopedAssertCapture::kAssertsAreActive )
        SW_EXPECT_EQUAL( 3u, assertCapture.getCount() );

    sw::string     objectJSON;
    sw::JSONWriter objectWriter{ objectJSON };
    objectWriter.beginObject();
    objectWriter.valueInt( 1 );
    objectWriter.endObject();
    SW_EXPECT_FALSE( objectWriter.isComplete() );
}
