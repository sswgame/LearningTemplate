#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Tests/AssetRefTestTypes.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

using namespace sw;

namespace
{
    struct XTestUtil
    {
        static const SerializeContext& getContext() { return SerializeContext::getDefault( SerializePurpose::Authoring ); }
        static const PropertyInfo&     getReference()
        {
            for ( const PropertyInfo& prop : AssetRefSample::StaticType()->_listProperty )
            {
                if ( prop._name == hashed_string( "_reference" ) )
                    return prop;
            }
            SW_ASSERT( false && "_reference is declared on AssetRefSample" );
            return AssetRefSample::StaticType()->_listProperty.front();
        }
    };
} // namespace

/** @brief [AssetRefTest] 대상 종류의 접미사면 넣고(대소문자 무시 · 앞뒤 공백 제거), 빈 글은 칸을 비운다 */
SW_TEST_CASE( AssetRefTest, AcceptsItsKindAndEmptyText )
{
    AssetRef<AssetRefSampleTarget> reference;
    SW_EXPECT_TRUE( reference.empty() );
    SW_EXPECT_TRUE( reference.getTargetType()._name == AssetRefSampleTarget::kAssetType._name );
    const hashed_string typeName( AssetRefBase::kTypeName );
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &reference }, typeName, "  common/data/a.REFTARGET.xml \t", XTestUtil::getContext() ) );
    SW_EXPECT_TRUE( reference == "common/data/a.REFTARGET.xml" );
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &reference }, typeName, "", XTestUtil::getContext() ) );
    SW_EXPECT_TRUE( reference.empty() );
}

/** @brief [AssetRefTest] 잘못된 종류를 가리키면 종류 이름과 함께 오류를 남기고 값을 넣지 않는다 — 값 하나든 코드의 대입이든 같다 */
SW_TEST_CASE( AssetRefTest, WrongKindIsALoadError )
{
    AssetRef<AssetRefSampleTarget> reference;
    reference = "common/data/kept.reftarget.xml";
    SW_REQUIRE_TRUE( reference == "common/data/kept.reftarget.xml" );

    SW_EXPECT_LOG( "AssetRef.WrongType" );
    SW_EXPECT_FALSE( SerializerUtil::parseTextValue( MutableValueAddress{ &reference }, hashed_string( AssetRefBase::kTypeName ), "common/materials/a.material", XTestUtil::getContext() ) );
    SW_EXPECT_TRUE( reference == "common/data/kept.reftarget.xml" );

    SW_EXPECT_LOG( "AssetRef.WrongType" );
    reference = "common/prefabs/a.prefab.xml";
    SW_EXPECT_TRUE( reference == "common/data/kept.reftarget.xml" );
}

/** @brief [AssetRefTest] 칸을 읽는 공통 길(JSON)도 잘못된 종류를 넣지 않는다 */
SW_TEST_CASE( AssetRefTest, WrongKindInADocumentDoesNotLoad )
{
    AssetRefSample sample;
    SW_EXPECT_LOG( "AssetRef.WrongType" );
    (void)JSONSerializer::deserialize( &sample, *AssetRefSample::StaticType(), R"({"_reference":"common/materials/a.material"})" );
    SW_EXPECT_TRUE( sample._reference.empty() );
}

/** @brief [AssetRefTest] 파일 형식은 경로 글 그대로다 — JSON · 바이너리로 왕복하고, 시퀀스 원소도 같은 처리기를 지난다 */
SW_TEST_CASE( AssetRefTest, RoundTrips )
{
    AssetRefSample source;
    source._reference = "common/data/a.reftarget.xml";
    source._listReference.emplace_back( string_view{ "common/data/b.reftarget.xml" } );

    const string json = JSONSerializer::serialize( &source, *AssetRefSample::StaticType() );
    SW_EXPECT_TRUE( json.find( "\"common/data/a.reftarget.xml\"" ) != string::npos );
    AssetRefSample fromJSON;
    SW_REQUIRE_TRUE( JSONSerializer::deserialize( &fromJSON, *AssetRefSample::StaticType(), json ) );
    SW_EXPECT_TRUE( fromJSON._reference == "common/data/a.reftarget.xml" );
    SW_REQUIRE_EQUAL( fromJSON._listReference.size(), static_cast<size_t>( 1 ) );
    SW_EXPECT_TRUE( fromJSON._listReference[0] == "common/data/b.reftarget.xml" );

    vector<uint8> bytes;
    SW_REQUIRE_TRUE( BinarySerializer::serialize( &source, *AssetRefSample::StaticType(), bytes ) );
    AssetRefSample fromBinary;
    SW_REQUIRE_TRUE( BinarySerializer::deserialize( &fromBinary, *AssetRefSample::StaticType(), bytes.data(), bytes.size() ) );
    SW_EXPECT_TRUE( fromBinary._reference == "common/data/a.reftarget.xml" );
}

/** @brief [AssetRefTest] 파서가 칸을 내장 형식과 경로 칸 메타로 적는다 — 프리로드 · 참조 칸 목록이 이 메타를 본다 */
SW_TEST_CASE( AssetRefTest, ParserMarksTheFieldAsAPath )
{
    const PropertyInfo& prop = XTestUtil::getReference();
    SW_EXPECT_TRUE( prop._typeName == hashed_string( AssetRefBase::kTypeName ) );
    SW_EXPECT_TRUE( prop._metadata->_bAssetPath != SW_FALSE );
    SW_EXPECT_EQUAL( prop._metadata->_assetType, string( "AssetRefSampleTarget" ) );
}
