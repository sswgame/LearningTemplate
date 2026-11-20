// 시퀀스 재생(오브젝트 있음, nogpu) — 바인딩으로 대상 찾기, 묶이지 않은 바인딩이면 틀지 않음, 끝내기 계약(되돌림 · 유지), 기록, 구간 처리기,
// 정수 틱 재생(속도 · 역방향 · 반복 · 왕복 · 범위 · 건너뛰기)과 발화 규칙(문턱을 지날 때마다 한 번).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Presentation/ISoundPlayer.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayer.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

using namespace sw;

namespace
{
    struct SequencePlaybackTestUtil
    {
        /** @brief 바인딩 Hero(1) 의 위치 X 를 0 프레임 5, 10 프레임 5 로 키 찍고, 같은 Hero 를 0..10 동안 켜는 클립이 있는 10 프레임 시퀀스입니다. */
        static SequenceAsset makeMoveSequence( SequenceFinishPolicy policy )
        {
            SequenceAsset asset;
            asset._frameMin          = 0;
            asset._frameMax          = 10;
            asset._whenFinished      = policy;
            const uint32      heroID = asset.addBinding( "Hero" );
            SequenceTrackItem clip{};
            clip._kind    = SequenceItemKind::Clip;
            clip._binding = heroID;
            clip._start   = 0;
            clip._end     = 10;
            asset._listItem.push_back( clip );
            SequenceKeyTrack track{};
            track._kind    = SequenceTrackKind::Transform;
            track._binding = heroID;
            track.fitChannelsToKind();
            (void)track.setChannelKey( 0, 0.0f, 5.0f );
            (void)track.setChannelKey( 0, 10.0f, 5.0f );
            asset._listTrack.push_back( track );
            return asset;
        }

        /** @brief 꺼진 Hero(위치 X 1) 와 플레이어를 단 Director 입니다. */
        static SequencePlayerComponent* makeScene( GameObjectManager& manager, GameObject*& outHero )
        {
            outHero                = manager.createGameObject( hashed_string( "Hero" ) );
            SceneComponent* pScene = outHero->addComponent<SceneComponent>();
            pScene->setLocalPosition( float3{ 1.0f, 0.0f, 0.0f } );
            outHero->setActive( false );
            GameObject* pDirector = manager.createGameObject( hashed_string( "Director" ) );
            return pDirector->addComponent<SequencePlayerComponent>();
        }

        /** @brief 플레이어를 한 틱 돌리고 그 틱 뒤 큐(묶인 오브젝트에 적용 · 완료 기록)를 비웁니다 — 엔진 프레임이 하는 순서다. */
        static void update( GameObjectManager& manager, SequencePlayerComponent& player, float32 deltaSeconds )
        {
            {
                TickContext context{ *player.getOwner(), deltaSeconds };
                player.onTick( context );
            }
            manager.update( 0.0f ); // 틱 뒤 큐를 비운다(시간은 흐르지 않는다)
        }

        static float32 readX( const GameObject& object )
        {
            return object.getPrimarySceneComponent()->getLocalPosition()._x;
        }
    };
} // namespace

/** @brief [SequencePlaybackTest] 바인딩으로 묶은 대상을 몰고, 멈추면 `RestoreState` 가 재생 전의 위치 · 활성으로 되돌린다 */
SW_TEST_CASE( SequencePlaybackTest, StopRestoresTheStateBeforePlay )
{
    using Util = SequencePlaybackTestUtil;
    GameObjectManager        manager;
    GameObject*              pHero   = nullptr;
    SequencePlayerComponent* pPlayer = Util::makeScene( manager, pHero );
    SW_REQUIRE_NOT_NULL( pPlayer );
    pPlayer->setSequence( Util::makeMoveSequence( SequenceFinishPolicy::RestoreState ) );
    pPlayer->setBinding( 1, pHero->getHandle() );

    pPlayer->play();
    SW_REQUIRE_TRUE( pPlayer->isPlaying() );
    Util::update( manager, *pPlayer, 0.1f );
    SW_EXPECT_NEAR( 5.0f, Util::readX( *pHero ), 1e-4f );
    SW_EXPECT_TRUE( pHero->isActive() );

    pPlayer->stop();
    SW_EXPECT_FALSE( pPlayer->isPlaying() );
    SW_EXPECT_NEAR( 1.0f, Util::readX( *pHero ), 1e-4f );
    SW_EXPECT_FALSE( pHero->isActive() );
    SW_EXPECT_EQUAL( 1u, pPlayer->getHistory().countKind( SequenceHistoryKind::Started ) );
    SW_EXPECT_EQUAL( 1u, pPlayer->getHistory().countKind( SequenceHistoryKind::Stopped ) );
}

/** @brief [SequencePlaybackTest] `KeepState` 는 끝 프레임의 상태를 남긴다 — 정상 완료도 같은 끝내기를 지나고 기록에 Finished 가 남는다 */
SW_TEST_CASE( SequencePlaybackTest, KeepStateLeavesTheLastFrame )
{
    using Util = SequencePlaybackTestUtil;
    GameObjectManager        manager;
    GameObject*              pHero   = nullptr;
    SequencePlayerComponent* pPlayer = Util::makeScene( manager, pHero );
    SW_REQUIRE_NOT_NULL( pPlayer );
    pPlayer->setSequence( Util::makeMoveSequence( SequenceFinishPolicy::KeepState ) );
    pPlayer->setBinding( 1, pHero->getHandle() );

    pPlayer->play();
    for ( int32 tickIndex = 0; tickIndex < 20 && pPlayer->isPlaying(); ++tickIndex )
    {
        Util::update( manager, *pPlayer, 0.05f );
    }
    SW_EXPECT_FALSE( pPlayer->isPlaying() );
    SW_EXPECT_NEAR( 5.0f, Util::readX( *pHero ), 1e-4f );
    SW_EXPECT_TRUE( pHero->isActive() );
    SW_EXPECT_EQUAL( 1u, pPlayer->getHistory().countKind( SequenceHistoryKind::Finished ) );
}

/** @brief [SequencePlaybackTest] 정상 완료에서도 `RestoreState` 가 되돌린다 — 다시 틀면 먼저 끝내고 새 시작 상태를 잰다 */
SW_TEST_CASE( SequencePlaybackTest, FinishAndReplayRestore )
{
    using Util = SequencePlaybackTestUtil;
    GameObjectManager        manager;
    GameObject*              pHero   = nullptr;
    SequencePlayerComponent* pPlayer = Util::makeScene( manager, pHero );
    SW_REQUIRE_NOT_NULL( pPlayer );
    pPlayer->setSequence( Util::makeMoveSequence( SequenceFinishPolicy::RestoreState ) );
    pPlayer->setBinding( 1, pHero->getHandle() );

    pPlayer->play();
    Util::update( manager, *pPlayer, 0.1f );
    pPlayer->play(); // 다시 틀기 — 앞 재생을 끝내고(되돌림) 다시 시작한다
    SW_EXPECT_EQUAL( 1u, pPlayer->getHistory().countKind( SequenceHistoryKind::Stopped ) );
    for ( int32 tickIndex = 0; tickIndex < 20 && pPlayer->isPlaying(); ++tickIndex )
    {
        Util::update( manager, *pPlayer, 0.05f );
    }
    SW_EXPECT_NEAR( 1.0f, Util::readX( *pHero ), 1e-4f );
    SW_EXPECT_FALSE( pHero->isActive() );
}

/** @brief [SequencePlaybackTest] 재생에 필요한 바인딩이 묶이지 않았으면 오류를 남기고 틀지 않는다 — 이름이 같은 오브젝트가 있어도 찾지 않는다 */
SW_TEST_CASE( SequencePlaybackTest, UnboundBindingDoesNotPlay )
{
    using Util = SequencePlaybackTestUtil;
    SW_EXPECT_LOG( "SequencePlayer.BindingUnbound" );
    GameObjectManager        manager;
    GameObject*              pHero   = nullptr;
    SequencePlayerComponent* pPlayer = Util::makeScene( manager, pHero );
    SW_REQUIRE_NOT_NULL( pPlayer );
    pPlayer->setSequence( Util::makeMoveSequence( SequenceFinishPolicy::RestoreState ) );

    pPlayer->play();
    SW_EXPECT_FALSE( pPlayer->isPlaying() );
    SW_EXPECT_NEAR( 1.0f, Util::readX( *pHero ), 1e-4f );
    SW_EXPECT_EQUAL( 1u, pPlayer->getHistory().countKind( SequenceHistoryKind::UnboundBinding ) );
    SW_EXPECT_TRUE( pPlayer->findBoundObject( 1 ) == nullptr );

    pPlayer->setBinding( 1, pHero->getHandle() );
    SW_EXPECT_TRUE( pPlayer->findBoundObject( 1 ) == pHero );
    pPlayer->setBinding( 1, GameObjectHandle{} ); // 무효 핸들은 칸을 지운다
    SW_EXPECT_TRUE( pPlayer->getBindings().empty() );
}

/** @brief [SequencePlaybackTest] 같은 시퀀스를 다른 오브젝트에 묶어 다시 쓴다 — 바인딩은 플레이어(씬)의 것이다 */
SW_TEST_CASE( SequencePlaybackTest, SameSequenceDrivesAnotherBinding )
{
    using Util = SequencePlaybackTestUtil;
    GameObjectManager manager;
    GameObject*       pHero = nullptr;
    (void)Util::makeScene( manager, pHero );
    GameObject* pDouble = manager.createGameObject( hashed_string( "Double" ) );
    (void)pDouble->addComponent<SceneComponent>();
    GameObject*              pOtherDirector = manager.createGameObject( hashed_string( "OtherDirector" ) );
    SequencePlayerComponent* pOther         = pOtherDirector->addComponent<SequencePlayerComponent>();
    pOther->setSequence( Util::makeMoveSequence( SequenceFinishPolicy::KeepState ) );
    pOther->setBinding( 1, pDouble->getHandle() );

    pOther->play();
    Util::update( manager, *pOther, 0.1f );
    SW_EXPECT_NEAR( 5.0f, Util::readX( *pDouble ), 1e-4f );
    SW_EXPECT_NEAR( 1.0f, Util::readX( *pHero ), 1e-4f );
}

/** @brief [SequencePlaybackTest] 렌즈 트랙은 키가 있는 채널만 카메라에 쓰고, 되돌리면 재생 전 렌즈로 돌아간다 */
SW_TEST_CASE( SequencePlaybackTest, LensTrackKeysTheCameraAndRestores )
{
    GameObjectManager manager;
    GameObject*       pCut    = manager.createGameObject( hashed_string( "Cut" ) );
    CameraComponent*  pCamera = pCut->addComponent<CameraComponent>();
    SW_REQUIRE_NOT_NULL( pCamera );
    pCamera->setFieldOfViewY( 1.0f );
    GameObject*              pDirector = manager.createGameObject( hashed_string( "Director" ) );
    SequencePlayerComponent* pPlayer   = pDirector->addComponent<SequencePlayerComponent>();

    SequenceAsset asset;
    asset._frameMax     = 10;
    asset._whenFinished = SequenceFinishPolicy::RestoreState;
    SequenceKeyTrack lens{};
    lens._kind    = SequenceTrackKind::CameraLens;
    lens._binding = asset.addBinding( "Cut" );
    lens.fitChannelsToKind();
    (void)lens.setChannelKey( 0, 0.0f, 0.5f ); // 화각
    (void)lens.setChannelKey( 2, 0.0f, 2.8f ); // 조리개
    asset._listTrack.push_back( lens );
    pPlayer->setSequence( asset );
    pPlayer->setBinding( lens._binding, pCut->getHandle() );

    pPlayer->play();
    SW_EXPECT_NEAR( 0.5f, pCamera->getFieldOfViewY(), 1e-4f );
    SW_EXPECT_NEAR( 2.8f, pCamera->getDepthOfField()._aperture, 1e-4f );
    SW_EXPECT_NEAR( 10.0f, pCamera->getDepthOfField()._focusDistance, 1e-4f ); // 키 없는 초점은 그대로다
    pPlayer->stop();
    SW_EXPECT_NEAR( 1.0f, pCamera->getFieldOfViewY(), 1e-4f );
    SW_EXPECT_NEAR( 0.0f, pCamera->getDepthOfField()._aperture, 1e-4f );
}

/** @brief [SequencePlaybackTest] 구간 처리기: 애니메이터가 없는 대상의 애니메이션 항목은 들어갈 때 오류이고, 나갈 때까지 상태가 남았다가 멈추면 빈다 */
SW_TEST_CASE( SequencePlaybackTest, AnimationSpanNeedsAnAnimator )
{
    SW_EXPECT_LOG( "SequenceSpan.AnimationNoAnimator" );
    GameObjectManager manager;
    GameObject*       pHero = manager.createGameObject( hashed_string( "Hero" ) );
    SequenceAsset     asset;
    asset._frameMax     = 10;
    asset._whenFinished = SequenceFinishPolicy::RestoreState;
    SequenceTrackItem animation{};
    animation._kind    = SequenceItemKind::Animation;
    animation._binding = asset.addBinding( "Hero" );
    animation._clip    = "game/none.animclip";
    animation._start   = 2;
    animation._end     = 6;
    asset._listItem.push_back( animation );

    vector<SequenceBindingSlot> listSlot( 1 );
    listSlot[0]._binding = animation._binding;
    listSlot[0]._object  = pHero->getHandle();
    vector<SequenceSpawnedObject> listSpawned;
    SequenceBindingView           view;
    view._pAsset       = &asset;
    view._pListSlot    = &listSlot;
    view._pListSpawned = &listSpawned;
    vector<SequenceSpanState> listSpan;
    SequenceTimelineUtil::applySpans( &manager, asset, view, 1, 30.0f, 1.0f, listSpan );
    SW_EXPECT_TRUE( listSpan.empty() ); // 아직 들어가지 않았다
    SequenceTimelineUtil::applySpans( &manager, asset, view, 3, 30.0f, 1.0f, listSpan );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listSpan.size() );
    SW_EXPECT_TRUE( listSpan[0]._bInside == SW_TRUE );
    SequenceTimelineUtil::applySpans( &manager, asset, view, 8, 30.0f, 1.0f, listSpan );
    SW_EXPECT_TRUE( listSpan[0]._bInside == SW_FALSE );
    SequenceTimelineUtil::releaseSpans( &manager, asset, view, 30.0f, listSpan );
    SW_EXPECT_TRUE( listSpan.empty() );
}

/** @brief [SequencePlaybackTest] 기록은 고정 용량이라 넘치면 가장 오래된 줄을 덮고, 누적 수는 이어 센다 */
SW_TEST_CASE( SequencePlaybackTest, HistoryIsBounded )
{
    SequencePlaybackHistory history;
    for ( uint32 index = 0; index < SequencePlaybackHistory::kCapacity + 5; ++index )
    {
        history.record( SequenceHistoryKind::Event, static_cast<int32>( index ), 0, "Beat" );
    }
    SW_EXPECT_EQUAL( SequencePlaybackHistory::kCapacity, history.getCount() );
    SW_EXPECT_EQUAL( uint64{ SequencePlaybackHistory::kCapacity + 5 }, history.getRecordedCount() );
    SW_EXPECT_EQUAL( 5, history.getEntry( 0 )._frame );
    SW_REQUIRE_NOT_NULL( history.findLatest() );
    SW_EXPECT_EQUAL( static_cast<int32>( SequencePlaybackHistory::kCapacity + 4 ), history.findLatest()->_frame );
    history.clear();
    SW_EXPECT_EQUAL( 0u, history.getCount() );
    SW_EXPECT_TRUE( history.findLatest() == nullptr );
}

// 일시정지한 갱신은 빈 구간이다 — 예전에는 플레이어의 "이전 프레임" 이 그대로 남아 직전에 지난 이벤트 · 카메라 신호가 매 틱 다시 났다(P0-12).
// 구간을 값으로 돌려받으므로 멈춘 틱은 구조상 아무것도 지나지 않는다(이벤트 판정은 `previousFrame < start <= frame`).
SW_TEST_CASE( SequencePlaybackTest, PausedPlayerStepsOverNothing )
{
    SequenceAsset asset;
    asset._frameMin = 0;
    asset._frameMax = 30;
    SequencePlayer player;
    player.setAsset( asset );
    player.setFramesPerSecond( 10.0f );
    player.play();
    const SequencePlaybackStep moved = player.update( 0.55f );
    SW_REQUIRE_TRUE( moved._previousFrame < moved._frame );

    player.pause();
    for ( int32 tick = 0; tick < 3; ++tick )
    {
        const SequencePlaybackStep paused = player.update( 0.1f );
        SW_EXPECT_EQUAL( paused._previousFrame, paused._frame );
        SW_EXPECT_EQUAL( moved._frame, paused._frame );
    }
    player.resume();
    const SequencePlaybackStep resumed = player.update( 0.1f );
    SW_EXPECT_EQUAL( moved._frame, resumed._previousFrame ); // 이어서 지난 구간만 본다(멈춘 동안의 구간을 다시 보지 않는다)
    SW_EXPECT_TRUE( resumed._frame > resumed._previousFrame );
}

namespace
{
    struct SequenceStepTestUtil
    {
        /** @brief 0 · 5 · 10 프레임에 이벤트가 있는 0..10 시퀀스입니다(fps 10 이면 1 초). */
        static SequenceAsset makeEventSequence()
        {
            SequenceAsset asset;
            asset._frameMin = 0;
            asset._frameMax = 10;
            for ( const int32 frame : { 0, 5, 10 } )
            {
                SequenceTrackItem item{};
                item._kind  = SequenceItemKind::Event;
                item._name  = frame == 0 ? "E0" : ( frame == 5 ? "E5" : "E10" );
                item._start = frame;
                item._end   = frame;
                asset._listItem.push_back( item );
            }
            return asset;
        }

        /** @brief 갱신 하나가 지난 이벤트의 시작 프레임들입니다(관리자 없이 — 문턱 판정만 본다). */
        static vector<int32> collectFired( const SequenceAsset& asset, const SequencePlaybackStep& step )
        {
            vector<const SequenceTrackItem*> listCrossed;
            SequenceTimelineUtil::applyPlayback( nullptr, asset, step, SequenceBindingView{}, &listCrossed );
            vector<int32> listFrame;
            for ( const SequenceTrackItem* pItem : listCrossed )
                listFrame.push_back( pItem->_start );
            return listFrame;
        }

        static void append( vector<int32>& inoutList, const vector<int32>& listMore ) { inoutList.insert( inoutList.end(), listMore.begin(), listMore.end() ); }
    };
} // namespace

/** @brief [SequencePlaybackTest] 정방향: 처음 프레임(시작 갱신)부터 끝 프레임까지 이벤트마다 한 번, 끝에서 멈춘다 */
SW_TEST_CASE( SequencePlaybackTest, ForwardFiresEveryEventOnce )
{
    using Util           = SequenceStepTestUtil;
    SequenceAsset  asset = Util::makeEventSequence();
    SequencePlayer player;
    player.setAsset( asset );
    player.setFramesPerSecond( 10.0f );
    player.play();
    vector<int32> listFired = Util::collectFired( asset, player.getStartStep() );
    for ( int32 tick = 0; tick < 100 && player.isPlaying(); ++tick )
        Util::append( listFired, Util::collectFired( asset, player.update( 0.05f ) ) );
    SW_EXPECT_FALSE( player.isPlaying() );
    SW_EXPECT_EQUAL( 10, player.getCurrentFrame() );
    SW_REQUIRE_EQUAL( size_t( 3 ), listFired.size() );
    SW_EXPECT_EQUAL( 0, listFired[0] );
    SW_EXPECT_EQUAL( 5, listFired[1] );
    SW_EXPECT_EQUAL( 10, listFired[2] );
}

/** @brief [SequencePlaybackTest] 역재생: 끝에서 시작해 10 · 5 · 0 차례로 한 번씩 쏘고 처음 프레임에서 멈춘다(역방향 문턱 `to <= 시작 < from`) */
SW_TEST_CASE( SequencePlaybackTest, ReverseFiresEveryEventOnceFromTheEnd )
{
    using Util           = SequenceStepTestUtil;
    SequenceAsset  asset = Util::makeEventSequence();
    SequencePlayer player;
    player.setAsset( asset );
    player.setFramesPerSecond( 10.0f );
    player.playReverse();
    SW_EXPECT_TRUE( player.isReversed() );
    SW_EXPECT_EQUAL( 10, player.getCurrentFrame() );
    vector<int32> listFired = Util::collectFired( asset, player.getStartStep() );
    for ( int32 tick = 0; tick < 100 && player.isPlaying(); ++tick )
        Util::append( listFired, Util::collectFired( asset, player.update( 0.05f ) ) );
    SW_EXPECT_EQUAL( 0, player.getCurrentFrame() );
    SW_REQUIRE_EQUAL( size_t( 3 ), listFired.size() );
    SW_EXPECT_EQUAL( 10, listFired[0] );
    SW_EXPECT_EQUAL( 5, listFired[1] );
    SW_EXPECT_EQUAL( 0, listFired[2] );
}

/** @brief [SequencePlaybackTest] 큰 dt 하나가 여러 문턱을 지나면 그 사이 이벤트를 모두 한 번씩 쏜다 */
SW_TEST_CASE( SequencePlaybackTest, LargeStepFiresEveryCrossedEventOnce )
{
    using Util           = SequenceStepTestUtil;
    SequenceAsset  asset = Util::makeEventSequence();
    SequencePlayer player;
    player.setAsset( asset );
    player.setFramesPerSecond( 10.0f );
    player.play();
    (void)Util::collectFired( asset, player.getStartStep() );
    const vector<int32> listFired = Util::collectFired( asset, player.update( 5.0f ) ); // 5 · 10 을 한 번에 지나 끝난다
    SW_REQUIRE_EQUAL( size_t( 2 ), listFired.size() );
    SW_EXPECT_EQUAL( 5, listFired[0] );
    SW_EXPECT_EQUAL( 10, listFired[1] );
    SW_EXPECT_FALSE( player.isPlaying() );
}

/** @brief [SequencePlaybackTest] 반복: 끝을 넘는 갱신은 끝 구간(10) 다음 되감은 처음 구간(0)을 쏜다 — 경계의 두 이벤트가 한 번씩 */
SW_TEST_CASE( SequencePlaybackTest, LoopWrapFiresTheBoundaryEventsOnce )
{
    using Util           = SequenceStepTestUtil;
    SequenceAsset  asset = Util::makeEventSequence();
    SequencePlayer player;
    player.setAsset( asset );
    player.setFramesPerSecond( 10.0f );
    player.setLoopMode( SequenceLoopMode::Loop );
    player.play();
    (void)Util::collectFired( asset, player.getStartStep() );
    const vector<int32> listFirst = Util::collectFired( asset, player.update( 0.55f ) ); // 0 → 5.5
    SW_REQUIRE_EQUAL( size_t( 1 ), listFirst.size() );
    SW_EXPECT_EQUAL( 5, listFirst[0] );
    const SequencePlaybackStep wrap     = player.update( 0.55f ); // 5.5 → 11 → 되감아 1
    const vector<int32>        listWrap = Util::collectFired( asset, wrap );
    SW_EXPECT_EQUAL( 2u, wrap._segmentCount );
    SW_REQUIRE_EQUAL( size_t( 2 ), listWrap.size() );
    SW_EXPECT_EQUAL( 10, listWrap[0] );
    SW_EXPECT_EQUAL( 0, listWrap[1] );
    SW_EXPECT_EQUAL( 1, player.getCurrentFrame() );
    SW_EXPECT_TRUE( player.isPlaying() );
    SW_EXPECT_TRUE( Util::collectFired( asset, player.update( 0.2f ) ).empty() ); // 1 → 3
}

/** @brief [SequencePlaybackTest] 왕복: 끝에서 꺾여 돌아오며 꺾인 끝 프레임은 한 번만 쏘고, 돌아오는 길에 5 · 0 을 쏜 뒤 다시 정방향이다 */
SW_TEST_CASE( SequencePlaybackTest, PingPongTurnsOnceAtEachEnd )
{
    using Util           = SequenceStepTestUtil;
    SequenceAsset  asset = Util::makeEventSequence();
    SequencePlayer player;
    player.setAsset( asset );
    player.setFramesPerSecond( 10.0f );
    player.setLoopMode( SequenceLoopMode::PingPong );
    player.play();
    (void)Util::collectFired( asset, player.getStartStep() );
    (void)Util::collectFired( asset, player.update( 0.55f ) );                          // 0 → 5.5 (5)
    const vector<int32> listTurn = Util::collectFired( asset, player.update( 0.55f ) ); // 5.5 → 11 → 꺾여 9
    SW_REQUIRE_EQUAL( size_t( 1 ), listTurn.size() );
    SW_EXPECT_EQUAL( 10, listTurn[0] );
    SW_EXPECT_TRUE( player.isReversed() );
    SW_EXPECT_EQUAL( 9, player.getCurrentFrame() );
    const vector<int32> listBack = Util::collectFired( asset, player.update( 0.5f ) ); // 9 → 4
    SW_REQUIRE_EQUAL( size_t( 1 ), listBack.size() );
    SW_EXPECT_EQUAL( 5, listBack[0] );
    const vector<int32> listStart = Util::collectFired( asset, player.update( 0.5f ) ); // 4 → -1 → 꺾여 1
    SW_REQUIRE_EQUAL( size_t( 1 ), listStart.size() );
    SW_EXPECT_EQUAL( 0, listStart[0] );
    SW_EXPECT_FALSE( player.isReversed() );
    SW_EXPECT_EQUAL( 1, player.getCurrentFrame() );
    SW_EXPECT_NEAR( 1.0f, player.getPlayRate(), 1e-6f ); // 속도 값은 그대로 — 방향 표지만 뒤집혔다
}

/** @brief [SequencePlaybackTest] 속도: 30 프레임(fps 30)을 60 Hz 갱신으로 2 배속은 30 번, 0.5 배속은 120 번에 끝낸다(정수 틱이라 정확하다) */
SW_TEST_CASE( SequencePlaybackTest, PlayRateScalesTheUpdatesToTheEnd )
{
    SequenceAsset asset;
    asset._frameMin = 0;
    asset._frameMax = 30;
    for ( const float32 rate : { 2.0f, 0.5f } )
    {
        SequencePlayer player;
        player.setAsset( asset );
        player.setFramesPerSecond( 30.0f );
        SW_REQUIRE_TRUE( player.setPlayRate( rate ) );
        player.play();
        int32 updateCount = 0;
        while ( player.isPlaying() && updateCount < 1000 )
        {
            (void)player.update( 1.0f / 60.0f );
            ++updateCount;
        }
        SW_EXPECT_EQUAL( rate > 1.0f ? 30 : 120, updateCount );
        SW_EXPECT_EQUAL( 30, player.getCurrentFrame() );
    }
}

/** @brief [SequencePlaybackTest] 속도 0 · 무한대는 오류를 남기고 거절한다 — 앞 속도가 남는다 */
SW_TEST_CASE( SequencePlaybackTest, ZeroPlayRateIsRejected )
{
    SW_EXPECT_LOG( "SequencePlayer.PlayRateInvalid" );
    SequencePlayer player;
    SW_EXPECT_FALSE( player.setPlayRate( 0.0f ) );
    SW_EXPECT_FALSE( player.setPlayRate( std::numeric_limits<float32>::infinity() ) );
    SW_EXPECT_NEAR( 1.0f, player.getPlayRate(), 1e-6f );
}

/** @brief [SequencePlaybackTest] 재생 범위: 3..7 만 돌아 그 안의 이벤트(5)만 쏘고 7 에서 끝난다. 거꾸로 된 범위 · 시퀀스 밖 범위는 거절한다 */
SW_TEST_CASE( SequencePlaybackTest, PlaybackRangeLimitsThePlayback )
{
    SW_EXPECT_LOG( "SequencePlayer.PlaybackRangeInvalid" );
    using Util           = SequenceStepTestUtil;
    SequenceAsset  asset = Util::makeEventSequence();
    SequencePlayer player;
    player.setAsset( asset );
    player.setFramesPerSecond( 10.0f );
    SW_REQUIRE_TRUE( player.setPlaybackRange( 3, 7 ) );
    SW_EXPECT_FALSE( player.setPlaybackRange( 7, 3 ) );
    SW_EXPECT_FALSE( player.setPlaybackRange( 0, 11 ) );
    player.play();
    SW_EXPECT_EQUAL( 3, player.getCurrentFrame() );
    vector<int32> listFired = Util::collectFired( asset, player.getStartStep() );
    for ( int32 tick = 0; tick < 100 && player.isPlaying(); ++tick )
        Util::append( listFired, Util::collectFired( asset, player.update( 0.05f ) ) );
    SW_EXPECT_EQUAL( 7, player.getCurrentFrame() );
    SW_REQUIRE_EQUAL( size_t( 1 ), listFired.size() );
    SW_EXPECT_EQUAL( 5, listFired[0] );
    player.setAsset( asset ); // 에셋을 다시 주면 전체 범위다
    SW_EXPECT_EQUAL( 0, player.getPlaybackStart() );
    SW_EXPECT_EQUAL( 10, player.getPlaybackEnd() );
}

/** @brief [SequencePlaybackTest] 끝으로 건너뛰기는 점프다 — 구간이 없어 사이 이벤트를 쏘지 않고, 끝 프레임에 멈춘다(역방향이면 처음 프레임) */
SW_TEST_CASE( SequencePlaybackTest, SkipToEndJumpsWithoutFiring )
{
    using Util           = SequenceStepTestUtil;
    SequenceAsset  asset = Util::makeEventSequence();
    SequencePlayer player;
    player.setAsset( asset );
    player.setFramesPerSecond( 10.0f );
    player.play();
    (void)player.update( 0.15f );
    const SequencePlaybackStep jump = player.skipToEnd();
    SW_EXPECT_TRUE( jump._kind == SequencePlaybackStepKind::Jump );
    SW_EXPECT_EQUAL( 0u, jump._segmentCount );
    SW_EXPECT_EQUAL( 10, jump._frame );
    SW_EXPECT_TRUE( Util::collectFired( asset, jump ).empty() );
    SW_EXPECT_FALSE( player.isPlaying() );
    player.playReverse();
    (void)player.update( 0.15f );
    SW_EXPECT_EQUAL( 0, player.skipToEnd()._frame );
}

/** @brief [SequencePlaybackTest] 정수 틱이라 오래 반복해도 어긋나지 않는다 — 30 프레임 반복을 60 Hz 로 590 번(9 바퀴 + 25 프레임) 돌면 정확히 25 프레임이다 */
SW_TEST_CASE( SequencePlaybackTest, IntegerTicksDoNotDrift )
{
    SequenceAsset asset;
    asset._frameMin = 0;
    asset._frameMax = 30;
    SequencePlayer player;
    player.setAsset( asset );
    player.setFramesPerSecond( 30.0f );
    player.setLoopMode( SequenceLoopMode::Loop );
    player.play();
    for ( int32 tick = 0; tick < 590; ++tick )
        (void)player.update( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 25, player.getCurrentFrame() );
    SW_EXPECT_TRUE( player.isPlaying() );
}

/** @brief [SequencePlaybackTest] 역재생은 컷 순서도 뒤집는다 — 끝에서 들어오며 CutB 가 먼저, CutA 가 뒤다 */
SW_TEST_CASE( SequencePlaybackTest, ReverseCameraCutsComeInReverseOrder )
{
    SequenceAsset asset;
    asset._frameMin   = 0;
    asset._frameMax   = 10;
    const uint32 cutA = asset.addBinding( "CutA" );
    const uint32 cutB = asset.addBinding( "CutB" );
    for ( const uint32 binding : { cutA, cutB } )
    {
        SequenceTrackItem cut{};
        cut._kind    = SequenceItemKind::CameraCut;
        cut._binding = binding;
        cut._name    = binding == cutA ? "A" : "B";
        cut._start   = binding == cutA ? 2 : 6;
        cut._end     = binding == cutA ? 4 : 8;
        asset._listItem.push_back( cut );
    }
    SequencePlayer player;
    player.setAsset( asset );
    player.setFramesPerSecond( 10.0f );
    player.playReverse();
    vector<SequenceCameraSignal> listSignal;
    SequenceTimelineUtil::collectPlaybackCameraSignals( asset, player.getStartStep(), listSignal );
    for ( int32 tick = 0; tick < 100 && player.isPlaying(); ++tick )
        SequenceTimelineUtil::collectPlaybackCameraSignals( asset, player.update( 0.05f ), listSignal );
    vector<string> listCut;
    uint32         beginCount = 0;
    uint32         endCount   = 0;
    for ( const SequenceCameraSignal& signal : listSignal )
    {
        if ( signal._kind == SequenceCameraSignalKind::CutBegin )
            listCut.push_back( signal._item._name );
        beginCount += signal._kind == SequenceCameraSignalKind::CinematicBegin ? 1u : 0u;
        endCount += signal._kind == SequenceCameraSignalKind::CinematicEnd ? 1u : 0u;
    }
    SW_REQUIRE_EQUAL( size_t( 2 ), listCut.size() );
    SW_EXPECT_TRUE( listCut[0] == "B" );
    SW_EXPECT_TRUE( listCut[1] == "A" );
    // 컷 사이(5 프레임)는 덮는 컷이 없어 시네마틱이 두 번 열리고 닫힌다
    SW_EXPECT_EQUAL( 2u, beginCount );
    SW_EXPECT_EQUAL( 2u, endCount );
}

/** @brief [SequencePlaybackTest] 구간은 상태다 — 한 갱신에 통째로 지나친 구간은 들어가지 않고, 역방향으로 들어가면 한 번 들어가 한 번 나간다 */
SW_TEST_CASE( SequencePlaybackTest, SpansFollowTheCurrentFrame )
{
    GameObjectManager manager;
    // 소리 이벤트 "Beat" 는 등록되지 않았다 — 소리 재생기가 있는 구성(오디오 엔진)이면 구간이 들어갈 때 모르는 이벤트 오류를 한 번 낸다. 이 케이스는 구간 상태만 본다.
    // 구간 처리기와 같은 길(장면의 엔진 서비스)로 찾는다 — 오디오가 없는 구성(ASan nogpu)은 소리를 내지 않아 로그도 없다.
    if ( locateSubsystem<ISoundPlayer>( manager.getSceneEngineServices()._pEngineSubsystems ) != nullptr )
        SW_EXPECT_LOG( "AudioEngine.UnknownAudioEvent" );
    SequenceAsset asset;
    asset._frameMax = 10;
    SequenceTrackItem audio{};
    audio._kind  = SequenceItemKind::Audio;
    audio._sound = "Beat";
    audio._start = 2;
    audio._end   = 4;
    asset._listItem.push_back( audio );
    vector<SequenceBindingSlot>   listSlot;
    vector<SequenceSpawnedObject> listSpawned;
    SequenceBindingView           view;
    view._pAsset       = &asset;
    view._pListSlot    = &listSlot;
    view._pListSpawned = &listSpawned;
    vector<SequenceSpanState>        listSpan;
    vector<const SequenceTrackItem*> listEntered;
    SequenceTimelineUtil::applySpans( &manager, asset, view, 1, 30.0f, 1.0f, listSpan, &listEntered );
    SequenceTimelineUtil::applySpans( &manager, asset, view, 8, 30.0f, 1.0f, listSpan, &listEntered ); // 2..4 를 통째로 지나쳤다
    SW_EXPECT_TRUE( listEntered.empty() );
    SequenceTimelineUtil::applySpans( &manager, asset, view, 3, 30.0f, -1.0f, listSpan, &listEntered ); // 거꾸로 들어왔다
    SequenceTimelineUtil::applySpans( &manager, asset, view, 2, 30.0f, -1.0f, listSpan, &listEntered ); // 머문다
    SW_EXPECT_EQUAL( size_t( 1 ), listEntered.size() );
    SequenceTimelineUtil::applySpans( &manager, asset, view, 0, 30.0f, -1.0f, listSpan, &listEntered );
    SW_REQUIRE_EQUAL( size_t( 1 ), listSpan.size() );
    SW_EXPECT_TRUE( listSpan[0]._bInside == SW_FALSE );
}

/** @brief [SequencePlaybackTest] 컴포넌트의 끝으로 건너뛰기: 끝 상태를 적용하고(KeepState) 정상 끝 알림 · 기록이 한 번이다. 멈춤(`stop`)은 끝 알림이 아니다 */
SW_TEST_CASE( SequencePlaybackTest, ComponentSkipToEndFinishesOnce )
{
    using Util = SequencePlaybackTestUtil;
    GameObjectManager        manager;
    GameObject*              pHero   = nullptr;
    SequencePlayerComponent* pPlayer = Util::makeScene( manager, pHero );
    SW_REQUIRE_NOT_NULL( pPlayer );
    pPlayer->setSequence( Util::makeMoveSequence( SequenceFinishPolicy::KeepState ) );
    pPlayer->setBinding( 1, pHero->getHandle() );
    uint32             finishedCount = 0;
    const Subscription finished      = pPlayer->registerFinished( OnSequenceFinishedDelegate::create( [&finishedCount]()
    { ++finishedCount; } ) );

    SW_EXPECT_FALSE( pPlayer->skipToEnd() ); // 재생 중이 아니다
    pPlayer->play();
    Util::update( manager, *pPlayer, 0.05f );
    SW_REQUIRE_TRUE( pPlayer->skipToEnd() );
    SW_EXPECT_FALSE( pPlayer->isPlaying() );
    SW_EXPECT_EQUAL( 1u, finishedCount );
    SW_EXPECT_EQUAL( 1u, pPlayer->getHistory().countKind( SequenceHistoryKind::Finished ) );
    SW_EXPECT_NEAR( 5.0f, Util::readX( *pHero ), 1e-4f );
    pPlayer->play();
    pPlayer->stop();
    SW_EXPECT_EQUAL( 1u, finishedCount );
}

/** @brief [SequencePlaybackTest] 구간 섞임 가중치: 머리 4 프레임 동안 오르고 꼬리 2 프레임 동안 내린다(곡선은 블렌드 표), 구간 밖은 0 */
SW_TEST_CASE( SequencePlaybackTest, SectionEaseWeightRisesAndFalls )
{
    SequenceTrackItem item{};
    item._kind          = SequenceItemKind::Animation;
    item._start         = 0;
    item._end           = 10;
    item._easeInFrames  = 4;
    item._easeOutFrames = 2;
    item._easeInCurve   = BlendCurve::Linear;
    item._easeOutCurve  = BlendCurve::Linear;
    SW_EXPECT_NEAR( 0.0f, item.computeEaseWeight( 0.0f ), 1e-6f );
    SW_EXPECT_NEAR( 0.5f, item.computeEaseWeight( 2.0f ), 1e-6f );
    SW_EXPECT_NEAR( 1.0f, item.computeEaseWeight( 5.0f ), 1e-6f );
    SW_EXPECT_NEAR( 0.5f, item.computeEaseWeight( 9.0f ), 1e-6f );
    SW_EXPECT_NEAR( 0.0f, item.computeEaseWeight( 10.0f ), 1e-6f );
    SW_EXPECT_NEAR( 0.0f, item.computeEaseWeight( 11.0f ), 1e-6f );
    item._easeInCurve = BlendCurve::SmoothStep;
    SW_EXPECT_NEAR( 0.5f, item.computeEaseWeight( 2.0f ), 1e-6f );
    SW_EXPECT_NEAR( 0.15625f, item.computeEaseWeight( 1.0f ), 1e-6f );
}

/** @brief [SequencePlaybackTest] 이징 키의 값은 프레임의 값이다 — 정방향으로 닿은 4 프레임과 역방향으로 닿은 4 프레임의 위치가 같고, 그 값이 스무스스텝 표본이다 */
SW_TEST_CASE( SequencePlaybackTest, EasedKeysGiveTheSameValueInReverse )
{
    using Util = SequencePlaybackTestUtil;
    GameObjectManager        manager;
    GameObject*              pHero   = nullptr;
    SequencePlayerComponent* pPlayer = Util::makeScene( manager, pHero );
    SW_REQUIRE_NOT_NULL( pPlayer );
    SequenceAsset asset;
    asset._frameMin         = 0;
    asset._frameMax         = 10;
    asset._whenFinished     = SequenceFinishPolicy::KeepState;
    const uint32     heroID = asset.addBinding( "Hero" );
    SequenceKeyTrack track{};
    track._kind    = SequenceTrackKind::Transform;
    track._binding = heroID;
    track.fitChannelsToKind();
    (void)track.setChannelKey( 0, 0.0f, 0.0f );
    (void)track.setChannelKey( 0, 10.0f, 10.0f );
    track._listChannel[0]._listKey[0]._interpolation = CurveInterpolation::Eased;
    track._listChannel[0]._listKey[0]._easing        = BlendCurve::SmoothStep;
    asset._listTrack.push_back( track );
    pPlayer->setSequence( asset );
    pPlayer->setBinding( heroID, pHero->getHandle() );

    pPlayer->play();
    while ( pPlayer->isPlaying() && pPlayer->getCurrentFrame() < 4 )
        Util::update( manager, *pPlayer, 1.0f / 30.0f );
    SW_REQUIRE_EQUAL( 4, pPlayer->getCurrentFrame() );
    const float32 forwardX = Util::readX( *pHero );
    SW_EXPECT_NEAR( 3.52f, forwardX, 1e-4f ); // 10 × (3 · 0.4² - 2 · 0.4³)

    pPlayer->playReverse();
    while ( pPlayer->isPlaying() && pPlayer->getCurrentFrame() > 4 )
        Util::update( manager, *pPlayer, 1.0f / 30.0f );
    SW_REQUIRE_EQUAL( 4, pPlayer->getCurrentFrame() );
    SW_EXPECT_NEAR( forwardX, Util::readX( *pHero ), 1e-6f );
}

/** @brief [SequencePlaybackTest] 범위 제한 컷씬: 대상이 중심 둘레 반지름 밖으로 나가면 틱 뒤에 경계로 되돌린다. 칸이 비었으면 오류를 남기고 틀지 않는다 */
SW_TEST_CASE( SequencePlaybackTest, ConfinedCutsceneKeepsTheTargetInsideTheRadius )
{
    SW_EXPECT_LOG( "SequencePlayer.ConfineIncomplete" );
    using Util = SequencePlaybackTestUtil;
    GameObjectManager        manager;
    GameObject*              pHero   = nullptr;
    SequencePlayerComponent* pPlayer = Util::makeScene( manager, pHero );
    SW_REQUIRE_NOT_NULL( pPlayer );
    GameObject* pCenter = manager.createGameObject( hashed_string( "Center" ) );
    (void)pCenter->addComponent<SceneComponent>();
    SequenceAsset asset;
    asset._frameMax     = 300;
    asset._whenFinished = SequenceFinishPolicy::KeepState;
    pPlayer->setSequence( asset );
    pPlayer->setPlayerMovement( SequencePlayerMovement::Confined );

    pPlayer->play(); // 대상 · 중심 · 반지름이 없다
    SW_EXPECT_FALSE( pPlayer->isPlaying() );

    pPlayer->setConfine( pHero->getHandle(), pCenter->getHandle(), 2.0f );
    pPlayer->play();
    SW_REQUIRE_TRUE( pPlayer->isPlaying() );
    pHero->getPrimarySceneComponent()->setLocalPosition( float3{ 5.0f, 0.0f, 0.0f } );
    Util::update( manager, *pPlayer, 1.0f / 30.0f );
    SW_EXPECT_NEAR( 2.0f, Util::readX( *pHero ), 1e-4f );
    pHero->getPrimarySceneComponent()->setLocalPosition( float3{ 1.0f, 0.0f, 0.0f } );
    Util::update( manager, *pPlayer, 1.0f / 30.0f );
    SW_EXPECT_NEAR( 1.0f, Util::readX( *pHero ), 1e-4f ); // 안이면 그대로
}
