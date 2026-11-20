#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"
#include "Engine/Serialization/XML/XMLDocument.h"

using namespace sw;

namespace
{
    struct PrefabOverridesTestUtil
    {
        static constexpr const utf8* kBase = R"(<GameObject _schemaVersion="0" _name="Base" _bActive="true" _tag="a">
            <_listComponent>
                <MeshComponent _componentName="Body" _mesh="body.mesh" _scale="1"/>
                <MeshComponent _componentName="Body" _mesh="second.mesh" _scale="1"/>
                <LightComponent _intensity="1"><Color r="1"/></LightComponent>
            </_listComponent>
            <Extra value="1"/>
        </GameObject>)";

        /** @brief `kBase` 의 @p index 번째 컴포넌트 키입니다 — id 를 적지 않은 컴포넌트는 이름과 순번에서 정한다(`ComponentStableKey::resolveLocalIDs`). */
        static string keyOfBase( size_t index )
        {
            vector<ComponentLocalIDSlot> listSlot{
                {          "Body", 0, 0},
                {          "Body", 0, 0},
                {"LightComponent", 0, 0}
            };
            ComponentStableKey::resolveLocalIDs( listSlot );
            return ComponentStableKey::formatKey( listSlot[index]._resolved );
        }

        static string makeOverrides( string_view instance, string_view base )
        {
            string overrides;
            SW_EXPECT_TRUE( PrefabOverrides::computeOverrides( instance, base, overrides ).has_value() );
            return overrides;
        }

        /** @brief 만든 상태를 읽어 @p index 번째 컴포넌트를 찾습니다. */
        static XMLNode findComponent( const XMLDocument& doc, uint32 index )
        {
            XMLNode child = doc.getRoot().findChild( "_listComponent" ).findChild();
            for ( uint32 skip = 0; skip < index && child.isValid(); ++skip )
            {
                child = child.findNextSibling();
            }
            return child;
        }

        /** @brief 기준에 @p overrides 를 얹은 상태를 읽어 둡니다. */
        static void build( XMLDocument& outDoc, string_view overrides, string_view instanceName = "Inst" )
        {
            string state;
            SW_REQUIRE_TRUE( PrefabOverrides::makeInstanceState( kBase, overrides, instanceName, state ) );
            SW_REQUIRE_TRUE( outDoc.parse( state ) );
        }

        static uint32 countComponents( const XMLDocument& doc )
        {
            uint32 count = 0;
            for ( XMLNode child = doc.getRoot().findChild( "_listComponent" ).findChild(); child.isValid(); child = child.findNextSibling() )
            {
                ++count;
            }
            return count;
        }
    };
    using Util = PrefabOverridesTestUtil;
} // namespace

// ---------------------------------------------------------------------- 차이 구하기

SW_TEST_CASE( PrefabOverridesTest, SameStateHasNoOverrides )
{
    SW_EXPECT_TRUE( Util::makeOverrides( Util::kBase, Util::kBase ).empty() );
}

// 이름과 판은 엔티티 · 직렬화기의 것이라 덮어쓰기가 아니다.
SW_TEST_CASE( PrefabOverridesTest, NameAndSchemaAreNotOverrides )
{
    string instance = Util::kBase;
    instance.replace( instance.find( "_name=\"Base\"" ), 12, "_name=\"Inst\"" );
    SW_EXPECT_TRUE( Util::makeOverrides( instance, Util::kBase ).empty() );
}

SW_TEST_CASE( PrefabOverridesTest, BrokenInputsAreRefused )
{
    SW_EXPECT_LOG( "Xml.ParseError" );
    string               overrides;
    const expected<void> badInstance = PrefabOverrides::computeOverrides( "<a", Util::kBase, overrides );
    SW_REQUIRE_FALSE( badInstance.has_value() );
    SW_EXPECT_TRUE( badInstance.error().message().find( "instance state" ) != string_view::npos );
    const expected<void> badBase = PrefabOverrides::computeOverrides( Util::kBase, "<a", overrides );
    SW_REQUIRE_FALSE( badBase.has_value() );
    SW_EXPECT_TRUE( badBase.error().message().find( "base state" ) != string_view::npos );
    string state;
    SW_EXPECT_FALSE( PrefabOverrides::makeInstanceState( "<a", "", "x", state ) );
    SW_EXPECT_FALSE( PrefabOverrides::makeInstanceState( Util::kBase, "<a", "x", state ) );
}

SW_TEST_CASE( PrefabOverridesTest, WrongRootIsRefusedWithAWarning )
{
    SW_EXPECT_LOG( "PrefabOverrides.PrefabOverridesNoRoot" );
    string state;
    SW_EXPECT_FALSE( PrefabOverrides::makeInstanceState( Util::kBase, "<NotOverrides/>", "x", state ) );
}

// ---------------------------------------------------------------------- 왕복

SW_TEST_CASE( PrefabOverridesTest, NoOverridesRebuildsThePrefabUnderTheInstanceName )
{
    XMLDocument doc;
    Util::build( doc, "" );
    SW_EXPECT_TRUE( string_view( doc.getRoot().findAttribute( "_name" ) ) == "Inst" );
    SW_EXPECT_EQUAL( 3u, Util::countComponents( doc ) );
    SW_EXPECT_TRUE( doc.getRoot().findChild( "Extra" ).isValid() );
}

SW_TEST_CASE( PrefabOverridesTest, ChangedPropertiesRoundTrip )
{
    string instance = Util::kBase;
    instance.replace( instance.find( "_tag=\"a\"" ), 8, "_tag=\"b\"" );
    instance.replace( instance.find( "_mesh=\"body.mesh\"" ), 17, "_mesh=\"gold.mesh\"" );
    const string overrides = Util::makeOverrides( instance, Util::kBase );
    SW_REQUIRE_FALSE( overrides.empty() );

    XMLDocument doc;
    Util::build( doc, overrides );
    SW_EXPECT_TRUE( string_view( doc.getRoot().findAttribute( "_tag" ) ) == "b" );
    const XMLNode first = Util::findComponent( doc, 0 );
    SW_EXPECT_TRUE( string_view( first.findAttribute( "_mesh" ) ) == "gold.mesh" );
    SW_EXPECT_TRUE( string_view( first.findAttribute( "_scale" ) ) == "1" ); // 덮어쓰지 않은 칸은 프리팹 값이다
    SW_EXPECT_TRUE( string_view( Util::findComponent( doc, 1 ).findAttribute( "_mesh" ) ) == "second.mesh" );

    // 다시 구해도 같은 덮어쓰기다(저장할 때마다 달라지지 않는다).
    string rebuilt;
    SW_REQUIRE_TRUE( PrefabOverrides::makeInstanceState( Util::kBase, overrides, "Inst", rebuilt ) );
    SW_EXPECT_TRUE( Util::makeOverrides( rebuilt, Util::kBase ) == overrides );
}

SW_TEST_CASE( PrefabOverridesTest, ChangedChildElementReplacesOnlyItself )
{
    string instance = Util::kBase;
    instance.replace( instance.find( "<Color r=\"1\"/>" ), 14, "<Color r=\"0.5\"/>" );
    XMLDocument doc;
    Util::build( doc, Util::makeOverrides( instance, Util::kBase ) );
    const XMLNode light = Util::findComponent( doc, 2 );
    SW_EXPECT_TRUE( string_view( light.findChild( "Color" ).findAttribute( "r" ) ) == "0.5" );
    SW_EXPECT_TRUE( string_view( light.findAttribute( "_intensity" ) ) == "1" );
}

SW_TEST_CASE( PrefabOverridesTest, RemovedComponentStaysRemoved )
{
    const string instance = R"(<GameObject _schemaVersion="0" _name="Base" _bActive="true" _tag="a">
        <_listComponent>
            <MeshComponent _componentName="Body" _mesh="body.mesh" _scale="1"/>
            <MeshComponent _componentName="Body" _mesh="second.mesh" _scale="1"/>
        </_listComponent>
        <Extra value="1"/>
    </GameObject>)";
    XMLDocument  doc;
    Util::build( doc, Util::makeOverrides( instance, Util::kBase ) );
    SW_EXPECT_EQUAL( 2u, Util::countComponents( doc ) );
    SW_EXPECT_TRUE( string_view( Util::findComponent( doc, 1 ).findAttribute( "_mesh" ) ) == "second.mesh" );
}

// 더한 컴포넌트는 그 자리에 들어간다. 순서가 바뀌면 같은 타입의 `타입#n` 키가 바뀐다.
SW_TEST_CASE( PrefabOverridesTest, AddedComponentKeepsItsPlace )
{
    const string instance = R"(<GameObject _schemaVersion="0" _name="Base" _bActive="true" _tag="a">
        <_listComponent>
            <AudioComponent _componentName="First"/>
            <MeshComponent _componentName="Body" _mesh="body.mesh" _scale="1"/>
            <AudioComponent _componentName="Middle"/>
            <MeshComponent _componentName="Body" _mesh="second.mesh" _scale="1"/>
            <LightComponent _intensity="1"><Color r="1"/></LightComponent>
        </_listComponent>
        <Extra value="1"/>
    </GameObject>)";
    XMLDocument  doc;
    Util::build( doc, Util::makeOverrides( instance, Util::kBase ) );
    SW_REQUIRE_EQUAL( 5u, Util::countComponents( doc ) );
    SW_EXPECT_TRUE( string_view( Util::findComponent( doc, 0 ).findAttribute( "_componentName" ) ) == "First" );
    SW_EXPECT_TRUE( string_view( Util::findComponent( doc, 1 ).findAttribute( "_mesh" ) ) == "body.mesh" );
    SW_EXPECT_TRUE( string_view( Util::findComponent( doc, 2 ).findAttribute( "_componentName" ) ) == "Middle" );
    SW_EXPECT_TRUE( string_view( Util::findComponent( doc, 3 ).findAttribute( "_mesh" ) ) == "second.mesh" );
}

// ---------------------------------------------------------------------- 프리팹이 바뀐 뒤

// 프리팹에서 사라진 컴포넌트의 덮어쓰기는 알리고 버린다.
SW_TEST_CASE( PrefabOverridesTest, OverrideOfAGoneComponentIsWarnedAndDropped )
{
    SW_EXPECT_LOG( "Prefab.OverrideComponentDropped" );
    const string overrides = R"(<PrefabOverrides><Override key="#00000000000000ff"><MeshComponent _mesh="x"/></Override></PrefabOverrides>)";
    XMLDocument  doc;
    Util::build( doc, overrides );
    SW_EXPECT_EQUAL( 3u, Util::countComponents( doc ) );
}

// 타입이 바뀐 컴포넌트에 옛 값을 얹지 않는다.
SW_TEST_CASE( PrefabOverridesTest, OverrideOfAnotherTypeIsDropped )
{
    SW_EXPECT_LOG( "Prefab.OverrideComponentDropped" );
    const string overrides = "<PrefabOverrides><Override key=\"" + Util::keyOfBase( 0 ) + "\"><LightComponent _mesh=\"x\"/></Override></PrefabOverrides>";
    XMLDocument  doc;
    Util::build( doc, overrides );
    SW_EXPECT_TRUE( string_view( Util::findComponent( doc, 0 ).findAttribute( "_mesh" ) ) == "body.mesh" );
}

// 앞의 컴포넌트가 사라져도 더한 컴포넌트는 버리지 않고 끝에 둔다.
SW_TEST_CASE( PrefabOverridesTest, AddedComponentSurvivesALostAnchor )
{
    const string overrides = R"(<PrefabOverrides><Add after="#00000000000000ff"><AudioComponent _componentName="Kept"/></Add></PrefabOverrides>)";
    XMLDocument  doc;
    Util::build( doc, overrides );
    SW_REQUIRE_EQUAL( 4u, Util::countComponents( doc ) );
    SW_EXPECT_TRUE( string_view( Util::findComponent( doc, 3 ).findAttribute( "_componentName" ) ) == "Kept" );
}

SW_TEST_CASE( PrefabOverridesTest, RemovingAGoneComponentIsHarmless )
{
    const string overrides = R"(<PrefabOverrides><Remove key="#00000000000000ff"/></PrefabOverrides>)";
    XMLDocument  doc;
    Util::build( doc, overrides );
    SW_EXPECT_EQUAL( 3u, Util::countComponents( doc ) );
}

/**
 * @brief [PrefabOverridesTest] 옛 순번 키(`이름#n`)는 받지 않는다 — 오류로 남기고 그 항목은 어느 컴포넌트에도 얹지 않는다
 * @details 순번은 앞 형제를 지우면 다른 컴포넌트를 가리킨다. 저장소의 데이터는 모두 지역 id 키로 옮겼다.
 */
SW_TEST_CASE( PrefabOverridesTest, LegacyIndexKeyIsAnError )
{
    SW_EXPECT_LOG( "PrefabOverrides.LegacyComponentKey" );
    SW_EXPECT_LOG( "Prefab.OverrideComponentDropped" );
    const string overrides = R"(<PrefabOverrides><Override key="Body#0"><MeshComponent _mesh="x"/></Override></PrefabOverrides>)";
    XMLDocument  doc;
    Util::build( doc, overrides );
    SW_EXPECT_TRUE( string_view( Util::findComponent( doc, 0 ).findAttribute( "_mesh" ) ) == "body.mesh" );
}

// ---------------------------------------------------------------------- 중첩 파트

SW_TEST_CASE( PrefabOverridesTest, PartOverridesRoundTrip )
{
    string overrides = R"(<PrefabOverrides><Remove key="#0000000000000002"/></PrefabOverrides>)";
    SW_REQUIRE_TRUE( PrefabOverrides::appendPartOverrides( overrides, {
                                                                          PrefabPartOverride{ "Arm/Hand", R"(<PrefabOverrides><Remove key="#0000000000000001"/></PrefabOverrides>)" }
    } ) );
    SW_EXPECT_TRUE( overrides.find( "#0000000000000002" ) != string::npos ); // 있던 것은 그대로다

    vector<PrefabPartOverride> listPart;
    SW_REQUIRE_TRUE( PrefabOverrides::collectPartOverrides( overrides, listPart ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listPart.size() );
    SW_EXPECT_TRUE( listPart[0]._partPath == "Arm/Hand" );
    SW_EXPECT_TRUE( listPart[0]._overrideXML.find( "#0000000000000001" ) != string::npos );

    string fromEmpty;
    SW_REQUIRE_TRUE( PrefabOverrides::appendPartOverrides( fromEmpty, {
                                                                          PrefabPartOverride{ "Leg", "<PrefabOverrides/>" }
    } ) );
    SW_REQUIRE_TRUE( PrefabOverrides::collectPartOverrides( fromEmpty, listPart ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, listPart.size() );
    SW_EXPECT_TRUE( PrefabOverrides::appendPartOverrides( fromEmpty, {} ) );
    SW_EXPECT_TRUE( PrefabOverrides::collectPartOverrides( "", listPart ) );
    SW_EXPECT_TRUE( listPart.empty() );
}

SW_TEST_CASE( PrefabOverridesTest, BrokenPartOverridesAreRefused )
{
    string overrides;
    SW_EXPECT_FALSE( PrefabOverrides::appendPartOverrides( overrides, {
                                                                          PrefabPartOverride{ "", "<PrefabOverrides/>" }
    } ) );
    SW_EXPECT_FALSE( PrefabOverrides::appendPartOverrides( overrides, {
                                                                          PrefabPartOverride{ "Leg", "<Other/>" }
    } ) );

    SW_EXPECT_LOG( "PrefabOverrides.NestedPartOverrideNo" );
    vector<PrefabPartOverride> listPart;
    SW_EXPECT_FALSE( PrefabOverrides::collectPartOverrides( R"(<PrefabOverrides><Part path="Leg"/></PrefabOverrides>)", listPart ) );
    SW_EXPECT_TRUE( listPart.empty() );
}

// ---------------------------------------------------------------------- 고정 지역 id(감사 A-2)

/**
 * @brief [PrefabOverridesTest] 덮어쓰기는 컴포넌트의 고정 지역 id(`_localID`)를 따른다 — 프리팹에서 앞 형제를 지워도 같은 컴포넌트에 얹힌다
 * @details 예전 키 `이름#n` 은 순번이라, 앞의 `Body` 를 지우면 `Body#1` 이 없어져 덮어쓰기가 버려지거나 다른 컴포넌트에 얹혔다(유니티 fileID · 언리얼 SCS 변수 GUID 자리).
 */
SW_TEST_CASE( PrefabOverridesTest, OverrideFollowsComponentWhenAnEarlierSiblingIsRemoved )
{
    const string base      = R"(<GameObject _schemaVersion="0" _name="Base">
        <_listComponent>
            <MeshComponent _componentName="Body" _localID="11" _mesh="body.mesh"/>
            <MeshComponent _componentName="Body" _localID="22" _mesh="second.mesh"/>
        </_listComponent>
    </GameObject>)";
    const string instance  = R"(<GameObject _schemaVersion="0" _name="Base">
        <_listComponent>
            <MeshComponent _componentName="Body" _localID="11" _mesh="body.mesh"/>
            <MeshComponent _componentName="Body" _localID="22" _mesh="changed.mesh"/>
        </_listComponent>
    </GameObject>)";
    const string overrides = Util::makeOverrides( instance, base );
    SW_REQUIRE_TRUE( overrides.find( "changed.mesh" ) != string::npos );

    // 프리팹에서 앞의 Body 를 지웠다 — 덮어쓰기는 id 22 를 따라간다.
    const string editedBase = R"(<GameObject _schemaVersion="0" _name="Base">
        <_listComponent>
            <MeshComponent _componentName="Body" _localID="22" _mesh="second.mesh"/>
        </_listComponent>
    </GameObject>)";
    string       state;
    SW_REQUIRE_TRUE( PrefabOverrides::makeInstanceState( editedBase, overrides, "Inst", state ) );
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( state ) );
    SW_REQUIRE_EQUAL( 1u, Util::countComponents( doc ) );
    SW_EXPECT_TRUE( string_view( Util::findComponent( doc, 0 ).findAttribute( "_mesh" ) ) == "changed.mesh" );
}

/** @brief [PrefabOverridesTest] 기본값을 뺀 글도 컴포넌트의 지역 id(`_localID` — 정체)를 남긴다 — 빼면 다시 읽은 부착 키(#16진)가 컴포넌트를 찾지 못했다 */
SW_TEST_CASE( PrefabOverridesTest, OmitDefaultValuesKeepsComponentLocalID )
{
    const string state = R"(<GameObject _schemaVersion="0" _name="Lamp"><_listComponent><SceneComponent _componentName="SceneComponent" _localID="9027683366464785044"/></_listComponent></GameObject>)";
    string       trimmed;
    SW_REQUIRE_TRUE( PrefabOverrides::omitDefaultValues( state, trimmed ) );
    SW_EXPECT_TRUE( trimmed.find( "_localID=\"9027683366464785044\"" ) != string::npos );
}
