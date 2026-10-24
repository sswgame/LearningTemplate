#include "pch.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Base/Gameplay/Appearance/SequenceAppearanceComponent.h"

#include "TestFramework/TestFramework.h"

// 시퀀서의 외형 항목 — 알림을 받은 컴포넌트가 대상 오브젝트의 외형 프리셋을 바꾼다(시드는 그대로).

using namespace sw;

namespace
{
    SequenceTrackItem makeCue( const utf8* pTarget, const utf8* pPreset, int32 frame )
    {
        SequenceTrackItem cue{};
        cue._name         = "Cue";
        cue._targetObject = pTarget;
        cue._preset       = pPreset;
        cue._kind         = SequenceItemKind::Appearance;
        cue._start        = frame;
        cue._end          = frame;
        return cue;
    }
} // namespace

/**
 * @brief [SequenceAppearanceTest] 외형 항목은 대상의 프리셋만 바꾸고 시드는 그대로 둔다. 외형 항목이 아니거나 대상 · 외형 컴포넌트가 없으면 아무것도 바꾸지 않는다
 */
SW_TEST_CASE( SequenceAppearanceTest, ItemChangesThePresetAndKeepsTheSeed )
{
    GameObjectManager manager;
    GameObject*       pHero = manager.createGameObject( hashed_string( "Hero" ) );
    GameObject*       pProp = manager.createGameObject( hashed_string( "Prop" ) );
    SW_ASSERT_NOT_NULL( pHero );
    SW_ASSERT_NOT_NULL( pProp );
    manager.mergePendingAdds();
    CharacterAppearanceComponent* pAppearance = pHero->addComponent<CharacterAppearanceComponent>();
    SW_ASSERT_NOT_NULL( pAppearance );
    pAppearance->setPreset( hashed_string( "HeroPlain" ), 7 ); // 시작 전이라 조립은 하지 않고 값만 든다

    SW_EXPECT_TRUE( SequenceAppearanceComponent::applyAppearanceItem( manager, makeCue( "Hero", "HeroArmored", 5 ) ) );
    SW_EXPECT_EQUAL( "HeroArmored", pAppearance->getPresetID() );
    SW_EXPECT_EQUAL( 7, pAppearance->getSeed() );

    // 이벤트 항목 같은 다른 종류는 이 컴포넌트의 일이 아니다.
    SequenceTrackItem plainEvent = makeCue( "Hero", "Ignored", 5 );
    plainEvent._kind             = SequenceItemKind::Event;
    SW_EXPECT_FALSE( SequenceAppearanceComponent::applyAppearanceItem( manager, plainEvent ) );
    SW_EXPECT_EQUAL( "HeroArmored", pAppearance->getPresetID() );

    SW_TEST_DEFENSIVE_SCOPE( "an appearance item whose target is missing or has no appearance component changes nothing" );
    SW_EXPECT_FALSE( SequenceAppearanceComponent::applyAppearanceItem( manager, makeCue( "Nobody", "HeroArmored", 5 ) ) );
    SW_EXPECT_FALSE( SequenceAppearanceComponent::applyAppearanceItem( manager, makeCue( "Prop", "HeroArmored", 5 ) ) );
}

/**
 * @brief [SequenceAppearanceTest] 시퀀스 플레이어가 외형 항목의 시작 프레임을 지나면 같은 오브젝트의 컴포넌트가 대상의 프리셋을 바꾼다
 */
SW_TEST_CASE( SequenceAppearanceTest, PlayerDrivesTheComponent )
{
    SequenceAsset asset;
    asset._frameMin = 0;
    asset._frameMax = 30;
    asset._listItem.push_back( makeCue( "Hero", "HeroArmored", 10 ) );

    GameObjectManager manager;
    GameObject*       pDirector = manager.createGameObject( hashed_string( "Director" ) );
    GameObject*       pHero     = manager.createGameObject( hashed_string( "Hero" ) );
    SW_ASSERT_NOT_NULL( pDirector );
    SW_ASSERT_NOT_NULL( pHero );
    manager.mergePendingAdds();
    CharacterAppearanceComponent* pAppearance = pHero->addComponent<CharacterAppearanceComponent>();
    SW_ASSERT_NOT_NULL( pAppearance );
    pAppearance->setPreset( hashed_string( "HeroPlain" ), 3 );

    SequencePlayerComponent*     pPlayer   = pDirector->addComponent<SequencePlayerComponent>();
    SequenceAppearanceComponent* pListener = pDirector->addComponent<SequenceAppearanceComponent>();
    SW_ASSERT_NOT_NULL( pPlayer );
    SW_ASSERT_NOT_NULL( pListener );
    pPlayer->setSequence( asset );
    pListener->onBeginPlay(); // 같은 오브젝트의 플레이어에 건다

    pPlayer->play();
    SW_EXPECT_EQUAL( "HeroPlain", pAppearance->getPresetID() ); // 프레임 0 — 아직 시작 프레임 전
    pPlayer->onTick( 12.0f / 30.0f );
    SW_EXPECT_EQUAL( "HeroArmored", pAppearance->getPresetID() );
    SW_EXPECT_EQUAL( 3, pAppearance->getSeed() );

    pListener->onEndPlay(); // 떼고 나면 더 듣지 않는다
    pAppearance->setPreset( hashed_string( "HeroPlain" ), 3 );
    pPlayer->play();
    pPlayer->onTick( 12.0f / 30.0f );
    SW_EXPECT_EQUAL( "HeroPlain", pAppearance->getPresetID() );
}
