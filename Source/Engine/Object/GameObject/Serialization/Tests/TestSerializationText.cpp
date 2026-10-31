// 직렬화 글 칸 — 글(`string`)은 끝 공백을 값으로 지키고 이름(`hashed_string`)은 자르며, JSON 컴포넌트 목록의 모양 틀린 원소는 그 원소만 버린다(nogpu).
#include "pch.h"

#include "Core/String/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"

/**
 * @brief [SerializationTextTest] 글 칸(`string`)은 끝 공백을 값으로 지키고, 이름 칸(`hashed_string`)은 자른다
 * @details 맡아 둔 원문(`MissingComponent::_originalText`)처럼 줄바꿈으로 끝나는 글이 왕복에서 바뀌지 않아야 한다.
 */
SW_TEST_CASE( SerializationTextTest, StringKeepsTrailingWhitespaceButNamesAreTrimmed )
{
    const sw::SerializeContext context = sw::SerializeContext::deriveFromDefault( sw::SerializePurpose::Authoring );

    const sw::SerializeContext::TextReadFn* pReadString = context.findTextReader( sw::hashed_string( sw::PredefinedNameType::NameType_string ) );
    SW_REQUIRE_NOT_NULL( pReadString );
    sw::string text;
    SW_EXPECT_TRUE( ( *pReadString )( &text, "  <A/>\n" ) );
    SW_EXPECT_EQUAL( sw::string( "  <A/>\n" ), text );
    SW_EXPECT_TRUE( ( *pReadString )( &text, "\"quoted\"" ) );
    SW_EXPECT_EQUAL( sw::string( "quoted" ), text );

    const sw::SerializeContext::TextReadFn* pReadName = context.findTextReader( sw::hashed_string( sw::PredefinedNameType::NameType_hashed_string ) );
    SW_REQUIRE_NOT_NULL( pReadName );
    sw::hashed_string name;
    SW_EXPECT_TRUE( ( *pReadName )( &name, "  Muzzle\n" ) );
    SW_EXPECT_EQUAL( sw::string( "Muzzle" ), sw::string( name.c_str() ) );
}

/**
 * @brief [SerializationTextTest] JSON 컴포넌트 목록의 모양 틀린 원소는 그 원소만 버려지고 나머지 컴포넌트는 읽힌다
 * @details `{ "타입이름": {...} }` 꼴이 아닌 원소(`42`)를 목록 앞에 끼워 넣는다. 경고를 남기고 그 원소만 orphan 으로 둔다.
 */
SW_TEST_CASE( SerializationTextTest, MalformedOwnedPointerElementDropsOnlyThatElement )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "MalformedElementHost" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    pObject->addComponent<sw::SceneComponent>();

    sw::string state = sw::ObjectStateSerializer::saveToJSONString( pObject, sw::ObjectSaveOptions{ sw::SerializePurpose::Authoring } );
    size_t     pos   = state.find( "\"SceneComponent\"" );
    SW_REQUIRE_TRUE( pos != sw::string::npos );
    while ( pos > 0 && ( state[pos - 1] == ' ' || state[pos - 1] == '\n' || state[pos - 1] == '\r' || state[pos - 1] == '\t' ) )
    {
        --pos;
    }
    SW_REQUIRE_TRUE( pos > 0 && state[pos - 1] == '{' );
    state.insert( pos - 1, "42," );

    SW_EXPECT_LOG( "Serialization.MalformedOwnedPointerElement" );
    SW_REQUIRE_TRUE( sw::ObjectStateSerializer::loadFromJSONString( pObject, state, sw::ObjectLoadContext{ sw::SerializePurpose::Authoring } ) );
    SW_EXPECT_NOT_NULL( pObject->getComponent<sw::SceneComponent>() );
}
