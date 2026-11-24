#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Tests/AssetRefTestTypes.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"
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

        /** @brief GUID 하나를 경로 하나로 푸는 색인을 프로세스 표에 겁니다. 돌려준 표(기동이 건 것)를 테스트 끝에 다시 건다. */
        static AssetRegistry installOne( const AssetGUID& guid, const utf8* pPath )
        {
            vector<AssetRegistryLayer> listLayer;
            listLayer.push_back( AssetRegistryLayer{ "test", { AssetRegistryEntry{ guid, pPath } } } );
            return AssetRegistry::install( std::move( *AssetRegistry::merge( listLayer ) ) );
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

/** @brief [AssetRefTest] 내장 이름은 철자 그대로만 받는다 — 다른 철자 · 모르는 이름은 폴백 없이 오류이고, 파일 경로는 접미사로 가른다 */
SW_TEST_CASE( AssetRefTest, BuiltinNameIsExactAndUnknownNameIsALoadError )
{
    AssetRef<AssetRefSampleBuiltinTarget> shape;
    shape = "Box";
    SW_EXPECT_TRUE( shape == "Box" );
    SW_EXPECT_TRUE( shape.isBuiltin() );

    shape = "common/shapes/a.refshape";
    SW_EXPECT_TRUE( shape == "common/shapes/a.refshape" );
    SW_EXPECT_FALSE( shape.isBuiltin() );

    SW_EXPECT_LOG( "AssetRef.WrongType" );
    shape = "box";
    SW_EXPECT_TRUE( shape == "common/shapes/a.refshape" );

    SW_EXPECT_LOG( "AssetRef.WrongType" );
    shape = "None";
    SW_EXPECT_TRUE( shape == "common/shapes/a.refshape" );

    // 내장 이름은 그 종류의 것만이다 — 다른 종류의 참조는 같은 글을 받지 않는다.
    AssetRef<AssetRefSampleTarget> reference;
    SW_EXPECT_LOG( "AssetRef.WrongType" );
    reference = "Box";
    SW_EXPECT_TRUE( reference.empty() );
}

/** @brief [AssetRefTest] 실행 중에 만드는 자원의 경로(텍스처의 렌더 텍스처)는 접미사가 없어도 받고 파일로 보지 않는다 — 규칙 밖의 경로는 오류다 */
SW_TEST_CASE( AssetRefTest, RuntimePathIsAccepted )
{
    AssetRef<AssetRefSampleRuntimeTarget> texture;
    texture = "refruntime/minimap";
    SW_EXPECT_TRUE( texture == "refruntime/minimap" );
    SW_EXPECT_TRUE( texture.isBuiltin() );

    texture = "common/textures/a.reftexture";
    SW_EXPECT_TRUE( texture == "common/textures/a.reftexture" );
    SW_EXPECT_FALSE( texture.isBuiltin() );

    SW_EXPECT_LOG( "AssetRef.WrongType" );
    texture = "other/minimap";
    SW_EXPECT_TRUE( texture == "common/textures/a.reftexture" );
}

/** @brief [AssetRefTest] 접미사가 없는 카탈로그는 종류의 경로 규칙으로 가른다 — 규칙 밖의 파일은 오류다 */
SW_TEST_CASE( AssetRefTest, CatalogPathRuleDecidesTheKind )
{
    AssetRef<AssetRefSampleCatalog> catalog;
    catalog = "game/farm/data/refsample.xml";
    SW_EXPECT_TRUE( catalog == "game/farm/data/refsample.xml" );

    SW_EXPECT_LOG( "AssetRef.WrongType" );
    catalog = "game/farm/data/crops.xml";
    SW_EXPECT_TRUE( catalog == "game/farm/data/refsample.xml" );

    SW_EXPECT_LOG( "AssetRef.WrongType" );
    catalog = "common/data/refsample.xml";
    SW_EXPECT_TRUE( catalog == "game/farm/data/refsample.xml" );
}

/** @brief [AssetRefTest] 문서를 읽는 공통 길(JSON)도 내장 이름 · 카탈로그 규칙을 같은 처리기로 본다 */
SW_TEST_CASE( AssetRefTest, BuiltinAndCatalogInADocument )
{
    AssetRefSample sample;
    SW_REQUIRE_TRUE( JSONSerializer::deserialize( &sample, *AssetRefSample::StaticType(), R"({"_shape":"Ball","_catalog":"game/x/data/refsample.xml"})" ) );
    SW_EXPECT_TRUE( sample._shape == "Ball" );
    SW_EXPECT_TRUE( sample._catalog == "game/x/data/refsample.xml" );

    AssetRefSample wrongShape;
    SW_EXPECT_LOG( "AssetRef.WrongType" );
    (void)JSONSerializer::deserialize( &wrongShape, *AssetRefSample::StaticType(), R"({"_shape":"Cube"})" );
    SW_EXPECT_TRUE( wrongShape._shape.empty() );

    AssetRefSample wrongCatalog;
    SW_EXPECT_LOG( "AssetRef.WrongType" );
    (void)JSONSerializer::deserialize( &wrongCatalog, *AssetRefSample::StaticType(), R"({"_catalog":"game/x/data/crops.xml"})" );
    SW_EXPECT_TRUE( wrongCatalog._catalog.empty() );
}

/** @brief [AssetRefTest] 파일 형식은 경로 글 그대로다 — JSON · 바이너리로 왕복하고, 시퀀스 원소도 같은 처리기를 지난다 */
SW_TEST_CASE( AssetRefTest, RoundTrips )
{
    AssetRefSample source;
    source._reference = "common/data/a.reftarget.xml";
    source._listReference.emplace_back( string_view{ "common/data/b.reftarget.xml" } );

    const string json = JSONSerializer::serialize( &source, *AssetRefSample::StaticType() ).value();
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

/** @brief [AssetRefTest] 저장 꼴은 `guid:<32자>` 다 — 읽기가 색인으로 경로를 풀고, 쓰기는 GUID 를 적는다(옮겨도 참조가 그대로다) */
SW_TEST_CASE( AssetRefTest, GUIDReferenceResolvesThroughTheIndexAndWritesTheGUID )
{
    const AssetGUID     guid     = *AssetGUID::tryParse( "0000000000000000000000000000a5e1" );
    AssetRegistry       previous = XTestUtil::installOne( guid, "common/data/moved/g.reftarget.xml" );
    const hashed_string typeName( AssetRefBase::kTypeName );

    AssetRef<AssetRefSampleTarget> reference;
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &reference }, typeName, "guid:0000000000000000000000000000a5e1", XTestUtil::getContext() ) );
    SW_EXPECT_TRUE( reference == "common/data/moved/g.reftarget.xml" );
    SW_EXPECT_TRUE( reference.getGUID() == guid );
    SW_EXPECT_EQUAL( string( "guid:0000000000000000000000000000a5e1" ), SerializerUtil::formatValueText( ValueAddress{ &reference }, typeName, XTestUtil::getContext() ) );

    (void)AssetRegistry::install( std::move( previous ) );
}

/** @brief [AssetRefTest] 색인에 없는 GUID · 틀린 GUID 글은 로드 오류이고 값을 바꾸지 않는다 — 다른 에셋으로 대신하지 않는다 */
SW_TEST_CASE( AssetRefTest, UnknownOrMalformedGUIDIsALoadError )
{
    const AssetGUID                guid     = *AssetGUID::tryParse( "0000000000000000000000000000a5e2" );
    AssetRegistry                  previous = XTestUtil::installOne( guid, "common/data/k.reftarget.xml" );
    const hashed_string            typeName( AssetRefBase::kTypeName );
    AssetRef<AssetRefSampleTarget> reference;
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &reference }, typeName, "guid:0000000000000000000000000000a5e2", XTestUtil::getContext() ) );

    SW_EXPECT_LOG( "AssetRef.UnknownGUID" );
    SW_EXPECT_FALSE( SerializerUtil::parseTextValue( MutableValueAddress{ &reference }, typeName, "guid:0000000000000000000000000000dead", XTestUtil::getContext() ) );
    SW_EXPECT_LOG( "AssetRef.InvalidGUID" );
    SW_EXPECT_FALSE( SerializerUtil::parseTextValue( MutableValueAddress{ &reference }, typeName, "guid:0000000000000000000000000000A5E2", XTestUtil::getContext() ) );
    SW_EXPECT_TRUE( reference == "common/data/k.reftarget.xml" );
    SW_EXPECT_TRUE( reference.getGUID() == guid );

    (void)AssetRegistry::install( std::move( previous ) );
}

/** @brief [AssetRefTest] GUID 가 가리키는 에셋도 대상 종류가 아니면 로드 오류다 */
SW_TEST_CASE( AssetRefTest, GUIDOfAnotherKindIsALoadError )
{
    const AssetGUID                guid     = *AssetGUID::tryParse( "0000000000000000000000000000a5e3" );
    AssetRegistry                  previous = XTestUtil::installOne( guid, "common/materials/m.material" );
    AssetRef<AssetRefSampleTarget> reference;
    SW_EXPECT_LOG( "AssetRef.WrongType" );
    SW_EXPECT_FALSE( SerializerUtil::parseTextValue( MutableValueAddress{ &reference }, hashed_string( AssetRefBase::kTypeName ), "guid:0000000000000000000000000000a5e3", XTestUtil::getContext() ) );
    SW_EXPECT_TRUE( reference.empty() );
    (void)AssetRegistry::install( std::move( previous ) );
}

/**
 * @brief [AssetRefTest] 옮기는 동안: 옛 경로 글도 읽고 색인에서 GUID 를 찾아 둔다 — 다시 쓰면 GUID 로 적힌다. 내장 이름 · 색인에 없는 경로는 글 그대로 쓴다
 */
SW_TEST_CASE( AssetRefTest, PathTextFindsItsGUIDSoTheNextSaveWritesTheGUID )
{
    const AssetGUID     guid     = *AssetGUID::tryParse( "0000000000000000000000000000a5e4" );
    AssetRegistry       previous = XTestUtil::installOne( guid, "common/data/p.reftarget.xml" );
    const hashed_string typeName( AssetRefBase::kTypeName );

    AssetRef<AssetRefSampleTarget> reference;
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &reference }, typeName, "common/data/p.reftarget.xml", XTestUtil::getContext() ) );
    SW_EXPECT_TRUE( reference.getGUID() == guid );
    SW_EXPECT_EQUAL( string( "guid:0000000000000000000000000000a5e4" ), reference.toText() );

    reference = "common/data/unindexed.reftarget.xml";
    SW_EXPECT_FALSE( reference.getGUID().has_value() );
    SW_EXPECT_EQUAL( string( "common/data/unindexed.reftarget.xml" ), reference.toText() );

    AssetRef<AssetRefSampleBuiltinTarget> shape;
    shape = "Box";
    SW_EXPECT_FALSE( shape.getGUID().has_value() );
    SW_EXPECT_EQUAL( string( "Box" ), shape.toText() );

    (void)AssetRegistry::install( std::move( previous ) );
}

/**
 * @brief [AssetRefTest] 파서가 모은 칸 형식 이름("AssetRef")은 이름 형식이다 — 빈 글을 적은 필수 참조(`PROPERTY( Required )` 의 `_prefab=""`)를 필수 판정이 놓치지 않는다
 * @details 예전 판정은 "AssetRef<" 철자만 봐, 실제 칸 형식 이름(`AssetRefBase::kTypeName`)이면 적힌 빈 글을 값으로 보고 통과시켰다.
 */
SW_TEST_CASE( AssetRefTest, EmptyRequiredReferenceIsMissing )
{
    const PropertyInfo& prop = XTestUtil::getReference();
    SW_REQUIRE_TRUE( prop._typeName == hashed_string( AssetRefBase::kTypeName ) );
    SW_EXPECT_TRUE( SerializerUtil::isNameValueType( prop._typeName ) );
}
