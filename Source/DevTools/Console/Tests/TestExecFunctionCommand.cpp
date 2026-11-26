// ExecFunctionCommandTest — `exec` 는 FUNCTION( Exec ) 를 단 메서드만 부르고, 인자 글을 그 형식으로 바꾸며, 틀린 인자 · 허용하지 않은 메서드는 까닭과 함께 거절한다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "DevTools/Console/ExecFunctionCommand.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

using namespace sw;

/**
 * @brief [ExecFunctionCommandTest] Exec 메서드는 인자 글로 불리고, 인자가 모자라거나 형식이 틀리거나 Exec 가 아닌 메서드면 부르지 않고 까닭을 답한다
 */
SW_TEST_CASE( ExecFunctionCommandTest, OnlyExecFunctionsAreCalled )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "ExecTarget" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    string reply;
    SW_EXPECT_FALSE( ExecFunctionCommand::run( *pObject, "jump", { "4" }, reply ) ); // 그 메서드를 가진 컴포넌트가 없다
    SW_EXPECT_TRUE( reply.find( "ExecTarget" ) != string::npos );

    SW_REQUIRE_NOT_NULL( pObject->addComponent<CharacterControllerComponent>() );
    SW_EXPECT_TRUE( ExecFunctionCommand::run( *pObject, "jump", { "4" }, reply ) );
    SW_EXPECT_FALSE( ExecFunctionCommand::run( *pObject, "jump", {}, reply ) );                     // 기본 인자가 없다
    SW_EXPECT_FALSE( ExecFunctionCommand::run( *pObject, "jump", { "high" }, reply ) );             // 수가 아니다
    SW_EXPECT_FALSE( ExecFunctionCommand::run( *pObject, "jump", { "4", "5" }, reply ) );           // 인자가 많다
    SW_EXPECT_FALSE( ExecFunctionCommand::run( *pObject, "setMoveVelocity", { "0 0 0" }, reply ) ); // FUNCTION 이 아니다
}

/**
 * @brief [ExecFunctionCommandTest] bool 을 돌려주는 Exec 메서드가 false 를 돌려주면 명령도 실패다 — 클립이 없는 애니메이터의 재생
 */
SW_TEST_CASE( ExecFunctionCommandTest, FalseReturnFailsTheCommand )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "ExecAnimator" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    SW_REQUIRE_NOT_NULL( pObject->addComponent<SkeletalAnimatorComponent>() );
    SW_EXPECT_LOG( "SkeletalAnimator.AnimationClipNotFound" );
    string reply;
    SW_EXPECT_FALSE( ExecFunctionCommand::run( *pObject, "play", { "NoSuchClip" }, reply ) ); // loop · blendSeconds 는 기본 인자다
    SW_EXPECT_TRUE( reply.find( "SkeletalAnimatorComponent.play" ) != string::npos );
}
