// SerializerTranscodeTest — 리플렉션 객체 하나를 형식 사이로 옮긴다: JSON · XML → 바이너리는 바로 쓴 바이너리와 같은 바이트이고, 바이너리 → JSON · XML 은 바로 쓴 글과 같다.
// 읽지 못하는 입력은 받는 버퍼를 건드리지 않는다. `Archive` 의 변환 함수는 같은 길을 아카이브에 싣는다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Format/SerializerTranscodeUtil.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/Serializer/Tests/SerializerTestTypes.h"

using namespace sw;

namespace
{
    struct SerializerTranscodeTestUtil
    {
        /** @brief 칸마다 기본값이 아닌 값을 넣은 샘플입니다 — 글 형식이 기본값 칸을 적지 않아도 왕복이 같은 값이 된다. */
        static SerializerSample makeSample()
        {
            SerializerSample sample;
            sample._count       = 42;
            sample._ratio       = 0.25f;
            sample._name        = "knight";
            sample._small       = 9;
            sample._mode        = SerializerSampleMode::Fast;
            sample._bFirst      = 1;
            sample._withDefault = 3;
            // 경로 칸은 "읽지 않음" 과 "빈 글" 이 다르다 — 값을 넣어 세 형식이 같은 상태를 싣게 한다. 케이스가 `isSet` 으로 확인한다.
            const PropertyInfo* pPath = SerializerSample::StaticType()->findProperty( hashed_string( "_path" ) );
            if ( pPath != nullptr )
                (void)SerializerUtil::applyPropertyText( *pPath, MutableObjectAddress{ &sample }, "common/materials/a.material", SerializeContext::getDefault( SerializePurpose::Authoring ) );
            return sample;
        }
    };
} // namespace

/**
 * @brief [SerializerTranscodeTest] JSON · XML 을 바이너리로 옮기면 그 객체를 바로 쓴 바이너리와 같고, 바이너리를 글로 옮기면 바로 쓴 글과 같다
 */
SW_TEST_CASE( SerializerTranscodeTest, TextAndBinaryTranscodeBothWays )
{
    const SerializerSample sample = SerializerTranscodeTestUtil::makeSample();
    const TypeInfo&        type   = *SerializerSample::StaticType();
    SW_REQUIRE_TRUE( sample._path.isSet() );

    const expected<string> json = JSONSerializer::serialize( &sample, type );
    SW_REQUIRE_TRUE( json.has_value() );
    const string  xml = XMLSerializer::serialize( &sample, type );
    vector<uint8> binary;
    SW_REQUIRE_TRUE( BinarySerializer::serialize( &sample, type, binary ) );

    vector<uint8> fromJSON;
    vector<uint8> fromXML;
    SW_REQUIRE_TRUE( SerializerTranscodeUtil::transcodeJSONToBinary( *json, type, fromJSON ) );
    SW_REQUIRE_TRUE( SerializerTranscodeUtil::transcodeXMLToBinary( xml, type, fromXML ) );
    SW_EXPECT_TRUE( fromJSON == binary );
    SW_EXPECT_TRUE( fromXML == binary );

    SW_EXPECT_EQUAL( *json, SerializerTranscodeUtil::transcodeBinaryToJSON( binary.data(), binary.size(), type ) );
    SW_EXPECT_EQUAL( xml, SerializerTranscodeUtil::transcodeBinaryToXML( binary.data(), binary.size(), type ) );
}

/**
 * @brief [SerializerTranscodeTest] 빈 글 · 빈 바이트는 옮기지 않는다 — 받는 버퍼는 그대로이고 글 쪽은 빈 글이다
 */
SW_TEST_CASE( SerializerTranscodeTest, EmptyInputLeavesTheOutputAlone )
{
    const TypeInfo& type = *SerializerSample::StaticType();
    vector<uint8>   untouched{ 7 };
    SW_EXPECT_FALSE( SerializerTranscodeUtil::transcodeJSONToBinary( "", type, untouched ) );
    SW_EXPECT_FALSE( SerializerTranscodeUtil::transcodeXMLToBinary( "", type, untouched ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, untouched.size() );
    SW_EXPECT_EQUAL( 7, static_cast<int32>( untouched[0] ) );
    SW_EXPECT_TRUE( SerializerTranscodeUtil::transcodeBinaryToJSON( nullptr, 0, type ).empty() );
    SW_EXPECT_TRUE( SerializerTranscodeUtil::transcodeBinaryToXML( nullptr, 0, type ).empty() );
}

/**
 * @brief [SerializerTranscodeTest] `Archive` 는 JSON · XML 글을 바이너리로 바꿔 싣고, 실린 바이너리를 JSON 으로 되돌린다 — 빈 글은 오류 상태로 남는다
 */
SW_TEST_CASE( SerializerTranscodeTest, ArchiveCarriesTheConvertedObject )
{
    const SerializerSample sample = SerializerTranscodeTestUtil::makeSample();
    const TypeInfo&        type   = *SerializerSample::StaticType();
    const expected<string> json   = JSONSerializer::serialize( &sample, type );
    SW_REQUIRE_TRUE( json.has_value() );

    Archive fromJSON;
    SW_REQUIRE_TRUE( fromJSON.convertJSONToBinary( *json, type ) );
    Archive fromXML;
    SW_REQUIRE_TRUE( fromXML.convertXMLToBinary( XMLSerializer::serialize( &sample, type ), type ) );
    SW_REQUIRE_EQUAL( fromJSON.getSize(), fromXML.getSize() );

    Archive readArchive( fromJSON.getData(), fromJSON.getSize() );
    SW_EXPECT_EQUAL( *json, readArchive.convertBinaryToJSON( type ) );

    Archive refused;
    SW_EXPECT_FALSE( refused.convertJSONToBinary( "", type ) );
    SW_EXPECT_TRUE( refused.isError() );
}
