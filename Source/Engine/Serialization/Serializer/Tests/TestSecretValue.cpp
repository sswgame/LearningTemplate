// SecretValueTest — 비밀 값 형식: 글(JSON · XML)에 값이 나가지 않고 가린 글은 값으로 읽히지 않으며, 바이너리 · 고정 배치로는 왕복한다. 지우기와 비교.
#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/Serializer/Tests/SerializerTestTypes.h"

#include <type_traits>

using namespace sw;

// 글로 새는 변환이 없다 — 로그 서식 · 문자열 칸에 그대로 넘길 수 없다
static_assert( std::is_convertible_v<SecretText, string_view> == false );
static_assert( std::is_convertible_v<SecretText, string> == false );
static_assert( std::is_convertible_v<Secret32, const uint8*> == false );

namespace
{
    SecretValueSample makeSample()
    {
        SecretValueSample sample;
        sample._visible = 7;
        for ( int32 index = 0; index < Secret32::kSize; ++index )
        {
            sample._digest.reveal()[index] = static_cast<uint8>( 200 + index % 50 );
        }
        sample._password = "hunter2-plain";
        return sample;
    }
} // namespace

/** @brief [SecretValueTest] JSON · XML 글에는 비밀 값 대신 가린 글이 있고 다른 칸은 그대로다 */
SW_TEST_CASE( SecretValueTest, TextFormatsNeverCarryTheValue )
{
    const SecretValueSample sample = makeSample();
    const expected<string>  json   = JSONSerializer::serialize( &sample, *SecretValueSample::StaticType() );
    SW_REQUIRE_TRUE( json.has_value() );
    const string xml = XMLSerializer::serialize( &sample, *SecretValueSample::StaticType() );
    for ( const string& text : { *json, xml } )
    {
        SW_EXPECT_TRUE( text.find( "hunter2" ) == string::npos );
        SW_EXPECT_TRUE( text.find( "200" ) == string::npos ); // 다이제스트 첫 바이트
        SW_EXPECT_TRUE( text.find( SecretValueUtil::kRedactedText ) != string::npos );
        SW_EXPECT_TRUE( text.find( "7" ) != string::npos );
    }

    // 가린 글을 값으로 읽지 않는다
    SecretText restored;
    SW_EXPECT_FALSE( SerializerUtil::parseTextValue( MutableValueAddress{ &restored }, SecretText::StaticType()->_name, SecretValueUtil::kRedactedText,
                                                     SerializeContext::getDefault( SerializePurpose::Authoring ) ) );
    SW_EXPECT_TRUE( restored.empty() );
}

/** @brief [SecretValueTest] 바이너리와 고정 배치는 값을 그대로 왕복한다 */
SW_TEST_CASE( SecretValueTest, BinaryFormatsRoundTrip )
{
    const SecretValueSample sample = makeSample();
    vector<uint8>           bytes;
    SW_REQUIRE_TRUE( BinarySerializer::serialize( &sample, *SecretValueSample::StaticType(), bytes ) );
    SecretValueSample fromBinary;
    SW_REQUIRE_TRUE( BinarySerializer::deserialize( &fromBinary, *SecretValueSample::StaticType(), bytes.data(), bytes.size() ) );
    SW_EXPECT_TRUE( fromBinary._digest.isEqual( sample._digest ) );
    SW_EXPECT_TRUE( fromBinary._password.reveal() == "hunter2-plain" );

    BitWriter writer;
    SW_REQUIRE_TRUE( FixedLayout::write( sample, writer ) );
    const vector<uint8>& fixedBytes = writer.getBytes();
    BitReader            reader( fixedBytes.data(), static_cast<int32>( fixedBytes.size() ) );
    SecretValueSample    fromFixed;
    SW_REQUIRE_TRUE( FixedLayout::read( reader, fromFixed ) == FixedReadResult::Ok );
    SW_EXPECT_TRUE( fromFixed._digest.isEqual( sample._digest ) );
    SW_EXPECT_TRUE( fromFixed._password.reveal() == "hunter2-plain" );
}

/** @brief [SecretValueTest] 지우면 값이 없고, 짧은 글로 덮어도 앞 글을 잇지 않으며, 한 바이트만 달라도 같지 않다 */
SW_TEST_CASE( SecretValueTest, WipeAndCompare )
{
    SecretValueSample left  = makeSample();
    SecretValueSample right = left;
    SW_EXPECT_TRUE( left._digest.isEqual( right._digest ) );
    right._digest.reveal()[31] ^= 1;
    SW_EXPECT_FALSE( left._digest.isEqual( right._digest ) );

    left._password = "x";
    SW_EXPECT_TRUE( left._password.reveal() == "x" );
    left._password.wipe();
    left._digest.wipe();
    SW_EXPECT_TRUE( left._password.empty() );
    SW_EXPECT_TRUE( left._digest.isEqual( Secret32{} ) );
}
