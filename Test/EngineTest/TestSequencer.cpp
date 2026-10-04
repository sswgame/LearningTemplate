#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayer.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"
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
            clip._kind         = sw::SequenceItemKind::Clip;
            asset._listItem.push_back( std::move( clip ) );

            SequenceTrackItem event{};
            event._name         = "FirstFrameEvent";
            event._targetObject = "SeqTarget";
            event._start        = 0;
            event._end          = 0;
            event._kind         = sw::SequenceItemKind::Event;
            asset._listItem.push_back( std::move( event ) );

            return asset;
        }
    } // namespace
} // namespace sw

/**
 * @brief [SequencerTest] 시퀀스의 첫 프레임에 걸린 이벤트도 발화한다
 * @details 이벤트는 "이전 프레임에는 아직 안 지났고 이번 프레임에는 지났다" 로 판정한다.
 *          play() 가 _previousFrame 을 _frameMin 으로 두면 첫 프레임에 걸린 이벤트는 처음부터
 *          "이미 지난 것" 이라 영영 발화하지 않는다. 루프가 한 바퀴 돌 때마다 같은 일이 반복된다.
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

    // 두 번째 적용의 이전 프레임은 이번 프레임과 같다 — 지나간 적이 없다.
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
 * @brief [SequencerTest] 클립이 둘인 대상은 프레임마다 꺼졌다 켜지지 않고, 대상의 컴포넌트를 꺼 두면 그대로다
 * @details applyFrame 이 클립마다 대상을 끄고(덮지 않는 클립) 켜면(덮는 클립) 클립이 둘인 대상은 매 프레임 꺼졌다 켜져 렌더
 *          집합이 두 번 흔들린다. 오브젝트 토글이 컴포넌트의 자기 비트를 덮어쓰면 꺼 둔 컴포넌트가 다음 프레임에 켜진다.
 */
SW_TEST_CASE( SequencerTest, MultiClipTargetDoesNotFlicker )
{
    sw::SequenceAsset asset;
    asset._frameMin = 0;
    asset._frameMax = 40;
    for ( int32 clipIndex = 0; clipIndex < 2; ++clipIndex )
    {
        sw::SequenceTrackItem clip{};
        clip._name         = clipIndex == 0 ? "Early" : "Late";
        clip._targetObject = "Flicker";
        clip._start        = clipIndex == 0 ? 0 : 20;
        clip._end          = clipIndex == 0 ? 10 : 30;
        clip._kind         = sw::SequenceItemKind::Clip;
        asset._listItem.push_back( std::move( clip ) );
    }

    sw::GameObjectManager manager;
    sw::GameObject*       pTarget = manager.createGameObject( sw::hashed_string{ "Flicker" } );
    sw::MeshComponent*    pMesh   = pTarget->addComponent<sw::MeshComponent>();
    sw::SceneComponent*   pOff    = pTarget->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    manager.mergePendingAdds();
    pOff->setActive( false );

    sw::SequenceTimelineUtil::applyFrame( &manager, asset, 5 );
    SW_EXPECT_TRUE( pTarget->isActive() );
    // 켜진 채 머무는 프레임에서는 아무것도 흔들리지 않는다 — 한 호출 안에서 꺼졌다 켜지면 메시가 렌더 더티를 찍는다.
    manager.getPrimitiveRegistry().clearDirty();
    for ( int32 frame = 6; frame < 10; ++frame )
        sw::SequenceTimelineUtil::applyFrame( &manager, asset, frame );
    SW_EXPECT_FALSE( manager.getPrimitiveRegistry().hasDirty() );
    SW_EXPECT_FALSE( pOff->isSelfActive() ); // 꺼 둔 컴포넌트는 그대로

    sw::SequenceTimelineUtil::applyFrame( &manager, asset, 15 );
    SW_EXPECT_FALSE( pTarget->isActive() );
    sw::SequenceTimelineUtil::applyFrame( &manager, asset, 25 );
    SW_EXPECT_TRUE( pTarget->isActive() );
    SW_EXPECT_FALSE( pOff->isSelfActive() );
}

/**
 * @brief [SequencerTest] JSON 왕복이 값을 잃지 않는다 (파일 경유 포함)
 * @details loadFromFile 은 읽은 문서를 다시 문자열로 덤프하지 않고 그대로 읽는다. 이 테스트는 그 경로가 같은 결과를 내는지 본다.
 */
SW_TEST_CASE( SequencerTest, JsonRoundTripThroughFileKeepsValues )
{
    const sw::string  path   = test::makeTempPath( "sw_test_sequence.json" );
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
        SW_EXPECT_TRUE( source._listItem[index]._kind == loaded._listItem[index]._kind );
        SW_EXPECT_NEAR_EQUAL( source._listItem[index]._translation._x, loaded._listItem[index]._translation._x, 0.0001f );
    }
}

/**
 * @brief [SequencerTest] 파싱이 실패하면 애셋에 반쯤 남은 상태가 없다
 * @details parseJson 이 _listItem 만 비우고 실패하면 앞 시퀀스의 프레임 범위와 노트가 그대로 남아,
 *          트랙 없는 옛 시퀀스가 새 시퀀스인 척한다.
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

/**
 * @brief [SequencerTest] 새 자산을 넣으면 재생 위치가 **그 자산의** 시작으로 돌아간다
 * @details setAsset/loadFromFile 이 자산을 바꾸기 **전에** stop() 을 부르면 stop() 안의
 *          `_previousFrame = _asset._frameMin` 이 아직 옛 자산을 보므로, 100 프레임에서
 *          시작하는 자산을 넣을 때 이전 프레임만 0 에 남고 현재 프레임은 100 이 된다 — 첫 적용이
 *          `applyFrame(100, 0)` 이라 100 이하의 이벤트가 전부 한꺼번에 발화한다.
 */
SW_TEST_CASE( SequencerTest, LoadedAssetResetsPlaybackToItsOwnStart )
{
    sw::SequencePlayer player;
    player.setAsset( sw::makeSequenceWithFirstFrameEvent() ); // _frameMin 은 0

    sw::SequenceAsset later;
    later._frameMin = 100;
    later._frameMax = 200;

    sw::SequenceTrackItem event{};
    event._name         = "MidEvent";
    event._targetObject = "SeqTarget";
    event._start        = 50; // 새 자산의 시작보다 앞 — 지나간 적이 없어야 한다.
    event._end          = 50;
    event._kind         = sw::SequenceItemKind::Event;
    later._listItem.push_back( std::move( event ) );

    player.setAsset( later );
    SW_EXPECT_EQUAL( player.getCurrentFrame(), player.getPreviousFrame() );
    SW_EXPECT_EQUAL( 100, player.getPreviousFrame() );

    sw::GameObjectManager manager;
    SW_ASSERT_NOT_NULL( manager.createGameObject( sw::hashed_string{ "SeqTarget" } ) );
    manager.mergePendingAdds();

    sw::vector<const sw::SequenceTrackItem*> listCrossed;
    sw::SequenceTimelineUtil::applyFrame( &manager, player.getAsset(), player.getCurrentFrame(), player.getPreviousFrame(), &listCrossed );
    SW_EXPECT_FALSE( sw::containsEvent( listCrossed, "MidEvent" ) );
}

/**
 * @brief [SequencerTest] 파일이 준 프레임 번호가 int32 양 끝이어도 뺄셈이 넘치지 않는다
 * @details 프레임 번호는 JSON 에서 온다. int32 최대값이 그대로 들어오면 `_frameMax = _frameMin + 1`
 *          이 부호 있는 넘침이 되어 끝이 시작보다 **앞**이 되고, 트랙의 `_end - _start` 도 마찬가지로
 *          접혔다. 파싱 자리에서 절반 범위로 잘라 어떤 두 값의 차도 int32 안에 들어오게 한다.
 */
SW_TEST_CASE( SequencerTest, OutOfRangeFrameNumbersCannotOverflowSpans )
{
    sw::SequenceAsset asset;
    SW_ASSERT_TRUE( asset.parseJson( R"({
        "frameMin": 2147483647,
        "frameMax": 0,
        "items": [ { "name": "Wide", "target": "SeqTarget", "start": 2147483647, "end": -2147483648 } ]
    })" ) );

    SW_EXPECT_TRUE( asset._frameMax > asset._frameMin );
    SW_ASSERT_EQUAL( size_t( 1 ), asset._listItem.size() );

    const int64 itemSpan = static_cast<int64>( asset._listItem[0]._end ) - static_cast<int64>( asset._listItem[0]._start );
    SW_EXPECT_TRUE( itemSpan >= static_cast<int64>( sw::MathUtil::MinInt32 ) );
    SW_EXPECT_TRUE( itemSpan <= static_cast<int64>( sw::MathUtil::MaxInt32 ) );
}

/**
 * @brief [SequencerTest] 루프가 되감길 때 끝 구간 이벤트와 마지막 프레임 이벤트도 발화한다
 * @details 되감을 때 이전 프레임을 `_frameMin - 1` 로 돌리기만 하면 (직전 프레임, `_frameMax`] 구간을 아무도 보지 않는다 — 끝쪽 이벤트가 루프마다
 *          빠지고 `_frameMax` 의 이벤트는 루프 중에 한 번도 뜨지 않는다.
 */
SW_TEST_CASE( SequencerTest, LoopWrapFiresTailAndLastFrameEvents )
{
    sw::SequenceAsset asset;
    asset._frameMin = 0;
    asset._frameMax = 60;
    for ( const auto& [pName, frame] : {
              std::pair<const utf8*, int32>{ "TailEvent", 58},
              {  "EndEvent", 60},
              {"StartEvent",  2}
    } )
    {
        sw::SequenceTrackItem event{};
        event._name         = pName;
        event._targetObject = "SeqTarget";
        event._start        = frame;
        event._end          = frame;
        event._kind         = sw::SequenceItemKind::Event;
        asset._listItem.push_back( std::move( event ) );
    }

    sw::SequencePlayer player;
    player.setAsset( asset );
    player.setFramesPerSecond( 60.0f );
    player.setLoop( true );
    player.play();
    player.seekToFrame( 50 );
    // 15 프레임 → 65 에서 되감겨 5.
    player.update( 15.0f / 60.0f );
    SW_ASSERT_TRUE( player.getFrameBeforeWrap() != sw::SequencePlayer::kNoLoopWrap );

    sw::GameObjectManager manager;
    SW_ASSERT_NOT_NULL( manager.createGameObject( sw::hashed_string{ "SeqTarget" } ) );
    manager.mergePendingAdds();

    sw::vector<const sw::SequenceTrackItem*> listCrossed;
    sw::SequenceTimelineUtil::applyPlayback( &manager, player, &listCrossed );
    SW_EXPECT_TRUE( sw::containsEvent( listCrossed, "TailEvent" ) );
    SW_EXPECT_TRUE( sw::containsEvent( listCrossed, "EndEvent" ) );
    SW_EXPECT_TRUE( sw::containsEvent( listCrossed, "StartEvent" ) );

    // 되감지 않은 다음 갱신에서는 끝 구간을 다시 보지 않는다.
    player.update( 1.0f / 60.0f );
    SW_EXPECT_TRUE( player.getFrameBeforeWrap() == sw::SequencePlayer::kNoLoopWrap );
    sw::SequenceTimelineUtil::applyPlayback( &manager, player, &listCrossed );
    SW_EXPECT_FALSE( sw::containsEvent( listCrossed, "EndEvent" ) );
}

/**
 * @brief [SequencerTest] 반복 안 하는 시퀀스는 끝 프레임에 닿고, 프레임으로 찾아가면 그 프레임이다(float32 경계 반올림)
 * @details 끝에서 멈춘 시간은 span/fps 인데 float32 로는 경계 바로 아래라(63/30*30 = 62.999996) 자르면 한 프레임 모자란다. 30 fps 의
 *          63 · 125 · 126 · 127, 25 fps 의 53 · 59 프레임 길이가 그렇다 — 그 프레임에서 끝나는 클립은 끝까지 가지 않고, 거기 놓인
 *          이벤트는 발화하지 않는다.
 */
SW_TEST_CASE( SequencerTest, NonLoopingSequenceReachesItsLastFrame )
{
    for ( const float32 fps : { 30.0f, 60.0f, 25.0f, 29.97f } )
    {
        for ( int32 span = 1; span <= 300; ++span )
        {
            sw::SequenceAsset asset;
            asset._frameMin = 0;
            asset._frameMax = span;

            sw::SequencePlayer player;
            player.setAsset( asset );
            player.setFramesPerSecond( fps );
            player.setLoop( false );
            player.play();
            player.update( 100000.0f );
            if ( player.getCurrentFrame() != span )
            {
                sw::StringBuilder<sw::constant::kMaxBuffer128> message;
                message.appendFormat( "end frame at fps %# span %# was %#", fps, span, player.getCurrentFrame() );
                SW_EXPECT_TRUE_MSG( false, message.c_str() );
                return;
            }

            player.seekToFrame( span / 2 );
            if ( player.getCurrentFrame() != span / 2 )
            {
                sw::StringBuilder<sw::constant::kMaxBuffer128> message;
                message.appendFormat( "seek to %# at fps %# landed on %#", span / 2, fps, player.getCurrentFrame() );
                SW_EXPECT_TRUE_MSG( false, message.c_str() );
                return;
            }
        }
    }
}

// ------------------------------------------------------------------------------
// 항목 종류 표 · 이벤트 델리게이트
// ------------------------------------------------------------------------------

/**
 * @brief [SequencerTest] 종류 표가 종류마다 한 줄이고, 표에 없는 정수는 nullptr 이다
 */
SW_TEST_CASE( SequencerTest, ItemKindTableCoversEveryKind )
{
    for ( int32 kindIndex = 0; kindIndex < static_cast<int32>( sw::SequenceItemKind::Count ); ++kindIndex )
    {
        const sw::SequenceItemKindInfo* pInfo = sw::SequenceAsset::findItemKindInfo( static_cast<sw::SequenceItemKind>( kindIndex ) );
        SW_ASSERT_NOT_NULL( pInfo );
        SW_EXPECT_EQUAL( kindIndex, static_cast<int32>( pInfo->_kind ) );
    }
    // 에디터 트랙 이름은 리플렉션 enum 이름이다.
    SW_EXPECT_STREQ( "Clip", sw::engine::getTypeRegistry().enumToString( sw::SequenceItemKind::Clip ) );
    SW_EXPECT_STREQ( "Event", sw::engine::getTypeRegistry().enumToString( sw::SequenceItemKind::Event ) );
    SW_EXPECT_NULL( sw::SequenceAsset::findItemKindInfo( sw::SequenceItemKind::Count ) );
    SW_EXPECT_NULL( sw::SequenceAsset::findItemKindInfo( static_cast<sw::SequenceItemKind>( -1 ) ) );
}

/**
 * @brief [SequencerTest] 종류를 enum 으로 바꾼 뒤에도 시퀀스 파일이 같은 바이트로 왕복한다(종류는 정수 그대로)
 * @details 아래 문서는 종류가 `int32 _type` 이던 때의 `toJson` 출력이다(저장소에 시퀀스 파일이 없어 시험 안에 둔다). 세 번째 항목의
 *          종류 7 은 이 버전이 모르는 값이다 — 경고하고, 지우지 않고 다시 쓴다.
 */
SW_TEST_CASE( SequencerTest, SequenceFileRoundTripKeepsIntegerKinds )
{
    const sw::string_view kSequenceJson = R"({
  "frameMin": 0,
  "frameMax": 48,
  "note": "golden",
  "items": [
    {
      "name": "Walk",
      "target": "Hero",
      "start": 0,
      "end": 24,
      "type": 0,
      "color": 4289364096,
      "translation": {
        "x": 2.0,
        "y": 0.0,
        "z": 0.5
      },
      "rotation": {
        "x": 0.0,
        "y": 0.0,
        "z": 0.0
      },
      "scale": {
        "x": 1.0,
        "y": 1.0,
        "z": 1.0
      }
    },
    {
      "name": "Door",
      "target": "Gate",
      "start": 12,
      "end": 12,
      "type": 1,
      "color": 4286611626,
      "translation": {
        "x": 0.0,
        "y": 0.0,
        "z": 0.0
      },
      "rotation": {
        "x": 0.0,
        "y": 0.0,
        "z": 0.0
      },
      "scale": {
        "x": 1.0,
        "y": 1.0,
        "z": 1.0
      }
    },
    {
      "name": "Future",
      "target": "",
      "start": 30,
      "end": 40,
      "type": 7,
      "color": 4289364096,
      "translation": {
        "x": 0.0,
        "y": 0.0,
        "z": 0.0
      },
      "rotation": {
        "x": 0.0,
        "y": 0.0,
        "z": 0.0
      },
      "scale": {
        "x": 1.0,
        "y": 1.0,
        "z": 1.0
      }
    }
  ]
})";

    sw::SequenceAsset asset;
    {
        test::ScopedDefensiveTestLog expected( "unknown sequence item type 7" );
        SW_ASSERT_TRUE( asset.parseJson( kSequenceJson ) );
    }
    SW_ASSERT_EQUAL( size_t( 3 ), asset._listItem.size() );
    SW_EXPECT_TRUE( asset._listItem[0]._kind == sw::SequenceItemKind::Clip );
    SW_EXPECT_TRUE( asset._listItem[1]._kind == sw::SequenceItemKind::Event );
    SW_EXPECT_EQUAL( 7, static_cast<int32>( asset._listItem[2]._kind ) );
    SW_EXPECT_TRUE_MSG( asset.toJson() == kSequenceJson, "시퀀스 파일을 읽고 다시 쓰면 바이트가 달라진다" );
}

/**
 * @brief [SequencerTest] 종류마다 적용이 다르다 — 클립은 대상을 몰고, 이벤트는 지날 때 알리고, 모르는 종류는 아무것도 하지 않는다
 */
SW_TEST_CASE( SequencerTest, EachItemKindAppliesItsOwnWay )
{
    sw::SequenceAsset asset;
    asset._frameMin                        = 0;
    asset._frameMax                        = 30;
    const sw::SequenceItemKind arrKind[]   = { sw::SequenceItemKind::Clip, sw::SequenceItemKind::Event, static_cast<sw::SequenceItemKind>( 7 ) };
    const utf8*                arrTarget[] = { "ClipTarget", "EventTarget", "FutureTarget" };
    for ( size_t itemIndex = 0; itemIndex < SW_COUNT_OF( arrKind ); ++itemIndex )
    {
        sw::SequenceTrackItem item{};
        item._name         = arrTarget[itemIndex];
        item._targetObject = arrTarget[itemIndex];
        item._kind         = arrKind[itemIndex];
        item._start        = 5;
        item._end          = 20;
        asset._listItem.push_back( std::move( item ) );
    }

    sw::GameObjectManager manager;
    sw::GameObject*       arrObject[SW_COUNT_OF( arrTarget )] = {};
    for ( size_t itemIndex = 0; itemIndex < SW_COUNT_OF( arrTarget ); ++itemIndex )
    {
        arrObject[itemIndex] = manager.createGameObject( sw::hashed_string{ arrTarget[itemIndex] } );
        SW_ASSERT_NOT_NULL( arrObject[itemIndex] );
    }
    manager.mergePendingAdds();

    // 구간 밖: 클립 대상만 꺼진다.
    sw::vector<const sw::SequenceTrackItem*> listCrossed;
    sw::SequenceTimelineUtil::applyFrame( &manager, asset, 0, sw::SequenceTimelineUtil::kNoPreviousFrame, &listCrossed );
    SW_EXPECT_FALSE( arrObject[0]->isActive() );
    SW_EXPECT_TRUE( arrObject[1]->isActive() );
    SW_EXPECT_TRUE( arrObject[2]->isActive() );

    // 시작 프레임을 지남: 클립 대상이 켜지고, 이벤트만 알린다.
    sw::SequenceTimelineUtil::applyFrame( &manager, asset, 10, 0, &listCrossed );
    SW_EXPECT_TRUE( arrObject[0]->isActive() );
    SW_ASSERT_EQUAL( size_t( 1 ), listCrossed.size() );
    SW_EXPECT_TRUE( listCrossed[0]->_kind == sw::SequenceItemKind::Event );
}

/**
 * @brief [SequencerTest] SequencePlayerComponent 가 지난 이벤트를 걸린 델리게이트 모두에 알린다(로그가 없는 Shipping 에서도 같은 길)
 * @details 컴포넌트가 지난 이벤트를 받을 출력 없이 타임라인을 적용하면 이벤트 트랙은 Dev 로그 한 줄 말고는 아무 데도 나가지 않는다.
 */
SW_TEST_CASE( SequencerTest, PlayerComponentBroadcastsCrossedEvents )
{
    sw::SequenceAsset asset;
    asset._frameMin = 0;
    asset._frameMax = 30;
    for ( const auto& [pName, frame] : {
              std::pair<const utf8*, int32>{ "Open",  0},
              {"Close", 10}
    } )
    {
        sw::SequenceTrackItem event{};
        event._name  = pName;
        event._kind  = sw::SequenceItemKind::Event;
        event._start = frame;
        event._end   = frame;
        asset._listItem.push_back( std::move( event ) );
    }

    sw::GameObjectManager manager;
    sw::GameObject*       pDirector = manager.createGameObject( sw::hashed_string{ "Director" } );
    SW_ASSERT_NOT_NULL( pDirector );
    manager.mergePendingAdds();
    sw::SequencePlayerComponent* pPlayer = pDirector->addComponent<sw::SequencePlayerComponent>();
    SW_ASSERT_NOT_NULL( pPlayer );
    pPlayer->setSequence( asset );

    sw::vector<sw::string> listFirst;
    sw::vector<sw::string> listSecond;
    pPlayer->registerSequenceEvent( [&listFirst]( const sw::SequenceTrackItem& item )
    { listFirst.push_back( item._name ); } );
    const sw::DelegateHandle secondHandle =
        pPlayer->registerSequenceEvent( [&listSecond]( const sw::SequenceTrackItem& item )
    { listSecond.push_back( item._name ); } );

    pPlayer->play();
    SW_ASSERT_EQUAL( size_t( 1 ), listFirst.size() );
    SW_EXPECT_EQUAL( "Open", listFirst[0] );
    SW_EXPECT_EQUAL( size_t( 1 ), listSecond.size() );

    pPlayer->unregisterSequenceEvent( secondHandle );
    pPlayer->onTick( 12.0f / 30.0f );
    SW_ASSERT_EQUAL( size_t( 2 ), listFirst.size() );
    SW_EXPECT_EQUAL( "Close", listFirst[1] );
    SW_EXPECT_EQUAL( size_t( 1 ), listSecond.size() );
}

/**
 * @brief [SequencerTest] 상태를 읽어 들인 플레이어 컴포넌트는 플레이 전에도 그 시퀀스를 든다
 * @details 상태 읽기는 프로퍼티를 직접 쓰고 `onPostLoad` 만 부른다(onBeginPlay 는 월드가 플레이 중일 때만). 시퀀스를 onBeginPlay 에서만 열면
 *          에디터에서 읽은 컴포넌트의 Play(CallInEditor)가 빈 플레이어를 돌린다.
 */
SW_TEST_CASE( SequencerTest, PlayerComponentReopensItsSequenceAfterStateLoad )
{
    const sw::string  path  = test::makeTempPath( "sw_test_state_sequence.json" );
    sw::SequenceAsset asset = sw::makeSequenceWithFirstFrameEvent();
    SW_ASSERT_TRUE( asset.saveToFile( path ) );

    sw::GameObjectManager manager;
    sw::GameObject*       pSource = manager.createGameObject( sw::hashed_string{ "Director" } );
    SW_ASSERT_NOT_NULL( pSource );
    sw::SequencePlayerComponent* pSourcePlayer = pSource->addComponent<sw::SequencePlayerComponent>();
    SW_ASSERT_NOT_NULL( pSourcePlayer );
    pSourcePlayer->setSequencePath( path );

    const sw::string xml = sw::ObjectStateSerializer::saveToXmlString( pSource );
    SW_ASSERT_FALSE( xml.empty() );
    sw::GameObject* pTarget = manager.createGameObject( sw::hashed_string{ "LoadedDirector" } );
    SW_ASSERT_NOT_NULL( pTarget );
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXmlString( pTarget, xml ) );

    sw::SequencePlayerComponent* pLoaded = pTarget->getComponent<sw::SequencePlayerComponent>();
    SW_ASSERT_NOT_NULL( pLoaded );
    SW_EXPECT_EQUAL( path, pLoaded->getSequencePath() );
    SW_EXPECT_FALSE( pLoaded->hasBegunPlay() );

    sw::vector<sw::string> listEvent;
    pLoaded->registerSequenceEvent( [&listEvent]( const sw::SequenceTrackItem& item )
    { listEvent.push_back( item._name ); } );
    pLoaded->play();
    SW_ASSERT_EQUAL( size_t( 1 ), listEvent.size() );
    SW_EXPECT_EQUAL( "FirstFrameEvent", listEvent[0] );
}
