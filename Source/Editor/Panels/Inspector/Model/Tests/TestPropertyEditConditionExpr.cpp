#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Inspector/Model/PropertyEditConditionExpr.h"

#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/Tests/ReflectionTestTypes.h"
#include "Engine/Reflection/TextAddress.h"

// PropertyEditConditionTest — 인스펙터의 EditCondition 판정. 다른 프로퍼티 값을 따라 막고 · 숨긴다(엔진 테스트 타입 `DisplayMetaActor` 로 본다).

namespace
{
    /** @brief @p pName 프로퍼티의 판정입니다. 프로퍼티가 있는지는 부르는 쪽이 먼저 본다(없으면 `Enabled`). */
    sw::PropertyEditState getState( const sw::TypeInfo& type, const sw::DisplayMetaActor& actor, const utf8* pName )
    {
        const sw::PropertyInfo* pProp = type.findPropertyInHierarchy( sw::hashed_string( pName ) );
        return pProp != nullptr ? sw::PropertyEditConditionExpr::getEditState( *pProp, sw::ObjectAddress{ &actor } ) : sw::PropertyEditState::Enabled;
    }
} // namespace

/**
 * @brief [PropertyEditConditionTest] EditCondition 이 다른 프로퍼티 값을 따라 막고 · 숨긴다 — bool · 부정 · 열거형 같음 · 다름
 */
SW_TEST_CASE( PropertyEditConditionTest, EditConditionFollowsTheOtherProperty )
{
    const sw::TypeInfo* pType = sw::DisplayMetaActor::StaticType();
    SW_REQUIRE_NOT_NULL( pType );
    const sw::TypeInfo&  type = *pType;
    sw::DisplayMetaActor actor;
    for ( const utf8* pName : { "_speed", "_fallback", "_orbitRadius", "_blend", "_height" } )
        SW_REQUIRE_NOT_NULL( type.findPropertyInHierarchy( sw::hashed_string( pName ) ) );

    SW_EXPECT_TRUE( getState( type, actor, "_speed" ) == sw::PropertyEditState::Disabled );
    SW_EXPECT_TRUE( getState( type, actor, "_fallback" ) == sw::PropertyEditState::Enabled );
    SW_EXPECT_TRUE( getState( type, actor, "_orbitRadius" ) == sw::PropertyEditState::Disabled );
    SW_EXPECT_TRUE( getState( type, actor, "_blend" ) == sw::PropertyEditState::Disabled );
    SW_EXPECT_TRUE( getState( type, actor, "_height" ) == sw::PropertyEditState::Enabled );

    actor._bEnabled = SW_TRUE;
    actor._mode     = sw::DisplayMetaMode::Orbit;
    SW_EXPECT_TRUE( getState( type, actor, "_speed" ) == sw::PropertyEditState::Enabled );
    SW_EXPECT_TRUE( getState( type, actor, "_fallback" ) == sw::PropertyEditState::Hidden );
    SW_EXPECT_TRUE( getState( type, actor, "_orbitRadius" ) == sw::PropertyEditState::Enabled );
    SW_EXPECT_TRUE( getState( type, actor, "_blend" ) == sw::PropertyEditState::Enabled );

    actor._mode = sw::DisplayMetaMode::Follow;
    SW_EXPECT_TRUE( getState( type, actor, "_orbitRadius" ) == sw::PropertyEditState::Disabled );
    SW_EXPECT_TRUE( getState( type, actor, "_blend" ) == sw::PropertyEditState::Enabled );
}
