#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialParameterCollection.h"
#include "Engine/Graphics/Material/MaterialXML.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/XML/XMLDocument.h"

using namespace sw;

namespace
{
    /** @brief 부모 묶음 글을 한 벌 돌려주는 곳입니다. */
    class CollectionParentSource final : public IDataInheritanceSource
    {
    public:
        explicit CollectionParentSource( string_view text )
            : _text{ text }
        {
        }
        [[nodiscard]] bool readText( string_view /*assetID*/, string& outText ) override
        {
            outText = _text;
            return true;
        }

    private:
        string _text;
    };

    struct MaterialParameterCollectionTestUtil
    {
        static MaterialParameterCollectionDesc makeDesc()
        {
            MaterialParameterCollectionDesc desc;
            MaterialCollectionParameterDesc tint;
            tint._name  = "Tint";
            tint._value = float4{ 1.0f, 0.5f, 0.25f, 1.0f };
            desc._listParameter.push_back( tint );
            return desc;
        }
    };
    using Util = MaterialParameterCollectionTestUtil;
} // namespace

/** @brief [MaterialParameterCollectionTest] 값을 바꾸면 세대가 오르고, 같은 값은 그대로이며, 없는 이름은 오류다 */
SW_TEST_CASE( MaterialParameterCollectionTest, SetValueBumpsTheGeneration )
{
    SW_EXPECT_LOG( "MaterialParameterCollection.UnknownParameter" );
    MaterialParameterCollection collection{ Util::makeDesc() };
    const hashed_string         tint( "Tint" );
    const uint32                first = collection.getGeneration();
    const float4*               pTint = collection.findValue( tint );
    SW_REQUIRE_NOT_NULL( pTint );
    SW_EXPECT_NEAR( 0.5f, pTint->_y, 0.0f );

    SW_EXPECT_TRUE( collection.setValue( tint, *pTint ) );
    SW_EXPECT_EQUAL( first, collection.getGeneration() );
    SW_EXPECT_TRUE( collection.setValue( tint, float4{ 0.0f, 0.0f, 1.0f, 1.0f } ) );
    SW_EXPECT_TRUE( collection.getGeneration() != first );
    SW_EXPECT_NEAR( 1.0f, collection.findValue( tint )->_z, 0.0f );

    SW_EXPECT_FALSE( collection.setValue( hashed_string( "Nope" ), float4{} ) );
    SW_EXPECT_TRUE( collection.findValue( hashed_string( "Nope" ) ) == nullptr );
}

/** @brief [MaterialParameterCollectionTest] 이름이 비거나 겹치면 읽기 오류다 */
SW_TEST_CASE( MaterialParameterCollectionTest, ValidateRejectsBadNames )
{
    SW_EXPECT_LOG( "MaterialParameterCollection.BadName" );
    MaterialParameterCollectionDesc desc = Util::makeDesc();
    SW_EXPECT_TRUE( desc.validate( "test" ) );
    desc._listParameter.push_back( desc._listParameter[0] );
    SW_EXPECT_FALSE( desc.validate( "test" ) );
}

/** @brief [MaterialParameterCollectionTest] 프로퍼티 값 글은 타입의 채널 수만큼이고 머티리얼 값 글과 같은 꼴이다 */
SW_TEST_CASE( MaterialParameterCollectionTest, PropertyTextHasTheTypeChannels )
{
    const float4 value{ 0.5f, 0.25f, 2.0f, 1.0f };
    SW_EXPECT_EQUAL( string( "0.5" ), MaterialParameterCollection::makePropertyText( value, 1u ) );
    SW_EXPECT_EQUAL( string( "0.5 0.25 2 1" ), MaterialParameterCollection::makePropertyText( value, 4u ) );
}

/** @brief [MaterialParameterCollectionTest] 묶음 변형은 값을 이름으로 합친다 */
SW_TEST_CASE( MaterialParameterCollectionTest, VariantMergesByName )
{
    CollectionParentSource source{ R"(<MaterialParameterCollectionDesc><_listParameter>
        <MaterialCollectionParameterDesc _name="Tint" _value="1, 1, 1, 1" />
        <MaterialCollectionParameterDesc _name="Wind" _value="2, 0, 0, 0" />
    </_listParameter></MaterialParameterCollectionDesc>)" };
    string                 flat;
    SW_REQUIRE_TRUE( DataInheritance::flatten( R"(<MaterialParameterCollectionDesc _base="parent.matparams.xml"><_listParameter>
        <MaterialCollectionParameterDesc _name="Tint" _value="1, 0, 0, 1" />
    </_listParameter></MaterialParameterCollectionDesc>)",
                                               "child", DataInheritanceSchema::makeFromType( *MaterialParameterCollectionDesc::StaticType() ), source, flat ) );
    MaterialParameterCollectionDesc desc;
    SW_REQUIRE_TRUE( XMLSerializer::deserialize( &desc, *MaterialParameterCollectionDesc::StaticType(), flat ) );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( desc._listParameter.size() ) );
    SW_EXPECT_NEAR( 0.0f, desc._listParameter[0]._value._y, 0.0f );
    SW_EXPECT_NEAR( 2.0f, desc._listParameter[1]._value._x, 0.0f );
}

/** @brief [MaterialParameterCollectionTest] 머티리얼 프로퍼티의 묶음 칸을 읽는다 */
SW_TEST_CASE( MaterialParameterCollectionTest, MaterialPropertyReadsTheBinding )
{
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( R"(<item name="_color" type="Color" defaultValue="1 1 1 1" collection="game/x.matparams.xml" collectionParameter="Tint" />)" ) );
    MaterialProperty property;
    SW_REQUIRE_TRUE( MaterialXML::parsePropertyNode( doc.getRoot(), property ) );
    SW_EXPECT_EQUAL( string( "game/x.matparams.xml" ), property._collectionPath );
    SW_EXPECT_EQUAL( string( "Tint" ), property._collectionParameter );
}
