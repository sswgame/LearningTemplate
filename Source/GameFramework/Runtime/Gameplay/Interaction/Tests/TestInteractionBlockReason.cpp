// 상호작용 진단 — 시작할 수 없는 까닭(사유별)과 정의의 효과 칸(시작 · 완료 · 취소)을 데이터에서 읽는다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionCatalog.h"

using namespace sw;

/** @brief [InteractionBlockReasonTest] 까닭은 정의 · 꺼짐 · 필수 태그 · 금지 태그 순으로 첫 것이고, 모두 맞으면 None(= isAvailableFor) */
SW_TEST_CASE( InteractionBlockReasonTest, ReasonsComeInOrder )
{
    GameObjectManager      manager;
    GameObject&            lever  = *manager.createGameObject( hashed_string( "Lever" ) );
    GameObject&            player = *manager.createGameObject( hashed_string( "Player" ) );
    InteractableComponent* pUse   = lever.addComponent<InteractableComponent>();
    SW_EXPECT_TRUE( pUse->findBlockReason( player ) == InteractionBlockReason::NoDefinition );

    InteractionDef def;
    def._id = hashed_string( "pull" );
    def._requiredTags.addTag( "Test.Has.Gloves"_tag );
    def._forbiddenTags.addTag( "Test.State.Stunned"_tag );
    pUse->setDefinition( def );
    SW_EXPECT_TRUE( pUse->findBlockReason( player ) == InteractionBlockReason::MissingRequiredTag );
    player.addTag( "Test.Has.Gloves"_tag );
    SW_EXPECT_TRUE( pUse->findBlockReason( player ) == InteractionBlockReason::None );
    SW_EXPECT_TRUE( pUse->isAvailableFor( player ) );
    player.addTag( "Test.State.Stunned"_tag );
    SW_EXPECT_TRUE( pUse->findBlockReason( player ) == InteractionBlockReason::ForbiddenTag );
    pUse->setEnabled( false );
    SW_EXPECT_TRUE( pUse->findBlockReason( player ) == InteractionBlockReason::Disabled );
}

/** @brief [InteractionBlockReasonTest] 정의의 효과 칸은 XML 속성으로 읽고, 시작 효과는 기본으로 끝에 지운다(endStartEffect) */
SW_TEST_CASE( InteractionBlockReasonTest, EffectPropertiesReadFromData )
{
    InteractionCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<Interactions>
  <Interaction id="channel" mode="Hold" duration="2" startEffect="GE_Channel" completeEffect="GE_Blessing" cancelEffect="GE_Drained"/>
  <Interaction id="keep" mode="Press" startEffect="GE_Mark" endStartEffect="false"/>
</Interactions>)",
                                              "InteractionBlockReasonTest" ) );
    const InteractionDef* pChannel = catalog.findInteraction( hashed_string( "channel" ) );
    SW_REQUIRE_TRUE( pChannel != nullptr );
    SW_EXPECT_TRUE( pChannel->_startEffect == hashed_string( "GE_Channel" ) );
    SW_EXPECT_TRUE( pChannel->_completeEffect == hashed_string( "GE_Blessing" ) );
    SW_EXPECT_TRUE( pChannel->_cancelEffect == hashed_string( "GE_Drained" ) );
    SW_EXPECT_TRUE( pChannel->_bEndStartEffect == SW_TRUE );
    const InteractionDef* pKeep = catalog.findInteraction( hashed_string( "keep" ) );
    SW_REQUIRE_TRUE( pKeep != nullptr );
    SW_EXPECT_TRUE( pKeep->_bEndStartEffect == SW_FALSE );
}
