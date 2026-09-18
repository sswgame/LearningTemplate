#include "pch.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayer.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include "TestFramework/TestFramework.h"

// 시퀀서 — 타임라인 프레임 적용 · 이벤트 교차 판정 · JSON 왕복.

namespace sw
{
    namespace
    {
        /** @brief 지나간 이벤트 목록에 이 이름이 있는지. */
        bool containsEvent( const vector<const SequenceTrackItem*>& listCrossed, const utf8* pName )
        {
            for ( const SequenceTrackItem* pItem : listCrossed )
            {
                if ( pItem != nullptr && pItem->_name == pName )
                    return true;
            }
            return false;
        }

        /** @brief 클립 하나와 첫 프레임 이벤트 하나를 가진 시퀀스. */
        SequenceAsset makeSequenceWithFirstFrameEvent()
        {
            SequenceAsset asset;
            asset._frameMin = 0;
            asset._frameMax = 30;

            SequenceTrackItem clip{};
            clip._name         = "Clip";
            clip._targetObject = "SeqTarget";
            clip._start        = 0;
            clip._end          = 20;
            clip._translation  = float3{ 4.0f, 0.0f, 0.0f };
            clip._type         = 0;
            asset._listItem.push_back( std::move( clip ) );

            SequenceTrackItem event{};
            event._name         = "FirstFrameEvent";
            event._targetObject = "SeqTarget";
            event._start        = 0;
            event._end          = 0;
            event._type         = 1;
            asset._listItem.push_back( std::move( event ) );

            return asset;
        }
    } // namespace
} // namespace sw

/**
 * @brief [SequencerTest] 시퀀스의 첫 프레임에 걸린 이벤트도 발화한다
 * @details 이벤트는 "이전 프레임에는 아직 안 지났고 이번 프레임에는 지났다" 로 판정한다.
 *          그런데 play() 가 _previousFrame 을 _frameMin 으로 두었기 때문에 첫 프레임에
 *          걸린 이벤트는 처음부터 "이미 지난 것" 이었다 — 영영 발화하지 않았다. 루프가
 *          한 바퀴 돌 때마다 같은 일이 반복된다.
 */
SW_TEST_CASE( SequencerTest, FirstFrameEventFires )
{
    sw::SequencePlayer player;
    player.setAsset( sw::makeSequenceWithFirstFrameEvent() );
    player.play();

    sw::GameObjectManager manager;
    sw::GameObject*       pTarget = manager.createGameObject( sw::hashed_string{ "SeqTarget" } );
    SW_ASSERT_NOT_NULL( pTarget );
    manager.mergePendingAdds();

    sw::vector<const sw::SequenceTrackItem*> listCrossed;
    sw::SequenceTimelineUtil::applyFrame( &manager, player.getAsset(), player.getCurrentFrame(), player.getPreviousFrame(), &listCrossed );
    SW_EXPECT_TRUE( sw::containsEvent( listCrossed, "FirstFrameEvent" ) );
}

/**
 * @brief [SequencerTest] 같은 프레임을 두 번 적용해도 이벤트는 한 번만 발화한다
 */
SW_TEST_CASE( SequencerTest, EventDoesNotRefireOnSameFrame )
{
    sw::SequencePlayer player;
    player.setAsset( sw::makeSequenceWithFirstFrameEvent() );
    player.play();

    sw::GameObjectManager manager;
    SW_ASSERT_NOT_NULL( manager.createGameObject( sw::hashed_string{ "SeqTarget" } ) );
    manager.mergePendingAdds();

    const int32 frame = player.getCurrentFrame();
    sw::SequenceTimelineUtil::applyFrame( &manager, player.getAsset(), frame, player.getPreviousFrame() );

    // 두 번째 적용의 이전 프레임은 이제 이번 프레임과 같다 — 지나간 적이 없다.
    sw::vector<const sw::SequenceTrackItem*> listCrossed;
    sw::SequenceTimelineUtil::applyFrame( &manager, player.getAsset(), frame, frame, &listCrossed );
    SW_EXPECT_FALSE( sw::containsEvent( listCrossed, "FirstFrameEvent" ) );
}

/**
 * @brief [SequencerTest] 클립 밖의 대상은 꺼지고, 안의 대상은 켜지며 트랜스폼이 적용된다
 */
SW_TEST_CASE( SequencerTest, ClipTogglesTargetAndAppliesTransform )
{
    sw::SequencePlayer player;
    player.setAsset( sw::makeSequenceWithFirstFrameEvent() );

    sw::GameObjectManager manager;
    sw::GameObject*       pTarget = manager.createGameObject( sw::hashed_string{ "SeqTarget" } );
    SW_ASSERT_NOT_NULL( pTarget );
    // 트랜스폼을 적용할 자리가 있어야 한다 — 없으면 applyClipTransform 이 조용히 넘어간다.
    SW_ASSERT_NOT_NULL( pTarget->addComponent<sw::SceneComponent>() );
    manager.mergePendingAdds();

    // 클립 한가운데(프레임 10) — 켜지고 절반쯤 이동해 있다.
    sw::SequenceTimelineUtil::applyFrame( &manager, player.getAsset(), 10 );
    SW_EXPECT_TRUE( pTarget->isActive() );
    sw::SceneComponent* pScene = pTarget->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pScene );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pScene->getLocalPosition()._x, 0.001f );

    // 클립 밖(프레임 25) — 꺼진다.
    sw::SequenceTimelineUtil::applyFrame( &manager, player.getAsset(), 25 );
    SW_EXPECT_FALSE( pTarget->isActive() );
}

/**
 * @brief [SequencerTest] JSON 왕복이 값을 잃지 않는다 (파일 경유 포함)
 * @details loadFromFile 은 문서를 읽은 뒤 다시 문자열로 덤프해 재파싱하고 있었다.
 *          값은 살아남았지만 전체 파일을 두 번 파싱하는 일이었다 — 지금은 읽은 문서를
 *          그대로 읽는다. 이 테스트는 그 경로가 여전히 같은 결과를 내는지 본다.
 */
SW_TEST_CASE( SequencerTest, JsonRoundTripThroughFileKeepsValues )
{
    const sw::string path = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_sequence.json" );
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [path]()
    {
        sw::FileUtil::removeFile( path );
    } ) );

    sw::SequenceAsset source = sw::makeSequenceWithFirstFrameEvent();
    source._note             = "round trip";
    SW_ASSERT_TRUE( source.saveToFile( path ) );

    sw::SequenceAsset loaded;
    SW_ASSERT_TRUE( loaded.loadFromFile( path ) );

    SW_EXPECT_EQUAL( source._frameMin, loaded._frameMin );
    SW_EXPECT_EQUAL( source._frameMax, loaded._frameMax );
    SW_EXPECT_STREQ( source._note.c_str(), loaded._note.c_str() );
    SW_ASSERT_EQUAL( source._listItem.size(), loaded._listItem.size() );
    for ( size_t index = 0; index < source._listItem.size(); ++index )
    {
        SW_EXPECT_STREQ( source._listItem[index]._name.c_str(), loaded._listItem[index]._name.c_str() );
        SW_EXPECT_EQUAL( source._listItem[index]._start, loaded._listItem[index]._start );
        SW_EXPECT_EQUAL( source._listItem[index]._end, loaded._listItem[index]._end );
        SW_EXPECT_EQUAL( source._listItem[index]._type, loaded._listItem[index]._type );
        SW_EXPECT_NEAR_EQUAL( source._listItem[index]._translation._x, loaded._listItem[index]._translation._x, 0.0001f );
    }
}

/**
 * @brief [SequencerTest] 파싱이 실패하면 애셋에 반쯤 남은 상태가 없다
 * @details 예전 parseJson 은 _listItem 만 비우고 실패해서, 앞 시퀀스의 프레임 범위와
 *          노트가 그대로 남았다 — 트랙 없는 옛 시퀀스가 새 시퀀스인 척했다.
 */
SW_TEST_CASE( SequencerTest, FailedParseLeavesNothingBehind )
{
    sw::SequenceAsset asset = sw::makeSequenceWithFirstFrameEvent();
    asset._note             = "previous sequence";
    SW_ASSERT_EQUAL( size_t( 2 ), asset._listItem.size() );

    {
        test::ScopedLogSuppressor suppressor;
        SW_EXPECT_FALSE( asset.parseJson( "{ this is not json" ) );
    }

    SW_EXPECT_TRUE( asset._listItem.empty() );
    SW_EXPECT_TRUE( asset._note.empty() );
    SW_EXPECT_EQUAL( 0, asset._frameMin );
    SW_EXPECT_EQUAL( 100, asset._frameMax );
}
