// PrefabRequiredFieldsTest — 프리팹 기반 + override 를 합친 상태에서 필수 칸(Required)을 본다. 필수 칸을 가진 컴포넌트(애니메이션 단)를 쓰므로
// 월드 단(Object/Prefab) 테스트가 아니라 여기에 둔다 — 아래 층 부분은 위 층 헤더를 include 하지 못한다(EngineLayers.toml).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Component/LocomotionWarpingComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"

using namespace sw;

namespace
{
    string makeOverrides( string_view instance, string_view base )
    {
        string overrides;
        SW_EXPECT_TRUE( PrefabOverrides::computeOverrides( instance, base, overrides ).has_value() );
        return overrides;
    }
} // namespace

/**
 * @brief [PrefabRequiredFieldsTest] 필수 칸(`PROPERTY( Required )`)은 합친 상태에서 본다 — override 가 다른 칸만 적어도 기반에 있으면 읽고,
 *        기반에도 없으면 오류 id 를 남기고 읽지 않는다(초기값이 데이터를 대신하지 않는다)
 */
SW_TEST_CASE( PrefabRequiredFieldsTest, RequiredFieldsAreCheckedOnTheMergedState )
{
    static constexpr const utf8* kWritten = R"(<GameObject _schemaVersion="0" _name="Walker" _bActive="true"><_listComponent>)"
                                            R"(<LocomotionWarpingComponent _componentName="LocomotionWarpingComponent" _bStrideWarping="true" _speedCurve="Speed"/>)"
                                            R"(</_listComponent></GameObject>)";
    static constexpr const utf8* kMissing = R"(<GameObject _schemaVersion="0" _name="Walker" _bActive="true"><_listComponent>)"
                                            R"(<LocomotionWarpingComponent _componentName="LocomotionWarpingComponent" _bStrideWarping="true"/>)"
                                            R"(</_listComponent></GameObject>)";
    string                       instance = kWritten;
    instance.replace( instance.find( "_bStrideWarping=\"true\"" ), 22, "_bStrideWarping=\"false\"" );
    const string overrides = makeOverrides( instance, kWritten );
    SW_REQUIRE_TRUE( overrides.find( "_speedCurve" ) == string::npos ); // override 는 바꾼 칸만 적는다

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Walker" ) );
    SW_REQUIRE_NOT_NULL( pObject );

    string merged;
    SW_REQUIRE_TRUE( PrefabOverrides::makeInstanceState( kWritten, overrides, "Walker", merged ) );
    SW_EXPECT_TRUE( ObjectStateSerializer::loadFromXMLString( pObject, merged, ObjectLoadContext{ SerializePurpose::Authoring } ) );
    SW_EXPECT_NOT_NULL( pObject->getComponent<LocomotionWarpingComponent>() );

    SW_EXPECT_LOG( "ObjectStateSerializer.RequiredMissing" );
    SW_REQUIRE_TRUE( PrefabOverrides::makeInstanceState( kMissing, overrides, "Walker", merged ) );
    SW_EXPECT_FALSE( ObjectStateSerializer::loadFromXMLString( pObject, merged, ObjectLoadContext{ SerializePurpose::Authoring } ) );

    // 적혀 있어도 빈 이름은 빠진 것이다(에디터가 빈 칸을 속성으로 적는 길) — XML · JSON 이 같은 판정을 지난다.
    string empty = kWritten;
    empty.replace( empty.find( "_speedCurve=\"Speed\"" ), 19, "_speedCurve=\"\"" );
    SW_EXPECT_FALSE( ObjectStateSerializer::loadFromXMLString( pObject, empty, ObjectLoadContext{ SerializePurpose::Authoring } ) );
    SW_EXPECT_FALSE( ObjectStateSerializer::loadFromJSONString( pObject, R"({"_schemaVersion":0,"_name":"Walker","_listComponent":[{"LocomotionWarpingComponent":{"_speedCurve":""}}]})",
                                                                ObjectLoadContext{ SerializePurpose::Authoring } ) );
}
