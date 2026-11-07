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
        static MaterialParameterCollectionDef makeDesc()
        {
            MaterialParameterCollectionDef desc;
            MaterialCollectionParameterDef tint;
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

/** @brief [MaterialParameterCollectionTest] 다시 읽은 파일 값으로 제자리에서 바꾸면 세대가 오르고 런타임 값 · 없어진 이름은 파일을 따른다(핫 리로드) */
SW_TEST_CASE( MaterialParameterCollectionTest, ReplaceValuesFollowsTheReloadedFile )
{
    MaterialParameterCollection collection{ Util::makeDesc() };
    const hashed_string         tint( "Tint" );
    SW_REQUIRE_TRUE( collection.setValue( tint, float4{ 0.0f, 0.0f, 0.0f, 1.0f } ) );
    const uint32 before = collection.getGeneration();

    MaterialParameterCollectionDef reloaded;
    MaterialCollectionParameterDef wind;
    wind._name  = "Wind";
    wind._value = float4{ 3.0f, 0.0f, 0.0f, 0.0f };
    reloaded._listParameter.push_back( wind );
    collection.replaceValues( reloaded );

    SW_EXPECT_TRUE( collection.getGeneration() != before );
    SW_EXPECT_TRUE( collection.findValue( tint ) == nullptr );
    const float4* pWind = collection.findValue( hashed_string( "Wind" ) );
    SW_REQUIRE_NOT_NULL( pWind );
    SW_EXPECT_NEAR( 3.0f, pWind->_x, 0.0f );

    // 같은 파일을 다시 읽어도 세대는 오른다 — 쥔 머티리얼이 한 번 더 패킹할 뿐 틀린 값이 남지 않는다.
    const uint32 second = collection.getGeneration();
    collection.replaceValues( reloaded );
    SW_EXPECT_TRUE( collection.getGeneration() != second );
}

/** @brief [MaterialParameterCollectionTest] 이름이 비거나 겹치면 읽기 오류다 */
SW_TEST_CASE( MaterialParameterCollectionTest, ValidateRejectsBadNames )
{
    SW_EXPECT_LOG( "MaterialParameterCollection.BadName" );
    MaterialParameterCollectionDef desc = Util::makeDesc();
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
    CollectionParentSource source{ R"(<MaterialParameterCollectionDef><_listParameter>
        <MaterialCollectionParameterDef _name="Tint" _value="1, 1, 1, 1" />
        <MaterialCollectionParameterDef _name="Wind" _value="2, 0, 0, 0" />
    </_listParameter></MaterialParameterCollectionDef>)" };
    string                 flat;
    SW_REQUIRE_TRUE( DataInheritance::flatten( R"(<MaterialParameterCollectionDef _base="parent.matparams.xml"><_listParameter>
        <MaterialCollectionParameterDef _name="Tint" _value="1, 0, 0, 1" />
    </_listParameter></MaterialParameterCollectionDef>)",
                                               "child", DataInheritanceSchema::makeFromType( *MaterialParameterCollectionDef::StaticType() ), source, flat ) );
    MaterialParameterCollectionDef desc;
    SW_REQUIRE_TRUE( XMLSerializer::deserialize( &desc, *MaterialParameterCollectionDef::StaticType(), flat ) );
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
