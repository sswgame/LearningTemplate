// CharacterDataReaderTest — 캐릭터 데이터 읽개의 필수 · 선택 필드 규칙, 오류 모으기, 쓰기 형식(nogpu).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Serialization/XML/XMLDocument.h"

using namespace sw;

/** @brief [CharacterDataReaderTest] 루트: 찾으면 true 이고, 다른 루트는 오류로 모은 뒤 finish 가 id 로 낸다 */
SW_TEST_CASE( CharacterDataReaderTest, RootStatus )
{
    SW_EXPECT_LOG( "Character.RootMissing" );
    XMLDocument         document;
    XMLNode             root;
    CharacterDataReader reader( "test" );
    SW_EXPECT_TRUE( reader.parseRoot( document, "<Character/>", "Character", root ) );
    SW_EXPECT_FALSE( reader.hasError() );
    SW_EXPECT_TRUE( reader.finish() );

    XMLDocument         other;
    CharacterDataReader wrong( "test" );
    SW_EXPECT_FALSE( wrong.parseRoot( other, "<Other/>", "Character", root ) );
    SW_EXPECT_TRUE( wrong.hasError() );
    SW_EXPECT_FALSE( wrong.finish() );
}

/** @brief [CharacterDataReaderTest] 필수 필드: 읽히면 값이고 오류가 없다. 벡터는 공백이나 쉼표로 나눈다 */
SW_TEST_CASE( CharacterDataReaderTest, RequiredPropertiesRead )
{
    XMLDocument         document;
    XMLNode             root;
    CharacterDataReader reader( "test" );
    SW_REQUIRE_TRUE( reader.parseRoot( document, "<C f=\" 1.5 \" i=\"7\" b=\"true\" v=\"1, 2 3\" n=\"name\" list=\"a, b  c\"/>", "C", root ) );
    SW_EXPECT_NEAR( 1.5f, reader.readFloat( root, "f" ), 1.0e-6f );
    SW_EXPECT_EQUAL( 7, reader.readInt( root, "i" ) );
    SW_EXPECT_TRUE( reader.readBool( root, "b" ) );
    const float3 vector3 = reader.readFloat3( root, "v" );
    SW_EXPECT_NEAR( 3.0f, vector3._z, 1.0e-6f );
    SW_EXPECT_TRUE( reader.readName( root, "n", true ) == hashed_string( "name" ) );
    vector<hashed_string> listName{ hashed_string( "stale" ) };
    reader.readNameList( root, "list", listName );
    SW_REQUIRE_EQUAL( size_t( 3 ), listName.size() );
    SW_EXPECT_TRUE( listName[2] == hashed_string( "c" ) );
    SW_EXPECT_EQUAL( string( "a b c" ), CharacterDataReader::formatNameList( listName ) );
    SW_EXPECT_FALSE( reader.hasError() );
}

/** @brief [CharacterDataReaderTest] 필수 필드: 없거나 형식이 틀리면 종류마다 오류로 모은다 */
SW_TEST_CASE( CharacterDataReaderTest, BadPropertiesCollectErrors )
{
    SW_EXPECT_LOG( "Character.AttributeMissing" );
    SW_EXPECT_LOG( "Character.NumberInvalid" );
    SW_EXPECT_LOG( "Character.IntegerInvalid" );
    SW_EXPECT_LOG( "Character.BooleanInvalid" );
    SW_EXPECT_LOG( "Character.Vector3Invalid" );
    SW_EXPECT_LOG( "Character.NameEmpty" );
    SW_EXPECT_LOG( "Character.UnknownAttribute" );
    SW_EXPECT_LOG( "Character.ElementUnknown" );
    XMLDocument         document;
    XMLNode             root;
    CharacterDataReader reader( "test" );
    SW_REQUIRE_TRUE( reader.parseRoot( document, "<C f=\"x\" i=\"1.5\" b=\"maybe\" v=\"1 2\" list=\"a none\" known=\"1\"/>", "C", root ) );
    SW_EXPECT_NEAR( 0.0f, reader.readFloat( root, "missing" ), 1.0e-6f );
    (void)reader.readFloat( root, "f" );
    (void)reader.readInt( root, "i" );
    SW_EXPECT_FALSE( reader.readBool( root, "b" ) );
    (void)reader.readFloat3( root, "v" );
    SW_EXPECT_TRUE( reader.readName( root, "absent", true ).empty() );
    vector<hashed_string> listName;
    reader.readNameList( root, "list", listName );
    SW_EXPECT_EQUAL( size_t( 1 ), listName.size() );
    const utf8* const arrKnown[] = { "f", "i", "b", "v", "list" };
    reader.reportUnknownAttributes( root, arrKnown );
    reader.reportUnknownElement( root );
    SW_EXPECT_FALSE( reader.finish() );
}

/** @brief [CharacterDataReaderTest] 선택 필드: 없으면 넣어 둔 값이 남고, 깨졌으면 값은 그대로에 오류만 남는다. 필수가 아닌 이름은 비어도 된다 */
SW_TEST_CASE( CharacterDataReaderTest, OptionalPropertiesKeepValue )
{
    SW_EXPECT_LOG( "Character.NumberInvalid" );
    XMLDocument         document;
    XMLNode             root;
    CharacterDataReader reader( "test" );
    SW_REQUIRE_TRUE( reader.parseRoot( document, "<C ok=\"2\" bad=\"x\" flag=\"false\" v=\"1 2 3\"/>", "C", root ) );
    float32 value = 9.0f;
    reader.readOptionalFloat( root, "absent", value );
    SW_EXPECT_NEAR( 9.0f, value, 1.0e-6f );
    SW_EXPECT_TRUE( reader.readName( root, "absent", false ).empty() );
    SW_EXPECT_FALSE( reader.hasError() );
    reader.readOptionalFloat( root, "ok", value );
    SW_EXPECT_NEAR( 2.0f, value, 1.0e-6f );
    bool bFlag = true;
    reader.readOptionalBool( root, "flag", bFlag );
    SW_EXPECT_FALSE( bFlag );
    float3 vector3{};
    reader.readOptionalFloat3( root, "v", vector3 );
    SW_EXPECT_NEAR( 2.0f, vector3._y, 1.0e-6f );
    SW_EXPECT_FALSE( reader.hasError() );
    reader.readOptionalFloat( root, "bad", value );
    SW_EXPECT_NEAR( 2.0f, value, 1.0e-6f );
    SW_EXPECT_TRUE( reader.hasError() );
    SW_EXPECT_FALSE( reader.finish() );
}

/** @brief [CharacterDataReaderTest] 쓰기 형식: 음의 0 을 쓰지 않고, 회전은 도 단위로 쓴 글을 다시 읽으면 같은 회전이다 */
SW_TEST_CASE( CharacterDataReaderTest, FormatsRoundTrip )
{
    SW_EXPECT_EQUAL( string( "0" ), CharacterDataReader::formatFloat( -0.0f ) );
    SW_EXPECT_EQUAL( string( "1.5" ), CharacterDataReader::formatFloat( 1.5f ) );
    SW_EXPECT_EQUAL( string( "1 0 -2.5" ), CharacterDataReader::formatFloat3( float3{ 1.0f, 0.0f, -2.5f } ) );

    XMLDocument         document;
    XMLNode             root;
    CharacterDataReader reader( "test" );
    SW_REQUIRE_TRUE( reader.parseRoot( document, "<C r=\"10 20 30\"/>", "C", root ) );
    const quaternion rotation = reader.readRotation( root, "r" );

    XMLDocument  written;
    XMLNode      writtenRoot;
    const string xml = string( "<C r=\"" ) + CharacterDataReader::formatRotation( rotation ) + "\"/>";
    SW_REQUIRE_TRUE( reader.parseRoot( written, xml, "C", writtenRoot ) );
    const quaternion again = reader.readRotation( writtenRoot, "r" );
    const float32    dot   = rotation._x * again._x + rotation._y * again._y + rotation._z * again._z + rotation._w * again._w;
    SW_EXPECT_NEAR( 1.0f, MathUtil::abs( dot ), 1.0e-4f );
    SW_EXPECT_FALSE( reader.hasError() );
}
