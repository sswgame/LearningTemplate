#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayer.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include "TestFramework/TestFramework.h"

// 시퀀서 — 타임라인 프레임 적용 · 이벤트 교차 판정 · 키 트랙 · 카메라 컷 · JSON 왕복.

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

        /** @brief 클립 하나, 첫 프레임 이벤트 하나, 0 → 20 프레임에 X 를 0 → 4 로 모는 트랜스폼 트랙 하나를 가진 시퀀스. */
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
            clip._kind         = sw::SequenceItemKind::Clip;
            asset._listItem.push_back( std::move( clip ) );

            SequenceKeyTrack track{};
            track._name         = "Move";
            track._targetObject = "SeqTarget";
            track._kind         = SequenceTrackKind::Transform;
            track.fitChannelsToKind();
            (void)track.setChannelKey( 0, 0.0f, 0.0f );
            (void)track.setChannelKey( 0, 20.0f, 4.0f );
            asset._listTrack.push_back( std::move( track ) );

            SequenceTrackItem event{};
            event._name         = "FirstFrameEvent";
            event._targetObject = "SeqTarget";
            event._start        = 0;
            event._end          = 0;
            event._kind         = sw::SequenceItemKind::Event;
            asset._listItem.push_back( std::move( event ) );

            return asset;
        }

        /** @brief 컷 둘(10~20 은 프리셋 "wide", 21~30), 흔들림 하나(15~18), 프리롤 3 프레임, 블렌드 인 · 아웃이 있는 시퀀스. */
        SequenceAsset makeCameraSequence()
        {
            SequenceAsset asset;
            asset._frameMin              = 0;
            asset._frameMax              = 40;
            asset._cameraBlendInSeconds  = 0.5f;
            asset._cameraBlendOutSeconds = 0.75f;
            asset._cameraPrerollFrames   = 3;
            SequenceTrackItem first{};
            first._name         = "Wide";
            first._targetObject = "ShotA";
            first._preset       = "wide";
            first._kind         = SequenceItemKind::CameraCut;
            first._start        = 10;
            first._end          = 20;
            asset._listItem.push_back( first );
            SequenceTrackItem second{};
            second._name          = "Close";
            second._targetObject  = "ShotB";
            second._kind          = SequenceItemKind::CameraCut;
            second._start         = 21;
            second._end           = 30;
            second._prerollFrames = 0; // 이 컷은 프리롤을 끈다
            asset._listItem.push_back( second );
            SequenceTrackItem shake{};
            shake._name         = "Hit";
            shake._targetObject = "ShotA";
            shake._kind         = SequenceItemKind::CameraShake;
            shake._start        = 15;
            shake._end          = 18;
            shake._amplitude    = 0.4f;
            asset._listItem.push_back( shake );
            return asset;
        }

        uint32 countSignal( const vector<SequenceCameraSignal>& listSignal, SequenceCameraSignalKind kind )
        {
            uint32 count = 0;
            for ( const SequenceCameraSignal& signal : listSignal )
            {
                count += signal._kind == kind ? 1u : 0u;
            }
            return count;
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
 * @brief [SequencerTest] 클립 밖의 대상은 꺼지고, 안의 대상은 켜지며 트랜스폼 트랙이 두 키 사이 값으로 옮긴다
 */
SW_TEST_CASE( SequencerTest, ClipTogglesTargetAndKeyTrackMovesIt )
{
    sw::SequencePlayer player;
    player.setAsset( sw::makeSequenceWithFirstFrameEvent() );

    sw::GameObjectManager manager;
    sw::GameObject*       pTarget = manager.createGameObject( sw::hashed_string{ "SeqTarget" } );
    SW_ASSERT_NOT_NULL( pTarget );
    // 트랜스폼을 적용할 자리가 있어야 한다 — 없으면 트랜스폼 트랙이 조용히 넘어간다.
    SW_ASSERT_NOT_NULL( pTarget->addComponent<sw::SceneComponent>() );
    manager.mergePendingAdds();

    // 두 키 한가운데(프레임 10) — 켜지고 절반 이동해 있다(끝 키의 자동 접선은 평평해 가운데가 정확히 절반이다).
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
    {
        sw::SequenceTimelineUtil::applyFrame( &manager, asset, frame );
    }
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
 * @details loadFromFile 과 parseJSON 은 같은 읽기(`readText`)로 모인다. 이 테스트는 파일 경로가 같은 결과를 내는지 본다.
 */
SW_TEST_CASE( SequencerTest, JSONRoundTripThroughFileKeepsValues )
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
    }
    SW_ASSERT_EQUAL( size_t( 1 ), loaded._listTrack.size() );
    SW_ASSERT_EQUAL( size_t( sw::kSequenceTransformChannelCount ), loaded._listTrack[0]._listChannel.size() );
    SW_EXPECT_EQUAL( size_t( 2 ), loaded._listTrack[0]._listChannel[0]._listKey.size() );
    SW_EXPECT_NEAR_EQUAL( 4.0f, loaded._listTrack[0].evaluateChannel( 0, 20.0f ).value(), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, loaded._listTrack[0].evaluateChannel( 0, 10.0f ).value(), 0.0001f );
}

/**
 * @brief [SequencerTest] 파싱이 실패하면 애셋에 반쯤 남은 상태가 없다
 * @details parseJSON 이 _listItem 만 비우고 실패하면 앞 시퀀스의 프레임 범위와 노트가 그대로 남아,
 *          트랙 없는 옛 시퀀스가 새 시퀀스인 척한다.
 */
SW_TEST_CASE( SequencerTest, FailedParseLeavesNothingBehind )
{
    sw::SequenceAsset asset = sw::makeSequenceWithFirstFrameEvent();
    asset._note             = "previous sequence";
    SW_ASSERT_EQUAL( size_t( 2 ), asset._listItem.size() );

    {
        test::ScopedLogSuppressor suppressor;
        SW_EXPECT_FALSE( asset.parseJSON( "{ this is not json" ) );
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
 * @brief [SequencerTest] 파일이 준 프레임 번호가 `kSequenceFrameLimit` 밖이거나 끝이 시작보다 앞이면 읽지 않는다
 * @details 프레임 번호는 JSON 에서 온다. int32 최대값이 그대로 들어오면 `_frameMax - _frameMin` 과 트랙의 `_end - _start` 가
 *          부호 있는 넘침이 된다. 예전에는 잘라서 읽었지만 잘린 값은 파일이 말한 값이 아니므로 읽기 오류로 멈춘다.
 */
SW_TEST_CASE( SequencerTest, OutOfRangeFrameNumbersAreRejected )
{
    test::ScopedDefensiveTestLog expected( "frame numbers past the sequence frame limit" );
    sw::SequenceAsset            asset;
    SW_EXPECT_FALSE( asset.parseJSON( R"({ "_frameMin": 2147483647, "_frameMax": 0 })" ) );
    SW_EXPECT_FALSE( asset.parseJSON( R"({ "_frameMin": 0, "_frameMax": 10, "_listItem": [ { "_name": "Wide", "_start": 2147483647, "_end": -2147483648 } ] })" ) );
    SW_EXPECT_FALSE( asset.parseJSON( R"({ "_frameMin": 10, "_frameMax": 10 })" ) );
    SW_EXPECT_TRUE( asset._listItem.empty() );
    SW_EXPECT_EQUAL( 0, asset._frameMin );
    SW_EXPECT_EQUAL( 100, asset._frameMax );
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
    SW_EXPECT_STREQ( "Clip", sw::engine::getSubsystem<sw::TypeRegistry>().enumToString( sw::SequenceItemKind::Clip ) );
    SW_EXPECT_STREQ( "Event", sw::engine::getSubsystem<sw::TypeRegistry>().enumToString( sw::SequenceItemKind::Event ) );
    SW_EXPECT_STREQ( "CameraCut", sw::engine::getSubsystem<sw::TypeRegistry>().enumToString( sw::SequenceItemKind::CameraCut ) );
    SW_EXPECT_NULL( sw::SequenceAsset::findItemKindInfo( sw::SequenceItemKind::Count ) );
    SW_EXPECT_NULL( sw::SequenceAsset::findItemKindInfo( static_cast<sw::SequenceItemKind>( -1 ) ) );
}

/**
 * @brief [SequencerTest] 시퀀스 파일이 같은 바이트로 왕복한다(종류는 열거자 이름, 키 트랙은 채널마다 키 배열), 모르는 종류 이름은 읽기 오류다
 * @details 아래 문서는 `toJSON` 출력이다. 종류는 리플렉션 열거자 이름이 정본이다 — 모르는 이름(새 버전의 종류, 옛 정수)을 읽어 두고
 *          적용만 하지 않던 예전 규칙은 없앴다(출시한 데이터가 없어 별칭 · 호환을 두지 않는다).
 */
SW_TEST_CASE( SequencerTest, SequenceFileRoundTripKeepsKindNames )
{
    const sw::string_view kSequenceJSON = R"({
  "_frameMin": 0,
  "_frameMax": 48,
  "_note": "golden",
  "_listItem": [
    {
      "_name": "Walk",
      "_targetObject": "Hero",
      "_preset": "",
      "_start": 0,
      "_end": 24,
      "_prerollFrames": -1,
      "_amplitude": 1,
      "_color": 4289364096,
      "_kind": "Clip"
    },
    {
      "_name": "Door",
      "_targetObject": "Gate",
      "_preset": "",
      "_start": 12,
      "_end": 12,
      "_prerollFrames": -1,
      "_amplitude": 1,
      "_color": 4286611626,
      "_kind": "Event"
    }
  ],
  "_listTrack": [
    {
      "_name": "Priority",
      "_targetObject": "Hero",
      "_propertyPath": "CameraComponent._priority",
      "_listChannel": [
        {
          "_listKey": [
            {
              "_time": 0,
              "_value": 1,
              "_arriveTangent": 0,
              "_leaveTangent": 0,
              "_interpolation": "Cubic",
              "_bAutoTangent": true
            },
            {
              "_time": 24,
              "_value": 3,
              "_arriveTangent": 0,
              "_leaveTangent": 0,
              "_interpolation": "Linear",
              "_bAutoTangent": true
            }
          ]
        }
      ],
      "_kind": "Property"
    }
  ],
  "_cameraBlendInSeconds": 0,
  "_cameraBlendOutSeconds": 0,
  "_cameraPrerollFrames": 0
})";

    sw::SequenceAsset asset;
    SW_ASSERT_TRUE( asset.parseJSON( kSequenceJSON ) );
    SW_ASSERT_EQUAL( size_t( 2 ), asset._listItem.size() );
    SW_EXPECT_TRUE( asset._listItem[0]._kind == sw::SequenceItemKind::Clip );
    SW_EXPECT_TRUE( asset._listItem[1]._kind == sw::SequenceItemKind::Event );
    SW_ASSERT_EQUAL( size_t( 1 ), asset._listTrack.size() );
    SW_EXPECT_TRUE( asset._listTrack[0]._kind == sw::SequenceTrackKind::Property );
    SW_EXPECT_TRUE( asset._listTrack[0]._listChannel[0]._listKey[1]._interpolation == sw::CurveInterpolation::Linear );
    SW_EXPECT_TRUE_MSG( asset.toJSON() == kSequenceJSON, "시퀀스 파일을 읽고 다시 쓰면 바이트가 달라진다" );

    test::ScopedDefensiveTestLog expected( "unknown sequence kind names and old keys are load errors" );
    sw::SequenceAsset            unknown;
    SW_EXPECT_FALSE( unknown.parseJSON( R"({ "_listItem": [ { "_name": "Future", "_kind": "Teleport" } ] })" ) );
    SW_EXPECT_FALSE( unknown.parseJSON( R"({ "frameMin": 0, "frameMax": 48, "items": [] })" ) );
    SW_EXPECT_FALSE( unknown.parseJSON( R"({ "_listTrack": [ { "_name": "Short", "_kind": "Transform", "_listChannel": [ { "_listKey": [] } ] } ] })" ) );
}

/**
 * @brief [SequencerTest] 저장소의 시퀀스 파일은 엔진이 쓰는 글 그대로다 — 읽고 다시 쓰면 바이트까지 같다
 * @details 시나리오(`sequencerkeys`)가 여는 파일이다. 변환한 파일이 저장 형식과 어긋나면 에디터에서 한 번 저장할 때마다 파일 전체가 바뀐다.
 */
SW_TEST_CASE( SequencerTest, RepositorySequenceResavesByteForByte )
{
    constexpr const utf8* kSequencePath = "engine/automation/editor/sequencerkeys/sequencerkeys.seq.json";
    sw::string            fileText;
    SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( kSequencePath, fileText ) );
    // core.autocrlf 체크아웃은 줄끝을 CRLF 로 바꾼다 — 저장소의 바이트(LF)로 비교한다.
    fileText.erase( std::remove( fileText.begin(), fileText.end(), '\r' ), fileText.end() );
    sw::SequenceAsset asset;
    SW_ASSERT_TRUE( asset.loadFromFile( kSequencePath ) );
    SW_ASSERT_EQUAL( size_t( 1 ), asset._listTrack.size() );
    SW_EXPECT_TRUE( asset._listTrack[0]._kind == sw::SequenceTrackKind::Transform );
    SW_EXPECT_EQUAL( size_t( sw::kSequenceTransformChannelCount ), asset._listTrack[0]._listChannel.size() );
    SW_EXPECT_STREQ( "GameCamera", asset._listTrack[0]._targetObject.c_str() );
    SW_EXPECT_STREQ( fileText.c_str(), asset.toJSON().c_str() );
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

    const sw::string xml = sw::ObjectStateSerializer::saveToXMLString( pSource, sw::ObjectSaveOptions{ sw::SerializePurpose::Authoring } );
    SW_ASSERT_FALSE( xml.empty() );
    sw::GameObject* pTarget = manager.createGameObject( sw::hashed_string{ "LoadedDirector" } );
    SW_ASSERT_NOT_NULL( pTarget );
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXMLString( pTarget, xml, sw::ObjectLoadContext{ sw::SerializePurpose::Authoring } ) );

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

// ------------------------------------------------------------------------------
// 키 트랙 · 카메라 컷
// ------------------------------------------------------------------------------

/**
 * @brief [SequencerTest] 같은 프레임에 키를 다시 찍으면 값만 바뀌고, 키 프레임은 채널을 겹쳐 하나로 세며, 지우면 모든 채널에서 빠진다
 */
SW_TEST_CASE( SequencerTest, KeyTrackSetKeyReplacesAndRemovesAcrossChannels )
{
    sw::SequenceKeyTrack track{};
    track._kind = sw::SequenceTrackKind::Transform;
    track.fitChannelsToKind();
    SW_ASSERT_EQUAL( size_t( sw::kSequenceTransformChannelCount ), track._listChannel.size() );

    float32 arrValue[sw::kSequenceTransformChannelCount] = { 1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
    track.setKey( 10.0f, arrValue, sw::kSequenceTransformChannelCount );
    arrValue[0] = 5.0f;
    track.setKey( 10.2f, arrValue, sw::kSequenceTransformChannelCount ); // 반 프레임 안 — 같은 키
    (void)track.setChannelKey( 1, 30.0f, 7.0f );                         // 채널 하나에만

    SW_EXPECT_EQUAL( size_t( 1 ), track._listChannel[0]._listKey.size() );
    SW_EXPECT_NEAR_EQUAL( 5.0f, track.evaluateChannel( 0, 10.0f ).value(), 0.0001f );
    sw::vector<float32> listFrame;
    track.collectKeyFrames( listFrame );
    SW_ASSERT_EQUAL( size_t( 2 ), listFrame.size() );
    SW_EXPECT_TRUE( track.hasKeyAt( 30.0f ) );
    SW_EXPECT_TRUE( track.findChannelKey( 0, 30.0f ) == sw::invalid_index::kUint32 );

    SW_EXPECT_TRUE( track.removeKeysAt( 10.0f ) );
    SW_EXPECT_TRUE( track._listChannel[0]._listKey.empty() );
    SW_EXPECT_EQUAL( size_t( 1 ), track._listChannel[1]._listKey.size() );
    SW_EXPECT_FALSE( track.removeKeysAt( 10.0f ) );
}

/**
 * @brief [SequencerTest] 트랜스폼 트랙은 키가 있는 채널만 몰고, 키 없는 채널 · 묶음은 대상의 지금 값을 둔다
 */
SW_TEST_CASE( SequencerTest, TransformTrackLeavesUnkeyedChannelsAlone )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pTarget = manager.createGameObject( sw::hashed_string{ "Mover" } );
    SW_ASSERT_NOT_NULL( pTarget );
    sw::SceneComponent* pScene = pTarget->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pScene );
    manager.mergePendingAdds();
    pScene->setLocalPosition( sw::float3{ 0.0f, 9.0f, 0.0f } );
    pScene->setLocalScale( sw::float3{ 2.0f, 2.0f, 2.0f } );

    sw::SequenceAsset    asset;
    sw::SequenceKeyTrack track{};
    track._targetObject = "Mover";
    track._kind         = sw::SequenceTrackKind::Transform;
    track.fitChannelsToKind();
    (void)track.setChannelKey( 0, 0.0f, 10.0f );
    (void)track.setChannelKey( 0, 10.0f, 20.0f );
    asset._listTrack.push_back( track );

    // 지금 값 읽기 — 에디터의 Key 단추가 이것을 키로 찍는다.
    float32 arrCurrent[sw::kSequenceTransformChannelCount]{};
    SW_ASSERT_TRUE( sw::SequenceTimelineUtil::readTrackValues( &manager, track, arrCurrent, sw::kSequenceTransformChannelCount ) );
    SW_EXPECT_NEAR_EQUAL( 9.0f, arrCurrent[1], 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, arrCurrent[6], 0.0001f );

    sw::SequenceTimelineUtil::applyFrame( &manager, asset, 5 );
    SW_EXPECT_NEAR_EQUAL( 15.0f, pScene->getLocalPosition()._x, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 9.0f, pScene->getLocalPosition()._y, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pScene->getLocalScale()._x, 0.0001f );
    // 끝 키 뒤는 끝 값이다.
    sw::SequenceTimelineUtil::applyFrame( &manager, asset, 40 );
    SW_EXPECT_NEAR_EQUAL( 20.0f, pScene->getLocalPosition()._x, 0.0001f );
}

/**
 * @brief [SequencerTest] 프로퍼티 트랙은 컴포넌트의 숫자 프로퍼티(int32 는 반올림)를 몰고, 숫자가 아닌 · 없는 프로퍼티는 읽지 못한다
 */
SW_TEST_CASE( SequencerTest, PropertyTrackDrivesNumericProperty )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pTarget = manager.createGameObject( sw::hashed_string{ "Lens" } );
    SW_ASSERT_NOT_NULL( pTarget );
    sw::CameraComponent* pCamera = pTarget->addComponent<sw::CameraComponent>();
    SW_ASSERT_NOT_NULL( pCamera );
    manager.mergePendingAdds();

    sw::SequenceAsset    asset;
    sw::SequenceKeyTrack track{};
    track._targetObject = "Lens";
    track._kind         = sw::SequenceTrackKind::Property;
    track._propertyPath = "CameraComponent._priority";
    track.fitChannelsToKind();
    (void)track.setChannelKey( 0, 0.0f, 0.0f );
    (void)track.setChannelKey( 0, 10.0f, 10.0f );
    track._listChannel[0]._listKey[0]._interpolation = sw::CurveInterpolation::Linear;
    asset._listTrack.push_back( track );

    sw::SequenceTimelineUtil::applyFrame( &manager, asset, 7 );
    SW_EXPECT_EQUAL( 7, pCamera->getPriority() );

    float32 value{ 0.0f };
    SW_EXPECT_TRUE( sw::SequenceTimelineUtil::readTrackValues( &manager, asset._listTrack[0], &value, 1 ) );
    SW_EXPECT_NEAR_EQUAL( 7.0f, value, 0.0001f );

    sw::SequenceKeyTrack wrong = track;
    wrong._propertyPath        = "CameraComponent._noSuchProperty";
    SW_EXPECT_FALSE( sw::SequenceTimelineUtil::readTrackValues( &manager, wrong, &value, 1 ) );
}

/**
 * @brief [SequencerTest] 리그 슬롯 트랙 — 재생이 대상 `PoseModifierComponent` 의 슬롯 가중치를 쓰고, 키를 찍을 때 읽는 값은 그 슬롯의 지금 가중치다(정하지 않으면 1)
 * @details 읽기가 늘 1 이면 슬롯을 0.25 로 둔 상태에서 키를 찍어도 1 이 키로 들어가 재생이 리그를 다시 켠다.
 */
SW_TEST_CASE( SequencerTest, RigSlotTrackReadsAndWritesTheSlotWeight )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pTarget = manager.createGameObject( sw::hashed_string{ "Rigged" } );
    SW_ASSERT_NOT_NULL( pTarget );
    sw::PoseModifierComponent* pModifier = pTarget->addComponent<sw::PoseModifierComponent>();
    SW_ASSERT_NOT_NULL( pModifier );
    manager.mergePendingAdds();

    sw::SequenceKeyTrack track{};
    track._targetObject = "Rigged";
    track._kind         = sw::SequenceTrackKind::RigSlot;
    track._propertyPath = "Aim";
    track.fitChannelsToKind();

    float32 value{ 0.0f };
    SW_ASSERT_TRUE( sw::SequenceTimelineUtil::readTrackValues( &manager, track, &value, 1 ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, value, 0.0001f );
    pModifier->setSlotWeight( sw::hashed_string{ "Aim" }, 0.25f );
    SW_ASSERT_TRUE( sw::SequenceTimelineUtil::readTrackValues( &manager, track, &value, 1 ) );
    SW_EXPECT_NEAR_EQUAL( 0.25f, value, 0.0001f );

    (void)track.setChannelKey( 0, 0.0f, 0.5f );
    sw::SequenceAsset asset;
    asset._listTrack.push_back( track );
    sw::SequenceTimelineUtil::applyFrame( &manager, asset, 3 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pModifier->getSlotWeight( sw::hashed_string{ "Aim" } ), 0.0001f );

    // 컴포넌트가 없는 대상은 읽지 못한다.
    sw::SequenceKeyTrack orphan = track;
    orphan._targetObject        = "Nobody";
    SW_EXPECT_FALSE( sw::SequenceTimelineUtil::readTrackValues( &manager, orphan, &value, 1 ) );
}

/**
 * @brief [SequencerTest] 카메라 컷 구간 안에서는 그 대상의 카메라가 컷 카메라이고, 구간 밖 · 멈춤에서는 풀린다
 * @details 컷이 없는 시퀀스는 게임 카메라를 건드리지 않는다 — 코드가 고른 컷을 다른 시퀀스가 지우지 않는다.
 */
SW_TEST_CASE( SequencerTest, CameraCutSelectsTargetCameraOnlyInsideItsSpan )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pShot = manager.createGameObject( sw::hashed_string{ "ShotCamera" } );
    SW_ASSERT_NOT_NULL( pShot );
    sw::CameraComponent* pCamera = pShot->addComponent<sw::CameraComponent>();
    SW_ASSERT_NOT_NULL( pCamera );
    manager.mergePendingAdds();

    sw::SequenceAsset asset;
    asset._frameMin = 0;
    asset._frameMax = 30;
    sw::SequenceTrackItem cut{};
    cut._name         = "Shot";
    cut._targetObject = "ShotCamera";
    cut._kind         = sw::SequenceItemKind::CameraCut;
    cut._start        = 10;
    cut._end          = 20;
    asset._listItem.push_back( cut );
    SW_ASSERT_TRUE( asset.hasCameraCut() );

    sw::SequenceTimelineUtil::applyFrame( &manager, asset, 15 );
    SW_EXPECT_TRUE( manager.getCameraRegistry().getCutCamera() == pCamera );
    // 컷 카메라 대상은 구간 밖에서 꺼지지 않는다(클립이 아니다).
    sw::SequenceTimelineUtil::applyFrame( &manager, asset, 25 );
    SW_EXPECT_NULL( manager.getCameraRegistry().getCutCamera() );
    SW_EXPECT_TRUE( pShot->isActive() );

    sw::SequenceTimelineUtil::applyFrame( &manager, asset, 12 );
    sw::SequenceTimelineUtil::releaseCameraCut( &manager, asset );
    SW_EXPECT_NULL( manager.getCameraRegistry().getCutCamera() );

    // 컷이 없는 시퀀스는 컷을 건드리지 않는다.
    manager.getCameraRegistry().setCutCamera( pCamera );
    sw::SequenceTimelineUtil::applyFrame( &manager, sw::makeSequenceWithFirstFrameEvent(), 5 );
    SW_EXPECT_TRUE( manager.getCameraRegistry().getCutCamera() == pCamera );
    manager.getCameraRegistry().setCutCamera( nullptr );
}

/**
 * @brief [SequencerTest] 카메라 신호 — 프리롤 전에 대기 뷰, 첫 컷에 컷 바뀜과 시네마틱 시작(블렌드 인), 컷 사이에 컷 바뀜만, 흔들림은 시작에, 마지막 컷이 끝나면 시네마틱 끝(블렌드 아웃)
 */
SW_TEST_CASE( SequencerTest, CameraSignalsFollowTheCutTrack )
{
    const sw::SequenceAsset              asset = sw::makeCameraSequence();
    sw::vector<sw::SequenceCameraSignal> listSignal;

    sw::SequenceTimelineUtil::collectCameraSignals( asset, 5, 7, listSignal ); // 10 − 3 = 7 을 지났다
    SW_ASSERT_EQUAL( size_t{ 1 }, listSignal.size() );
    SW_EXPECT_TRUE( listSignal[0]._kind == sw::SequenceCameraSignalKind::CutPrewarm );
    SW_EXPECT_EQUAL( 3, listSignal[0]._framesUntil );
    SW_EXPECT_TRUE( listSignal[0]._item._targetObject == "ShotA" );

    listSignal.clear();
    sw::SequenceTimelineUtil::collectCameraSignals( asset, 9, 10, listSignal );
    SW_ASSERT_EQUAL( size_t{ 2 }, listSignal.size() );
    SW_EXPECT_TRUE( listSignal[0]._kind == sw::SequenceCameraSignalKind::CutBegin ); // 프리셋을 먼저 켜고
    SW_EXPECT_TRUE( listSignal[0]._item._preset == "wide" );
    SW_EXPECT_TRUE( listSignal[1]._kind == sw::SequenceCameraSignalKind::CinematicBegin ); // 그 위에 게임 화면에서 섞는다
    SW_EXPECT_NEAR_EQUAL( 0.5f, listSignal[1]._blendSeconds, 1.0e-6f );

    listSignal.clear();
    sw::SequenceTimelineUtil::collectCameraSignals( asset, 14, 16, listSignal );
    SW_ASSERT_EQUAL( size_t{ 1 }, listSignal.size() );
    SW_EXPECT_TRUE( listSignal[0]._kind == sw::SequenceCameraSignalKind::Shake );
    SW_EXPECT_NEAR_EQUAL( 0.4f, listSignal[0]._item._amplitude, 1.0e-6f );

    listSignal.clear();
    sw::SequenceTimelineUtil::collectCameraSignals( asset, 20, 21, listSignal ); // 컷 사이 — 프리롤 0 이라 대기 뷰가 없다
    SW_ASSERT_EQUAL( size_t{ 1 }, listSignal.size() );
    SW_EXPECT_TRUE( listSignal[0]._kind == sw::SequenceCameraSignalKind::CutBegin );
    SW_EXPECT_TRUE( listSignal[0]._item._targetObject == "ShotB" );

    listSignal.clear();
    sw::SequenceTimelineUtil::collectCameraSignals( asset, 30, 31, listSignal );
    SW_ASSERT_EQUAL( size_t{ 1 }, listSignal.size() );
    SW_EXPECT_TRUE( listSignal[0]._kind == sw::SequenceCameraSignalKind::CinematicEnd );
    SW_EXPECT_NEAR_EQUAL( 0.75f, listSignal[0]._blendSeconds, 1.0e-6f );
    SW_EXPECT_TRUE( listSignal[0]._item._targetObject == "ShotB" );

    // 한 갱신에 모두 지나도 순서대로 다 낸다.
    listSignal.clear();
    sw::SequenceTimelineUtil::collectCameraSignals( asset, 0, 35, listSignal );
    SW_EXPECT_EQUAL( 1u, sw::countSignal( listSignal, sw::SequenceCameraSignalKind::Shake ) );
    SW_EXPECT_EQUAL( 0u, sw::countSignal( listSignal, sw::SequenceCameraSignalKind::CutPrewarm ) );     // 이미 지난 컷은 미리 준비하지 않는다
    SW_EXPECT_EQUAL( 0u, sw::countSignal( listSignal, sw::SequenceCameraSignalKind::CinematicBegin ) ); // 0 과 35 는 둘 다 컷 밖이다
}

/**
 * @brief [SequencerTest] 플레이어 컴포넌트는 카메라 신호에 초 단위 값을 채워 알리고, 컷 안에서 멈추면 시네마틱 끝을 낸다
 */
SW_TEST_CASE( SequencerTest, PlayerComponentBroadcastsCameraSignals )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pSequenceObject = manager.createGameObject( sw::hashed_string{ "Sequence" } );
    SW_ASSERT_NOT_NULL( pSequenceObject );
    sw::SequencePlayerComponent* pPlayer = pSequenceObject->addComponent<sw::SequencePlayerComponent>();
    SW_ASSERT_NOT_NULL( pPlayer );
    manager.mergePendingAdds();
    pPlayer->setSequence( sw::makeCameraSequence() );

    struct Recorder
    {
        sw::vector<sw::SequenceCameraSignal> _listSignal;
        void                                 record( const sw::SequenceCameraSignal& signal ) { _listSignal.push_back( signal ); }
    };
    Recorder                 recorder;
    const sw::DelegateHandle handle =
        pPlayer->registerCameraSignal( SW_DELEGATE_METHOD( sw::OnSequenceCameraSignalDelegate, &Recorder::record, &recorder ) );

    pPlayer->play();
    const float32 secondsPerFrame = 1.0f / pPlayer->getFramesPerSecond();
    for ( int32 frameIndex = 0; frameIndex < 16; ++frameIndex )
    {
        pPlayer->onTick( secondsPerFrame );
    }
    SW_EXPECT_EQUAL( 1u, sw::countSignal( recorder._listSignal, sw::SequenceCameraSignalKind::CutPrewarm ) );
    SW_EXPECT_EQUAL( 1u, sw::countSignal( recorder._listSignal, sw::SequenceCameraSignalKind::CinematicBegin ) );
    for ( const sw::SequenceCameraSignal& signal : recorder._listSignal )
    {
        if ( signal._kind == sw::SequenceCameraSignalKind::CutPrewarm )
            SW_EXPECT_NEAR_EQUAL( static_cast<float32>( signal._framesUntil ) * secondsPerFrame, signal._secondsUntil, 1.0e-5f );
        if ( signal._kind == sw::SequenceCameraSignalKind::Shake )
            SW_EXPECT_NEAR_EQUAL( 3.0f * secondsPerFrame, signal._durationSeconds, 1.0e-5f );
    }

    recorder._listSignal.clear();
    pPlayer->stop(); // 컷 안에서 멈춘다
    SW_ASSERT_EQUAL( size_t{ 1 }, recorder._listSignal.size() );
    SW_EXPECT_TRUE( recorder._listSignal[0]._kind == sw::SequenceCameraSignalKind::CinematicEnd );
    pPlayer->unregisterCameraSignal( handle );
}

/**
 * @brief [SequencerTest] 카메라 필드(프리셋 · 세기 · 프리롤 · 블렌드 인/아웃)는 JSON 으로 왕복하고, 다시 쓴 글은 바이트까지 같다
 */
SW_TEST_CASE( SequencerTest, CameraFieldsRoundTripThroughJSON )
{
    const sw::SequenceAsset source = sw::makeCameraSequence();
    sw::SequenceAsset       restored;
    SW_ASSERT_TRUE( restored.parseJSON( source.toJSON() ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, restored._cameraBlendInSeconds, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, restored._cameraBlendOutSeconds, 1.0e-6f );
    SW_EXPECT_EQUAL( 3, restored._cameraPrerollFrames );
    SW_ASSERT_EQUAL( size_t{ 3 }, restored._listItem.size() );
    SW_EXPECT_TRUE( restored._listItem[0]._preset == "wide" );
    SW_EXPECT_EQUAL( sw::SequenceTrackItem::kUseSequencePreroll, restored._listItem[0]._prerollFrames );
    SW_EXPECT_EQUAL( 0, restored._listItem[1]._prerollFrames );
    SW_EXPECT_TRUE( restored._listItem[2]._kind == sw::SequenceItemKind::CameraShake );
    SW_EXPECT_NEAR_EQUAL( 0.4f, restored._listItem[2]._amplitude, 1.0e-6f );
    SW_EXPECT_TRUE( restored.toJSON() == source.toJSON() );
}

/** 리그 슬롯 트랙 — 키가 없으면 1(슬롯 미설정), 키 값은 0..1 로 자른다. */
SW_TEST_CASE( SequencerTest, RigSlotTrackSamplesClampedWeight )
{
    sw::SequenceKeyTrack track{};
    track._kind         = sw::SequenceTrackKind::RigSlot;
    track._propertyPath = "Aim";
    track.fitChannelsToKind();
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( track._listChannel.size() ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, sw::SequenceTimelineUtil::sampleRigSlotWeight( track, 5.0f ), 0.0001f );

    (void)track.setChannelKey( 0, 0.0f, 0.25f );
    (void)track.setChannelKey( 0, 10.0f, 3.0f );
    (void)track.setChannelKey( 0, 20.0f, -1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, sw::SequenceTimelineUtil::sampleRigSlotWeight( track, 0.0f ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, sw::SequenceTimelineUtil::sampleRigSlotWeight( track, 10.0f ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, sw::SequenceTimelineUtil::sampleRigSlotWeight( track, 20.0f ), 0.0001f );
}
