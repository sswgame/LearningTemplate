// EmoteTest — 이모트는 폰과 애니메이터가 있어야 한다(없으면 오류이고 아무것도 틀지 않는다). 동작 재생 · 걸으면 그침은 시나리오 game.playground.outdoors 가 본다(nogpu).
#include "pch.h"

#include "Core/String/Name/hashed_string.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Pawn/EmoteComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

using namespace sw;

/** @brief [EmoteTest] 애니메이터가 없으면 오류이고, 폰 스키마에 없는 버튼을 적은 이모트도 오류다 */
SW_TEST_CASE( EmoteTest, ReportsMissingAnimatorAndUnknownButton )
{
    SW_EXPECT_LOG( "Emote.NoPawnOrAnimator" );
    SW_EXPECT_LOG( "Emote.BadEntry" );
    GameObjectManager manager;
    GameObject*       pBare  = manager.createGameObject( hashed_string( "Bare" ) );
    EmoteComponent*   pEmote = pBare->addComponent<EmoteComponent>();
    SW_REQUIRE_NOT_NULL( pEmote );

    GameObject* pBody = manager.createGameObject( hashed_string( "Body" ) );
    pBody->addComponent<PawnComponent>()->setAutoPossess( PawnAutoPossess::None );
    SW_REQUIRE_NOT_NULL( pBody->addComponent<SkeletalAnimatorComponent>() );
    EmoteComponent* pUnknown = pBody->addComponent<EmoteComponent>();
    SW_REQUIRE_NOT_NULL( pUnknown );
    EmoteDef emote;
    emote._button = hashed_string( "NoSuchButton" );
    emote._state  = hashed_string( "Cheer" );
    pUnknown->setEmotes( vector<EmoteDef>{ emote } );
    pEmote->onBeginPlay(); // 애니메이터까지 시작하지 않는다 — 이 테스트는 이모트의 준비만 본다
    pUnknown->onBeginPlay();
    SW_EXPECT_TRUE( pEmote->getActiveState().empty() );
    SW_EXPECT_TRUE( pUnknown->getActiveState().empty() );
}
