// 시퀀스 재생(오브젝트 있음, nogpu) — 바인딩으로 대상 찾기, 묶이지 않은 바인딩이면 틀지 않음, 끝내기 계약(되돌림 · 유지), 기록, 구간 처리기.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
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
    {
        TickContext context{ *pPlayer->getOwner(), 0.1f };
        pPlayer->onTick( context );
    }
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
        {
            TickContext context{ *pPlayer->getOwner(), 0.05f };
            pPlayer->onTick( context );
        }
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
    {
        TickContext context{ *pPlayer->getOwner(), 0.1f };
        pPlayer->onTick( context );
    }
    pPlayer->play(); // 다시 틀기 — 앞 재생을 끝내고(되돌림) 다시 시작한다
    SW_EXPECT_EQUAL( 1u, pPlayer->getHistory().countKind( SequenceHistoryKind::Stopped ) );
    for ( int32 tickIndex = 0; tickIndex < 20 && pPlayer->isPlaying(); ++tickIndex )
    {
        {
            TickContext context{ *pPlayer->getOwner(), 0.05f };
            pPlayer->onTick( context );
        }
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
    {
        TickContext context{ *pOther->getOwner(), 0.1f };
        pOther->onTick( context );
    }
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
    SequenceTimelineUtil::applySpans( &manager, asset, view, 1, 30.0f, listSpan );
    SW_EXPECT_TRUE( listSpan.empty() ); // 아직 들어가지 않았다
    SequenceTimelineUtil::applySpans( &manager, asset, view, 3, 30.0f, listSpan );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listSpan.size() );
    SW_EXPECT_TRUE( listSpan[0]._bInside == SW_TRUE );
    SequenceTimelineUtil::applySpans( &manager, asset, view, 8, 30.0f, listSpan );
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
