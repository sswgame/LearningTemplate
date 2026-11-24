#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/IntentPlayback.h"
#include "GameFramework/Runtime/Character/Control/Controller/IntentTrackControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/RemoteControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntentHistory.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/PrefabPool.h"
#include "GameFramework/Runtime/Foundation/Framework/Tests/PrefabPoolComponentSpawner.h"

using namespace sw;

namespace
{
    /** @brief 폰 하나와 기록 조종자 둘을 둔 씬입니다. 입력이 없어도 도는 조종자라 헤드리스로 돕니다. */
    struct ControlRig
    {
        GameObjectManager               _manager;
        GameObject*                     _pPawnObject{ nullptr };
        PawnComponent*                  _pPawn{ nullptr };
        IntentTrackControllerComponent* _pFirst{ nullptr };
        IntentTrackControllerComponent* _pSecond{ nullptr };

        ControlRig()
        {
            _pPawnObject = _manager.createGameObject( hashed_string( "Hero" ) );
            _pPawn       = _pPawnObject->addComponent<PawnComponent>();
            _pFirst      = _manager.createGameObject( hashed_string( "First" ) )->addComponent<IntentTrackControllerComponent>();
            _pSecond     = _manager.createGameObject( hashed_string( "Second" ) )->addComponent<IntentTrackControllerComponent>();
            _manager.beginPlay();
        }

        void update() { _manager.update( 1.0f / 60.0f ); }

        static ControlIntent makeMove( float32 forward )
        {
            ControlIntent intent;
            intent._move = float2{ 0.0f, forward };
            return intent;
        }
    };

    struct ControlTestUtil
    {
        static ControlIntent makeFullIntent()
        {
            ControlIntent intent;
            intent._move         = float2{ 0.5f, -0.25f };
            intent._moveUp       = 1.0f;
            intent._controlYaw   = 1.25f;
            intent._controlPitch = -0.5f;
            intent._arrAnalog[0] = 1.0f;
            intent._arrAnalog[3] = -1.0f;
            intent.setButton( 0, true );
            intent.setButton( 31, true );
            intent.addTrigger( 0, 0.5f, 0.25f, ControlViewTick::makeSeen( 12.5 ) );
            return intent;
        }
    };
} // namespace

// ---------------------------------------------------------------------- 의도

SW_TEST_CASE( ControlIntentTest, ButtonsAreBitsAndOutOfRangeIsIgnored )
{
    ControlIntent intent;
    intent.setButton( 3, true );
    intent.addTrigger( 4, 0.0f, 0.0f, ControlViewTick::makePresent() );
    SW_EXPECT_TRUE( intent.isDown( 3 ) );
    SW_EXPECT_FALSE( intent.wasTriggered( 3 ) );
    SW_EXPECT_TRUE( intent.wasTriggered( 4 ) );
    SW_EXPECT_FALSE( intent.isDown( 4 ) );
    intent.setButton( 3, false );
    SW_EXPECT_FALSE( intent.isDown( 3 ) );

    // 폰에 없는 버튼(-1)은 늘 꺼져 있고 쓰기도 할 일이 없다.
    const ControlIntent before = intent;
    intent.setButton( -1, true );
    intent.setButton( ControlIntent::kButtonCount, true );
    intent.addTrigger( -1, 0.0f, 0.0f, ControlViewTick::makePresent() );
    intent.addTrigger( ControlIntent::kButtonCount, 0.0f, 0.0f, ControlViewTick::makePresent() );
    SW_EXPECT_TRUE( intent == before );
    SW_EXPECT_FALSE( intent.isDown( -1 ) );
    SW_EXPECT_FALSE( intent.wasTriggered( 32 ) );
}

// 요 0 이면 앞이 +Z, 오른쪽이 +X. 요 90° 면 앞이 +X.
SW_TEST_CASE( ControlIntentTest, MoveTurnsWithTheControlYaw )
{
    ControlIntent intent;
    intent._move       = float2{ 0.0f, 1.0f };
    const float3 ahead = intent.computeWorldMove();
    SW_EXPECT_NEAR( 1.0f, ahead._z, 1e-5f );
    SW_EXPECT_NEAR( 0.0f, ahead._x, 1e-5f );

    intent._controlYaw  = MathUtil::kHalfPi;
    const float3 turned = intent.computeWorldMove();
    SW_EXPECT_NEAR( 1.0f, turned._x, 1e-5f );
    SW_EXPECT_NEAR( 0.0f, turned._z, 1e-5f );

    intent._moveUp = -0.5f;
    SW_EXPECT_NEAR( -0.5f, intent.computeWorldMove()._y, 1e-5f );
}

SW_TEST_CASE( ControlIntentTest, WorldMoveRoundTripsAndIsLimitedToOne )
{
    ControlIntent intent;
    intent._controlYaw = 0.7f;
    intent.setWorldMove( float3{ 0.3f, 0.0f, -0.4f } );
    const float3 world = intent.computeWorldMove();
    SW_EXPECT_NEAR( 0.3f, world._x, 1e-5f );
    SW_EXPECT_NEAR( -0.4f, world._z, 1e-5f );

    intent.setWorldMove( float3{ 30.0f, 0.0f, 40.0f } );
    const float32 length = MathUtil::sqrt( intent._move._x * intent._move._x + intent._move._y * intent._move._y );
    SW_EXPECT_NEAR( 1.0f, length, 1e-5f );
}

// 로컬 · 원격 · 재생이 같은 값으로 움직이는 근거다: 선을 거친 값은 quantize 한 값과 같다.
SW_TEST_CASE( ControlIntentTest, WireFormatMatchesQuantize )
{
    ControlIntent intent = ControlTestUtil::makeFullIntent();
    BitWriter     writer;
    SW_REQUIRE_TRUE( intent.write( writer ) );
    SW_EXPECT_TRUE( writer.getByteCount() <= ControlIntent::kMaxSerializedBytes );

    ControlIntent read;
    BitReader     reader( writer.getBytes().data(), static_cast<int32>( writer.getByteCount() ) );
    SW_REQUIRE_TRUE( read.read( reader ) );
    intent.quantize();
    SW_EXPECT_TRUE( read == intent );
    SW_EXPECT_TRUE( read.isDown( 31 ) );
    SW_EXPECT_TRUE( read.wasTriggered( 0 ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, read._listTrigger.size() );
    SW_EXPECT_NEAR( 12.5, read._listTrigger[0].getViewTick().getServerTick( 0.0 ), 1e-6 );

    // 상한을 넘는 발동은 쓰지 않는다 — 받는 쪽도 깨진 것으로 본다.
    for ( int32 index = 0; index < ControlIntent::kMaxTriggers; ++index )
    {
        intent.addTrigger( 1, 0.0f, 0.0f, ControlViewTick::makePresent() );
    }
    BitWriter crowded;
    SW_EXPECT_FALSE( intent.write( crowded ) );
}

// 0 과 ±1 은 그대로 남는다 — 0 이 흐르면 서 있는 폰이 조금씩 움직인다.
SW_TEST_CASE( ControlIntentTest, QuantizeKeepsZeroAndUnitExact )
{
    ControlIntent intent;
    intent._move   = float2{ 1.0f, 0.0f };
    intent._moveUp = -1.0f;
    intent.quantize();
    SW_EXPECT_EQUAL( 1.0f, intent._move._x );
    SW_EXPECT_EQUAL( 0.0f, intent._move._y );
    SW_EXPECT_EQUAL( -1.0f, intent._moveUp );
    SW_EXPECT_EQUAL( 0.0f, intent._controlYaw );
}

// 대각선 최대 입력이 반올림으로 길이 1 을 넘으면 받는 쪽이 거절한다. 어떤 방향이든 선을 지나야 한다.
SW_TEST_CASE( ControlIntentTest, EveryMoveDirectionSurvivesTheWire )
{
    for ( int32 degree = 0; degree < 360; ++degree )
    {
        const float32 angle = static_cast<float32>( degree ) * MathUtil::kPi / 180.0f;
        ControlIntent intent;
        intent._move = float2{ MathUtil::sin( angle ), MathUtil::cos( angle ) };
        BitWriter writer;
        SW_REQUIRE_TRUE( intent.write( writer ) );
        ControlIntent read;
        BitReader     reader( writer.getBytes().data(), static_cast<int32>( writer.getByteCount() ) );
        SW_EXPECT_TRUE( read.read( reader ) );
    }
    ControlIntent tooLong;
    tooLong._move = float2{ 3.0f, 4.0f }; // 길면 길이 1 로 줄인다
    tooLong.quantize();
    const float32 length = MathUtil::sqrt( tooLong._move._x * tooLong._move._x + tooLong._move._y * tooLong._move._y );
    SW_EXPECT_TRUE( length <= 1.0f );
    SW_EXPECT_NEAR( 1.0f, length, 0.02f );
}

SW_TEST_CASE( ControlIntentTest, TruncatedBytesAreRefusedAndClearTheIntent )
{
    const ControlIntent intent = ControlTestUtil::makeFullIntent();
    BitWriter           writer;
    SW_REQUIRE_TRUE( intent.write( writer ) );
    ControlIntent read = intent;
    BitReader     reader( writer.getBytes().data(), 2 );
    SW_EXPECT_FALSE( read.read( reader ) );
    SW_EXPECT_TRUE( read == ControlIntent{} );
}

// ---------------------------------------------------------------------- 기록

SW_TEST_CASE( ControlIntentHistoryTest, TicksAccumulatePerPawn )
{
    ControlIntentHistory                  history;
    const TComponentHandle<PawnComponent> pawn{};
    history.initialize( 8 );
    history.record( 10, pawn, hashed_string( "Hero" ), ControlRig::makeMove( 0.5f ) );
    history.record( 11, pawn, hashed_string( "Hero" ), ControlRig::makeMove( 1.0f ) );
    SW_REQUIRE_EQUAL( 1, history.getTrackCount() );
    SW_EXPECT_TRUE( history.findTrack( hashed_string( "Hero" ) ) == 0 );
    SW_EXPECT_FALSE( history.findTrack( hashed_string( "Other" ) ).has_value() );

    vector<ControlIntent> listIntent;
    SW_REQUIRE_TRUE( history.copyTrack( 0, listIntent ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listIntent.size() );
    SW_EXPECT_EQUAL( 1.0f, listIntent[1]._move._y );
    SW_EXPECT_TRUE( history.findIntent( 0, 10 ) != nullptr );
    SW_EXPECT_TRUE( history.findIntent( 0, 12 ) == nullptr );
    SW_EXPECT_TRUE( history.findIntent( 5, 10 ) == nullptr );
    SW_EXPECT_FALSE( history.copyTrack( 5, listIntent ) );
}

// 건너뛴 틱은 이어 붙일 수 없다 — 그 틱부터 다시 쌓는다.
SW_TEST_CASE( ControlIntentHistoryTest, GapRestartsTheTrack )
{
    ControlIntentHistory                  history;
    const TComponentHandle<PawnComponent> pawn{};
    history.record( 1, pawn, hashed_string( "Hero" ), ControlRig::makeMove( 0.5f ) );
    history.record( 5, pawn, hashed_string( "Hero" ), ControlRig::makeMove( 1.0f ) );
    SW_EXPECT_EQUAL( uint32{ 5 }, history.getTrack( 0 )._firstTick );
    vector<ControlIntent> listIntent;
    SW_REQUIRE_TRUE( history.copyTrack( 0, listIntent ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, listIntent.size() );
}

SW_TEST_CASE( ControlIntentHistoryTest, TicksRoundTripThroughBytes )
{
    ControlIntentHistory                  history;
    const TComponentHandle<PawnComponent> pawn{};
    history.record( 7, pawn, hashed_string( "Hero" ), ControlRig::makeMove( 0.5f ) );
    history.record( 8, pawn, hashed_string( "Hero" ), ControlTestUtil::makeFullIntent() );
    BitWriter writer;
    SW_REQUIRE_TRUE( history.write( writer ) );

    ControlIntentHistory loaded;
    BitReader            reader( writer.getBytes().data(), static_cast<int32>( writer.getByteCount() ) );
    SW_REQUIRE_TRUE( loaded.read( reader ) );
    SW_REQUIRE_EQUAL( 1, loaded.getTrackCount() );
    SW_EXPECT_TRUE( loaded.getTrack( 0 )._pawnName == hashed_string( "Hero" ) );
    vector<ControlIntent> listIntent;
    SW_REQUIRE_TRUE( loaded.copyTrack( 0, listIntent ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listIntent.size() );
    ControlIntent expected = ControlTestUtil::makeFullIntent();
    expected.quantize();
    SW_EXPECT_TRUE( listIntent[1] == expected );
}

SW_TEST_CASE( ControlIntentHistoryTest, BrokenBytesAreRefusedWithAReason )
{
    ControlIntentHistory history;
    const uint8          arrGarbage[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    BitReader            garbage( arrGarbage, 8 );
    const expected<void> garbageRead = history.read( garbage );
    SW_REQUIRE_FALSE( garbageRead.has_value() );
    SW_EXPECT_TRUE( garbageRead.error().message().find( "magic" ) != string_view::npos );

    BitWriter writer;
    writer.writeUint32( ControlIntentHistory::kMagic );
    writer.writeVarUint( ControlIntentHistory::kVersion + 1 );
    BitReader            badVersion( writer.getBytes().data(), static_cast<int32>( writer.getByteCount() ) );
    const expected<void> badVersionRead = history.read( badVersion );
    SW_REQUIRE_FALSE( badVersionRead.has_value() );
    SW_EXPECT_TRUE( badVersionRead.error().message().find( "version" ) != string_view::npos );

    // 틱 수가 남은 바이트보다 크면 큰 고리를 잡기 전에 거절한다.
    BitWriter hugeWriter;
    hugeWriter.writeUint32( ControlIntentHistory::kMagic );
    hugeWriter.writeVarUint( ControlIntentHistory::kVersion );
    hugeWriter.writeVarUint( 1 );
    const uint8 arrName[] = { 'H' };
    hugeWriter.writeBlob( arrName, 1 );
    hugeWriter.writeVarUint( 0 );
    hugeWriter.writeVarUint( uint64{ 1 } << 40 );
    BitReader            huge( hugeWriter.getBytes().data(), static_cast<int32>( hugeWriter.getByteCount() ) );
    const expected<void> hugeRead = history.read( huge );
    SW_REQUIRE_FALSE( hugeRead.has_value() );
    SW_EXPECT_TRUE( hugeRead.error().message().find( "tick count" ) != string_view::npos );
    SW_EXPECT_EQUAL( 0, history.getTrackCount() );
}

// ---------------------------------------------------------------------- 빙의

SW_TEST_CASE( ControlSystemTest, PossessLinksBothSidesAndAPawnHasOneController )
{
    ControlRig rig;
    SW_EXPECT_FALSE( rig._pPawn->isPossessed() );
    rig._pFirst->possess( *rig._pPawn );
    SW_EXPECT_TRUE( rig._pPawn->getController() == rig._pFirst->getHandle() );
    SW_EXPECT_TRUE( rig._pFirst->findPawn() == rig._pPawn );

    rig._pSecond->possess( *rig._pPawn );
    SW_EXPECT_TRUE( rig._pPawn->getController() == rig._pSecond->getHandle() );
    SW_EXPECT_TRUE( rig._pFirst->findPawn() == nullptr );

    rig._pSecond->unpossess();
    SW_EXPECT_FALSE( rig._pPawn->isPossessed() );
    rig._pSecond->unpossess(); // 두 번 놓아도 괜찮다
}

SW_TEST_CASE( ControlSystemTest, MovingToAnotherPawnReleasesTheFirst )
{
    ControlRig     rig;
    PawnComponent* pOther = rig._manager.createGameObject( hashed_string( "Other" ) )->addComponent<PawnComponent>();
    rig._pFirst->possess( *rig._pPawn );
    rig._pFirst->possess( *pOther );
    SW_EXPECT_FALSE( rig._pPawn->isPossessed() );
    SW_EXPECT_TRUE( pOther->getController() == rig._pFirst->getHandle() );
}

// 폰의 연결은 빙의를 따라간다 — 탈것은 운전석 조종자의 연결이다.
SW_TEST_CASE( ControlSystemTest, InputPeerFollowsThePossession )
{
    ControlRig                 rig;
    RemoteControllerComponent* pRemote = RemoteControllerComponent::spawnForConnection( rig._manager, hashed_string( "Remote" ), 6, *rig._pPawn ); // 입력 피어 = 연결 id + 1
    SW_REQUIRE_NOT_NULL( pRemote );
    SW_EXPECT_EQUAL( 7u, rig._pPawn->getInputPeer() );
    pRemote->transferToConnection( 8 );
    SW_EXPECT_EQUAL( 9u, rig._pPawn->getInputPeer() );
    pRemote->unpossess();
    SW_EXPECT_EQUAL( 0u, rig._pPawn->getInputPeer() );
}

SW_TEST_CASE( ControlSystemTest, ControllerProducesAndReleaseStopsTheMotion )
{
    ControlRig rig;
    rig._pFirst->play( *rig._pPawn, { ControlRig::makeMove( 1.0f ), ControlRig::makeMove( 0.5f ) }, false );
    rig.update();
    SW_EXPECT_EQUAL( 1.0f, rig._pPawn->getIntent()._move._y );
    rig.update();
    SW_EXPECT_NEAR( 0.5f, rig._pPawn->getIntent()._move._y, 0.01f );
    rig.update();
    SW_EXPECT_EQUAL( 0.0f, rig._pPawn->getIntent()._move._y ); // 트랙이 끝나면 의도는 0 이다
    SW_EXPECT_FALSE( rig._pFirst->isPlaying() );
    SW_EXPECT_TRUE( rig._pPawn->isPossessed() ); // 돌려주지 않으면 그대로 쥔다
}

SW_TEST_CASE( ControlSystemTest, UnpossessedPawnStopsButKeepsItsLook )
{
    ControlRig    rig;
    ControlIntent intent = ControlRig::makeMove( 1.0f );
    intent._controlYaw   = 0.5f;
    rig._pFirst->play( *rig._pPawn, { intent, intent }, false );
    rig.update();
    rig._pFirst->unpossess();
    SW_EXPECT_EQUAL( 0.0f, rig._pPawn->getIntent()._move._y );
    SW_EXPECT_NEAR( 0.5f, rig._pPawn->getIntent()._controlYaw, 1e-3f );
}

// 트랙이 끝나면 다음 틱에 빙의를 돌려준다. 같은 틱에 옮기면 두 조종자가 폰을 몬다.
SW_TEST_CASE( ControlSystemTest, PlaybackReturnsThePawnOnTheNextTick )
{
    ControlRig rig;
    rig._pFirst->possess( *rig._pPawn );
    rig._pSecond->play( *rig._pPawn, { ControlRig::makeMove( 1.0f ) }, true );
    SW_EXPECT_TRUE( rig._pPawn->getController() == rig._pSecond->getHandle() );
    rig.update();
    SW_EXPECT_TRUE( rig._pPawn->getController() == rig._pSecond->getHandle() );
    rig.update();
    SW_EXPECT_TRUE( rig._pPawn->getController() == rig._pFirst->getHandle() );
}

SW_TEST_CASE( ControlSystemTest, PlaybackWithNoPreviousControllerReleases )
{
    ControlRig rig;
    rig._pSecond->play( *rig._pPawn, { ControlRig::makeMove( 1.0f ) }, true );
    rig.update();
    rig.update();
    SW_EXPECT_FALSE( rig._pPawn->isPossessed() );
}

SW_TEST_CASE( ControlSystemTest, QueuedPossessIgnoresDeadControllers )
{
    ControlRig     rig;
    ControlSystem& system = ControlSystem::getOrCreate( rig._manager );
    system.queuePossess( TComponentHandle<ControllerComponent>{ rig._pFirst }, TComponentHandle<PawnComponent>{ rig._pPawn } );
    SW_EXPECT_FALSE( rig._pPawn->isPossessed() ); // 다음 틱 첫머리에 적용한다
    rig.update();
    SW_EXPECT_TRUE( rig._pPawn->getController() == rig._pFirst->getHandle() );

    system.queuePossess( TComponentHandle<ControllerComponent>{}, TComponentHandle<PawnComponent>{ rig._pPawn } );
    rig.update();
    SW_EXPECT_TRUE( rig._pPawn->getController() == rig._pFirst->getHandle() );

    system.queuePossess( TComponentHandle<ControllerComponent>{ rig._pFirst }, TComponentHandle<PawnComponent>{} ); // 무효 폰은 놓기다
    rig.update();
    SW_EXPECT_FALSE( rig._pPawn->isPossessed() );
}

SW_TEST_CASE( ControlSystemTest, ControlRotationRequestsAndOffsetsApply )
{
    ControlRig rig;
    rig._pFirst->possess( *rig._pPawn );
    rig._pPawn->requestControlRotation( 1.0f, 0.25f );
    rig.update();
    SW_EXPECT_NEAR( 1.0f, rig._pPawn->getIntent()._controlYaw, 1e-3f );
    SW_EXPECT_NEAR( 0.25f, rig._pPawn->getIntent()._controlPitch, 1e-3f );

    rig._pPawn->addControlRotationOffset( 0.5f, 0.0f );
    rig.update();
    SW_EXPECT_NEAR( 1.5f, rig._pPawn->getIntent()._controlYaw, 1e-3f );

    // 요청은 그 앞의 오프셋을 버린다.
    rig._pPawn->addControlRotationOffset( 2.0f, 0.0f );
    rig._pPawn->requestControlRotation( 0.1f, 0.0f );
    rig.update();
    SW_EXPECT_NEAR( 0.1f, rig._pPawn->getIntent()._controlYaw, 1e-3f );

    // 피치는 폰의 한도 안이다.
    rig._pPawn->addControlRotationOffset( 0.0f, 50.0f );
    rig.update();
    SW_EXPECT_NEAR( rig._pPawn->getMaxPitch(), rig._pPawn->getIntent()._controlPitch, 1e-3f );
}

SW_TEST_CASE( ControlSystemTest, UnpossessedPawnStillAppliesRotationRequests )
{
    ControlRig rig;
    rig._pPawn->requestControlRotation( 0.5f, 0.0f );
    rig.update();
    SW_EXPECT_NEAR( 0.5f, rig._pPawn->getIntent()._controlYaw, 1e-3f );
}

SW_TEST_CASE( ControlSystemTest, ButtonSchemaNamesFindTheirIndex )
{
    ControlRig rig;
    rig._pPawn->setButtonNames( { hashed_string( "Jump" ), hashed_string( "Fire" ) } );
    rig._pPawn->setAnalogNames( { hashed_string( "Throttle" ) } );
    SW_EXPECT_TRUE( rig._pPawn->findButton( hashed_string( "Fire" ) ) == 1 );
    SW_EXPECT_FALSE( rig._pPawn->findButton( hashed_string( "Nope" ) ).has_value() );
    SW_EXPECT_TRUE( rig._pPawn->findAnalog( hashed_string( "Throttle" ) ) == 0 );
    SW_EXPECT_FALSE( rig._pPawn->isButtonDown( nullopt ) );
    SW_EXPECT_FALSE( rig._pPawn->isButtonDown( -1 ) );
}

SW_TEST_CASE( ControlSystemTest, AutoPossessSpawnsAnAIThatDiesWithThePawn )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Guard" ) );
    PawnComponent*    pPawn   = pObject->addComponent<PawnComponent>();
    pPawn->setAutoPossess( PawnAutoPossess::AI );
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_FALSE( pPawn->isPossessed() ); // 플레이 전에는 쥐지 않는다

    manager.beginPlay();
    manager.update( 1.0f / 60.0f );
    SW_REQUIRE_TRUE( pPawn->isPossessed() );
    const ControllerComponent* pController = manager.resolveComponent( pPawn->getController() );
    SW_REQUIRE_NOT_NULL( pController );
    SW_EXPECT_TRUE( pController->isSpawnedForPawn() );
    SW_EXPECT_TRUE( pController->getOwner()->isTransient() ); // 세이브 · 핫 리로드가 싣지 않는다 — 복원한 폰이 다시 세운다
    const GameObjectHandle controllerObject = pController->getOwner()->getHandle();

    manager.destroyGameObject( pObject );
    manager.update( 1.0f / 60.0f );
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_TRUE( manager.resolveGameObject( controllerObject ) == nullptr );
}

/** @brief [ControlSystemTest] 월드 원점이 옮겨지면 AI 조종자의 목적지와 블랙보드의 위치도 같은 만큼 옮겨진다 — 옛 원점 기준의 자리로 가지 않는다. 위치가 아닌 값은 그대로다 */
SW_TEST_CASE( ControlSystemTest, AIPositionsFollowTheWorldOrigin )
{
    GameObjectManager      manager;
    AIControllerComponent* pAI = manager.createGameObject( hashed_string( "AI" ) )->addComponent<AIControllerComponent>();
    manager.update( 1.0f / 60.0f );
    pAI->moveTo( float3{ 1010.0f, 2.0f, 5.0f } );
    Blackboard& blackboard = pAI->getBlackboard();
    blackboard.setVector( "Goal", float3{ 1010.0f, 2.0f, 5.0f } );
    blackboard.setFloat( "Distance", 7.0f );
    const uint32 revision = blackboard.getRevision();

    manager.notifyWorldOriginShifted( float3{ -1000.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( ( pAI->getDestination() == float3{ 10.0f, 2.0f, 5.0f } ) );
    SW_EXPECT_TRUE( ( blackboard.getVector( "Goal" ) == sw::optional<float3>( float3{ 10.0f, 2.0f, 5.0f } ) ) );
    SW_EXPECT_TRUE( blackboard.getFloat( "Distance" ) == sw::optional<float32>( 7.0f ) );
    SW_EXPECT_EQUAL( revision, blackboard.getRevision() );
}

/** @brief [ControlSystemTest] 풀(`PrefabPool`)에 돌려준 폰은 자동 빙의가 세운 AI 조종자를 놓고(조종자는 지워진다) 쉬는 동안 쥐이지 않다가, 다시 꺼내면 갓 스폰한 폰처럼 다시 빙의된다 */
SW_TEST_CASE( ControlSystemTest, PooledPawnDropsItsAIAndIsPossessedAgainWhenReused )
{
    GameObjectManager manager;
    PrefabPool        pool = PrefabPoolComponentSpawner<PawnComponent>::makePool( 2 );
    manager.beginPlay();
    GameObject* pObject = pool.acquire( manager, "Guard" );
    SW_REQUIRE_NOT_NULL( pObject );
    PawnComponent* pPawn = pObject->getComponent<PawnComponent>();
    SW_REQUIRE_NOT_NULL( pPawn );
    pPawn->setAutoPossess( PawnAutoPossess::AI );
    manager.update( 1.0f / 60.0f );
    SW_REQUIRE_TRUE( pPawn->isPossessed() );
    const ControllerComponent* pFirstController = manager.resolveComponent( pPawn->getController() );
    SW_REQUIRE_NOT_NULL( pFirstController );
    const GameObjectHandle firstControllerObject = pFirstController->getOwner()->getHandle();

    pool.release( manager, pObject->getHandle() );
    SW_EXPECT_FALSE( pPawn->isPossessed() );
    manager.update( 1.0f / 60.0f );
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_TRUE( manager.resolveGameObject( firstControllerObject ) == nullptr );
    SW_EXPECT_FALSE( pPawn->isPossessed() ); // 꺼져 쉬는 폰은 쥐지 않는다

    SW_REQUIRE_TRUE( pool.acquire( manager, "Guard" ) == pObject );
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_TRUE( pPawn->isPossessed() );
    SW_EXPECT_EQUAL( uint64{ 1 }, pool.getCreatedCount().get() );
}

// 폰도 조종자도 없으면 시스템이 프레임에서 빠진다.
SW_TEST_CASE( ControlSystemTest, SystemLeavesWhenNothingUsesIt )
{
    GameObjectManager manager;
    SW_EXPECT_TRUE( ControlSystem::find( manager ) == nullptr );
    GameObject* pObject = manager.createGameObject( hashed_string( "Hero" ) );
    (void)pObject->addComponent<PawnComponent>();
    SW_EXPECT_TRUE( ControlSystem::find( manager ) != nullptr );
    manager.destroyGameObject( pObject );
    manager.update( 1.0f / 60.0f );
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_TRUE( ControlSystem::find( manager ) == nullptr );
}

SW_TEST_CASE( ControlSystemTest, RecordingCapturesEachTick )
{
    ControlRig     rig;
    ControlSystem& system = ControlSystem::getOrCreate( rig._manager );
    rig._pFirst->play( *rig._pPawn, { ControlRig::makeMove( 1.0f ), ControlRig::makeMove( 0.5f ) }, false );
    system.setRecording( true, 16 );
    SW_EXPECT_TRUE( system.isRecording() );
    rig.update();
    rig.update();
    system.setRecording( false );
    rig.update();
    const optional<int32> track = system.getHistory().findTrack( hashed_string( "Hero" ) );
    SW_REQUIRE_TRUE( track.has_value() );
    vector<ControlIntent> listIntent;
    SW_REQUIRE_TRUE( system.getHistory().copyTrack( *track, listIntent ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listIntent.size() );
    SW_EXPECT_EQUAL( 1.0f, listIntent[0]._move._y );
    SW_EXPECT_EQUAL( 16, system.getHistory().getCapacityTicks() );
}

// ---------------------------------------------------------------------- 재생 진입점

SW_TEST_CASE( IntentPlaybackTest, TracksFindTheirPawnsByName )
{
    SW_EXPECT_LOG( "IntentPlayback.IntentPlaybackSkippedTrack" );
    ControlRig                            rig;
    ControlIntentHistory                  history;
    const TComponentHandle<PawnComponent> pawn{};
    history.record( 0, pawn, hashed_string( "Hero" ), ControlRig::makeMove( 1.0f ) );
    const TComponentHandle<PawnComponent> ghost{};
    ControlIntentHistory                  withGhost;
    withGhost.record( 0, ghost, hashed_string( "NoSuchPawn" ), ControlRig::makeMove( 1.0f ) );

    SW_EXPECT_EQUAL( 1u, IntentPlayback::start( rig._manager, history, false, 0 ) );
    rig.update();
    SW_EXPECT_EQUAL( 1.0f, rig._pPawn->getIntent()._move._y );
    GameObject* pTrackObject = rig._manager.findGameObjectByName( hashed_string( "IntentTrack.Hero" ) );
    SW_EXPECT_TRUE( pTrackObject != nullptr );

    uint32 skipped = 0;
    SW_EXPECT_EQUAL( 0u, IntentPlayback::start( rig._manager, withGhost, false, 0, &skipped ) );
    SW_EXPECT_EQUAL( 1u, skipped );
}

// 다시 재생해도 조종자를 하나만 쓴다. 되감기는 키 틱에서 이어 돈다.
SW_TEST_CASE( IntentPlaybackTest, RestartReusesTheControllerAndHonoursTheFirstIndex )
{
    ControlRig                            rig;
    ControlIntentHistory                  history;
    const TComponentHandle<PawnComponent> pawn{};
    history.record( 0, pawn, hashed_string( "Hero" ), ControlRig::makeMove( 0.5f ) );
    history.record( 1, pawn, hashed_string( "Hero" ), ControlRig::makeMove( 1.0f ) );

    SW_EXPECT_EQUAL( 1u, IntentPlayback::start( rig._manager, history, false, 1 ) );
    rig.update();
    SW_EXPECT_EQUAL( 1.0f, rig._pPawn->getIntent()._move._y ); // 둘째 틱부터 나온다

    const uint32 objectCount = rig._manager.getComponentRegistry().getAll<ControllerComponent>().size();
    SW_EXPECT_EQUAL( 1u, IntentPlayback::start( rig._manager, history, false, 0 ) );
    SW_EXPECT_EQUAL( objectCount, static_cast<uint32>( rig._manager.getComponentRegistry().getAll<ControllerComponent>().size() ) );

    // 트랙보다 뒤에서 이어 돌면 그 폰은 이미 끝났다 — 건드리지 않고 건너뛴 것도 아니다.
    uint32 skipped = 9;
    SW_EXPECT_EQUAL( 0u, IntentPlayback::start( rig._manager, history, false, 5, &skipped ) );
    SW_EXPECT_EQUAL( 0u, skipped );
}

SW_TEST_CASE( IntentPlaybackTest, MissingFileReportsAnError )
{
    SW_EXPECT_LOG( "File.NotFound" );
    ControlRig             rig;
    const expected<uint32> started = IntentPlayback::startFromFile( rig._manager, test::makeScratchPath( "SwNoSuchIntentFile.swintent" ), false );
    SW_REQUIRE_FALSE( started.has_value() );
    SW_EXPECT_FALSE( started.error().message().empty() );
}

/** @brief [ControlSystemTest] 바라보던 쪽은 상태에 실리고 다시 빙의한 조종자가 잇는다 — 불러온 플레이어가 늘 같은 쪽을 보지 않는다 */
SW_TEST_CASE( ControlSystemTest, RestoredPawnKeepsTheControlRotation )
{
    ControlRig rig;
    Archive    forward;
    rig._pPawn->writeState( forward );
    SW_EXPECT_EQUAL( uint64{ 0 }, static_cast<uint64>( forward.getSize() ) );
    rig._pPawn->requestControlRotation( 1.25f, -0.5f );
    rig.update();
    Archive written;
    rig._pPawn->writeState( written );
    vector<uint8> bytes;
    written.writeData( bytes );

    PawnComponent* pRebuilt = rig._manager.createGameObject( hashed_string( "Rebuilt" ) )->addComponent<PawnComponent>();
    SW_REQUIRE_NOT_NULL( pRebuilt );
    pRebuilt->restoreState( std::move( bytes ) );
    rig._pFirst->possess( *pRebuilt );
    SW_EXPECT_NEAR( 1.25f, rig._pFirst->getControlYaw(), 1.0e-3f );
    SW_EXPECT_NEAR( -0.5f, rig._pFirst->getControlPitch(), 1.0e-3f );
}
