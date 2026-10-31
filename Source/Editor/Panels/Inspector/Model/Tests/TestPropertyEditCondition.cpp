#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Inspector/Model/PropertyEditCondition.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/Tests/ReflectionTestTypes.h"
#include "Engine/Reflection/TextAddress.h"
#include "Engine/Reflection/TypeRegistry.h"

// PropertyEditConditionTest — 인스펙터의 EditCondition 판정. 다른 프로퍼티 값을 따라 막고 · 숨긴다(엔진 테스트 타입 `DisplayMetaActor` 로 본다).

namespace
{
    /** @brief @p pName 프로퍼티의 판정입니다. 프로퍼티가 있는지는 부르는 쪽이 먼저 본다(없으면 `Enabled`). */
    sw::PropertyEditState getState( const sw::TypeInfo& type, const sw::DisplayMetaActor& actor, const utf8* pName )
    {
        const sw::PropertyInfo* pProp = type.findPropertyInHierarchy( sw::hashed_string( pName ) );
        return pProp != nullptr ? sw::PropertyEditCondition::getEditState( type, *pProp, sw::ObjectAddress{ &actor } ) : sw::PropertyEditState::Enabled;
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

    actor._bEnabled = true;
    actor._mode     = sw::DisplayMetaMode::Orbit;
    SW_EXPECT_TRUE( getState( type, actor, "_speed" ) == sw::PropertyEditState::Enabled );
    SW_EXPECT_TRUE( getState( type, actor, "_fallback" ) == sw::PropertyEditState::Hidden );
    SW_EXPECT_TRUE( getState( type, actor, "_orbitRadius" ) == sw::PropertyEditState::Enabled );
    SW_EXPECT_TRUE( getState( type, actor, "_blend" ) == sw::PropertyEditState::Enabled );

    actor._mode = sw::DisplayMetaMode::Follow;
    SW_EXPECT_TRUE( getState( type, actor, "_orbitRadius" ) == sw::PropertyEditState::Disabled );
    SW_EXPECT_TRUE( getState( type, actor, "_blend" ) == sw::PropertyEditState::Enabled );

    // 풀지 못하는 식은 막지 않고 이유를 말한다
    sw::PropertyInfo broken          = *type.findPropertyInHierarchy( "_speed" );
    broken._metadata->_editCondition = "_mode == Sideways";
    sw::PropertyEditConditionExpr expr;
    sw::string                    error;
    SW_EXPECT_FALSE( sw::PropertyEditCondition::parse( type, broken, expr, error ) );
    SW_EXPECT_TRUE_MSG( error.find( "Sideways" ) != sw::string::npos, error.c_str() );
    broken._metadata->_editCondition = "_noSuchProperty";
    SW_EXPECT_FALSE( sw::PropertyEditCondition::parse( type, broken, expr, error ) );
    SW_EXPECT_TRUE( sw::PropertyEditCondition::getEditState( type, broken, sw::ObjectAddress{ &actor } ) == sw::PropertyEditState::Enabled );
}

/**
 * @brief [PropertyEditConditionTest] 등록된 모든 타입의 EditCondition 이 풀린다 — 이름은 기반에 있을 수 있어 파서가 아니라 여기서 본다
 */
SW_TEST_CASE( PropertyEditConditionTest, EveryEditConditionResolves )
{
    sw::string failures;
    uint32     checkedCount = 0;
    sw::engine::getSubsystem<sw::TypeRegistry>().forEachType( [&failures, &checkedCount]( const sw::TypeInfo& type )
    {
        for ( const sw::PropertyInfo& prop : type.getPropertiesWithBase() )
        {
            if ( prop._metadata->_editCondition.empty() )
                continue;
            ++checkedCount;
            sw::PropertyEditConditionExpr expr;
            sw::string                    error;
            if ( sw::PropertyEditCondition::parse( type, prop, expr, error ) == false )
                failures += sw::string( type._fullyQualifiedName.c_str() ) + "::" + prop._name.c_str() + ": " + error + "\n";
        }
    } );
    SW_EXPECT_TRUE( checkedCount >= 4 );
    SW_EXPECT_TRUE_MSG( failures.empty(), failures.c_str() );
}
