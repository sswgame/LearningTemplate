#include "pch.h"

#include "Core/Memory/Memory.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Base/Actor/Control/Intent/IntentReplayBar.h"
#include "GameFramework/Base/Actor/Control/Intent/IntentRewindBuffer.h"
#include "GameFramework/Base/Actor/Control/Intent/IntentRewindSession.h"

#include "TestFramework/TestFramework.h"

// 되감기 키 상태 — 간격의 배수 틱에 찍고, 목표 이하의 가장 가까운 키를 찾고, 상한을 넘으면 오래된 것부터 버리고, 되감은 뒤 미래를 버린다.

using namespace sw;

/**
 * @brief [IntentRewindTest] 키는 간격마다, 찾기는 목표 이하의 가장 가까운 것, 상한은 오래된 것부터, 되감은 뒤에는 미래를 버리고 다시 쌓는다
 */
SW_TEST_CASE( IntentRewindTest, KeySnapshotsCoverTheRewindWindow )
{
    IntentRewindConfig config;
    config._intervalTicks = 10;
    config._maxMegabytes  = 1;
    IntentRewindBuffer buffer;
    SW_ASSERT_TRUE( buffer.initialize( config ) );
    SW_EXPECT_TRUE( buffer.isKeyTick( 20 ) );
    SW_EXPECT_FALSE( buffer.isKeyTick( 21 ) );

    // 키 하나 = 0.3 MB — 넷째를 넣으면 상한(1 MB)을 넘어 첫째가 빠진다.
    const size_t keyBytes = constant::kBytesPerMegabyte * 3 / 10;
    for ( uint32 tick = 0; tick <= 30; tick += 10 )
    {
        SW_ASSERT_TRUE( buffer.add( tick, vector<uint8>( keyBytes, static_cast<uint8>( tick ) ) ) );
    }
    SW_EXPECT_EQUAL( size_t( 3 ), buffer.getCount() );
    SW_EXPECT_NULL( buffer.findAtOrBefore( 5 ) ); // 틱 0 은 상한으로 빠졌다
    const IntentRewindBuffer::KeySnapshot* pKey = buffer.findAtOrBefore( 27 );
    SW_ASSERT_NOT_NULL( pKey );
    SW_EXPECT_EQUAL( 20u, pKey->_tick );
    SW_EXPECT_EQUAL( uint8( 20 ), pKey->_bytes.front() );

    // 틱 12 로 되감으면 그 뒤의 키는 다른 미래다 — 버리고, 거기서부터 다시 쌓는다.
    buffer.discardAfter( 12 );
    SW_EXPECT_EQUAL( size_t( 1 ), buffer.getCount() );
    SW_EXPECT_EQUAL( keyBytes, buffer.getByteCount() );
    SW_EXPECT_TRUE( buffer.add( 20, vector<uint8>( 4, 0 ) ) );

    SW_TEST_DEFENSIVE_SCOPE( "rewind keys out of order, over budget or with bad settings are refused" );
    SW_EXPECT_FALSE( buffer.add( 20, vector<uint8>( 4, 0 ) ) );
    SW_EXPECT_FALSE( buffer.add( 40, vector<uint8>( constant::kBytesPerMegabyte + 1, 0 ) ) );
    IntentRewindConfig broken;
    broken._intervalTicks = 0;
    SW_EXPECT_FALSE( buffer.initialize( broken ) );
    SW_EXPECT_EQUAL( 10u, buffer.getIntervalTicks() ); // 거절하면 이전 설정 그대로다
}

namespace
{
    /** @brief 결정적인 가짜 세계입니다 — 상태는 틱마다 의도(틱의 함수)로 갱신되는 정수 하나이고, 키는 상태에 채움 바이트를 붙인 것입니다. */
    class FakeRewindWorld final : public IIntentRewindWorld
    {
    public:
        explicit FakeRewindWorld( size_t keyPaddingBytes )
            : _keyPaddingBytes{ keyPaddingBytes }
        {
        }

        uint32 getTick() const override { return _tick; }

        [[nodiscard]] bool captureKey( vector<uint8>& outBytes ) override
        {
            outBytes.assign( sizeof( int64 ) + _keyPaddingBytes, 0 );
            Memory::copy( outBytes.data(), &_state, sizeof( int64 ) );
            ++_captureCount;
            return true;
        }

        [[nodiscard]] bool restoreKey( uint32 keyTick, const vector<uint8>& bytes ) override
        {
            if ( _bRefuseRestore || bytes.size() < sizeof( int64 ) )
                return false;
            Memory::copy( &_state, bytes.data(), sizeof( int64 ) );
            _tick = keyTick;
            ++_restoreCount;
            return true;
        }

        void stepTick() override
        {
            const int64 intent = static_cast<int64>( ( _tick * 7u ) % 5u ) - 2;
            _state             = ( _state * 3 + intent ) % 1000003;
            ++_tick;
        }

        size_t _keyPaddingBytes;
        int64  _state{ 1 };
        uint32 _tick{ 0 };
        int32  _captureCount{ 0 };
        int32  _restoreCount{ 0 };
        bool   _bRefuseRestore{ false };
    };

    /** @brief 호스트처럼 @p tickCount 틱을 돌리며 틱마다 상태를 적습니다(`listState[t]` = 틱 t 에 닿았을 때의 상태). */
    void runHost( IntentRewindSession& session, FakeRewindWorld& world, uint32 tickCount, vector<int64>& inoutListState )
    {
        inoutListState.resize( 1, world._state );
        for ( uint32 index = 0; index < tickCount; ++index )
        {
            SW_ASSERT_TRUE( session.captureIfDue() );
            world.stepTick();
            if ( inoutListState.size() <= world._tick )
                inoutListState.resize( world._tick + 1 );
            inoutListState[world._tick] = world._state;
        }
    }

    /** @brief 입력 관리자와 그것을 읽는 UI 시스템입니다. */
    struct ReplayBarFixture
    {
        InputManager _input;
        UISystem     _ui;

        ReplayBarFixture()
            : _input{}
            , _ui{}
        {
            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr ) );
        }

        ~ReplayBarFixture()
        {
            _ui.shutdown();
            _input.shutdown();
        }

        ReplayBarFixture( const ReplayBarFixture& )            = delete;
        ReplayBarFixture& operator=( const ReplayBarFixture& ) = delete;
    };
} // namespace

/**
 * @brief [IntentRewindTest] 되감기 세션 — 키로 되돌린 뒤 목표까지 다시 돈 상태가 처음과 같고, 목표 뒤의 키는 버려지며, 긴 구간은 나눠 돌고, 키 밖의 목표는 거절한다
 */
SW_TEST_CASE( IntentRewindTest, SeekReplaysFromKeyToTheSameState )
{
    FakeRewindWorld    world( 0 );
    IntentRewindConfig config;
    config._intervalTicks = 10;
    config._maxMegabytes  = 1;
    IntentRewindSession session;
    SW_ASSERT_TRUE( session.initialize( config, &world ) );

    vector<int64> listState;
    runHost( session, world, 95, listState );
    SW_EXPECT_EQUAL( 10, world._captureCount ); // 틱 0 · 10 · … · 90
    const int64 finalState = world._state;

    // 틱 37 로 — 틱 30 의 키로 되돌리고 7 틱을 다시 돈다. 나눠 돌 수 있다.
    SW_ASSERT_TRUE( session.beginSeek( 37 ) );
    SW_EXPECT_TRUE( session.isSeeking() );
    SW_EXPECT_EQUAL( 30u, world.getTick() );
    SW_EXPECT_EQUAL( 7u, session.getRemainingTickCount() );
    SW_EXPECT_EQUAL( 4u, session.stepSeek( 3 ) );
    SW_EXPECT_TRUE( session.isSeeking() );
    SW_EXPECT_EQUAL( 0u, session.stepSeek( 100 ) );
    SW_EXPECT_FALSE( session.isSeeking() );
    SW_EXPECT_EQUAL( 37u, world.getTick() );
    SW_EXPECT_EQUAL( listState[37], world._state ); // 결정적이라 처음과 같은 궤적

    // 목표 뒤(40 이후)의 키는 버려졌고, 거기서부터 다시 쌓는다. 끝까지 돌면 처음과 같은 상태다.
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( session.getBuffer().getCount() ) ); // 0 · 10 · 20 · 30
    vector<int64> listAgain;
    runHost( session, world, 58, listAgain );
    SW_EXPECT_EQUAL( 95u, world.getTick() );
    SW_EXPECT_EQUAL( finalState, world._state );
    SW_EXPECT_EQUAL( 10u, static_cast<uint32>( session.getBuffer().getCount() ) );

    // 앞으로 가는 목표는 되돌리지 않고 그대로 돈다.
    const int32 restoresBefore = world._restoreCount;
    SW_ASSERT_TRUE( session.beginSeek( 99 ) );
    SW_EXPECT_EQUAL( restoresBefore, world._restoreCount );
    SW_EXPECT_EQUAL( 0u, session.stepSeek( 100 ) );
    SW_EXPECT_EQUAL( 99u, world.getTick() );

    // 목표가 키 틱 자체면 되돌리기만 하고 돌 것이 없다.
    SW_ASSERT_TRUE( session.beginSeek( 60 ) );
    SW_EXPECT_FALSE( session.isSeeking() );
    SW_EXPECT_EQUAL( 60u, world.getTick() );
    SW_EXPECT_EQUAL( listState[60], world._state );

    // 돌던 중에 멈추면 그 자리에 둔다.
    SW_ASSERT_TRUE( session.beginSeek( 75 ) );
    SW_EXPECT_EQUAL( 10u, session.stepSeek( 5 ) ); // 60 에서 5 틱을 돌아 65, 남은 10
    session.cancelSeek();
    SW_EXPECT_FALSE( session.isSeeking() );
    SW_EXPECT_EQUAL( 65u, world.getTick() );
}

/**
 * @brief [IntentRewindTest] 상한으로 버려진 앞쪽의 틱 · 되돌리지 못하는 세계는 거절하고 세계를 건드리지 않는다
 */
SW_TEST_CASE( IntentRewindTest, SeekRefusesWhatTheKeysDoNotCover )
{
    FakeRewindWorld    world( constant::kBytesPerMegabyte * 3 / 10 ); // 키 하나 0.3 MB — 상한 1 MB 에 셋
    IntentRewindConfig config;
    config._intervalTicks = 10;
    config._maxMegabytes  = 1;
    IntentRewindSession session;
    SW_ASSERT_TRUE( session.initialize( config, &world ) );
    vector<int64> listState;
    runHost( session, world, 60, listState ); // 키 0 · 10 · … · 50 중 오래된 것부터 버려져 30 · 40 · 50 이 남는다
    uint32 earliest = 0;
    SW_ASSERT_TRUE( session.findEarliestSeekableTick( earliest ) );
    SW_EXPECT_EQUAL( 30u, earliest );

    SW_TEST_DEFENSIVE_SCOPE( "a seek older than the oldest kept key, or into a world that refuses, is refused and leaves the world alone" );
    const int64  stateBefore = world._state;
    const uint32 tickBefore  = world.getTick();
    SW_EXPECT_FALSE( session.beginSeek( 12 ) );
    SW_EXPECT_FALSE( session.isSeeking() );
    SW_EXPECT_EQUAL( tickBefore, world.getTick() );
    SW_EXPECT_EQUAL( stateBefore, world._state );

    world._bRefuseRestore = true;
    SW_EXPECT_FALSE( session.beginSeek( 42 ) );
    SW_EXPECT_EQUAL( tickBefore, world.getTick() );
    world._bRefuseRestore = false;
    SW_EXPECT_TRUE( session.beginSeek( 42 ) ); // 거절된 시도가 버퍼를 깎지 않았다
    SW_EXPECT_EQUAL( 40u, world.getTick() );
    SW_EXPECT_EQUAL( 0u, session.stepSeek( 100 ) );
    SW_EXPECT_EQUAL( listState[42], world._state );

    IntentRewindConfig broken;
    broken._intervalTicks = 0;
    SW_EXPECT_FALSE( session.initialize( broken, &world ) );
    SW_EXPECT_FALSE( session.initialize( config, nullptr ) );
}

/**
 * @brief [IntentRewindTest] 되감기 막대 — 슬라이더를 끌면 되감기가 시작돼 프레임마다 나눠 돌고, 일시정지 · 1 초 되감기 · 닫기 버튼이 호스트의 틱 여부를 바꾼다
 * @details 막대 문서 `common/ui/replaybar.ui.xml` 의 이름(`Scrub` · `TimeText` · `PlayPauseLabel`)과 명령이 코드와 맞는지도 이 테스트가 잡는다. 디바이스 없음(nogpu).
 */
SW_TEST_CASE( IntentRewindTest, ReplayBarSeeksPausesAndSteps )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    ReplayBarFixture   fixture;
    FakeRewindWorld    world( 0 );
    IntentRewindConfig config;
    config._intervalTicks = 10;
    config._maxMegabytes  = 1;
    IntentRewindSession session;
    SW_ASSERT_TRUE( session.initialize( config, &world ) );
    vector<int64> listState;
    runHost( session, world, 95, listState );

    IntentReplayBar bar;
    SW_EXPECT_TRUE( bar.shouldHostStep() ); // 열기 전에는 호스트가 평소대로 돈다
    SW_ASSERT_TRUE( bar.open( fixture._ui, "common/ui/replaybar.ui.xml", session, 1.0f / 60.0f, 10 ) );
    UIScreen* pScreen = fixture._ui.getActiveScreen();
    SW_ASSERT_NOT_NULL( pScreen );
    SliderWidget* pScrub = pScreen->getTree().findWidget<SliderWidget>( "Scrub" );
    SW_ASSERT_NOT_NULL( pScrub );
    bar.update();
    SW_EXPECT_NEAR_EQUAL( 95.0f, pScrub->getValue(), 1.0e-3f ); // 슬라이더가 재생 틱을 따라간다
    SW_EXPECT_TRUE( bar.shouldHostStep() );

    // 슬라이더를 47 로 끌면 틱 40 의 키로 되돌아가고, 프레임마다 10 틱씩 돌아 47 에 닿는다.
    pScrub->setValueFromUser( 47.0f );
    SW_EXPECT_TRUE( session.isSeeking() );
    SW_EXPECT_FALSE( bar.shouldHostStep() ); // 되감는 동안 화면은 멈춘 그림
    bar.update();
    SW_EXPECT_FALSE( session.isSeeking() );
    SW_EXPECT_EQUAL( 47u, world.getTick() );
    SW_EXPECT_EQUAL( listState[47], world._state );
    SW_EXPECT_NEAR_EQUAL( 47.0f, pScrub->getValue(), 1.0e-3f );
    SW_EXPECT_TRUE( bar.shouldHostStep() );

    // 일시정지 단추 — 호스트가 서고 글이 바뀐다.
    Widget* pPlayPause = pScreen->getTree().findWidgetByName( "PlayPause" );
    SW_ASSERT_NOT_NULL( pPlayPause );
    pScreen->dispatchCommand( hashed_string( "Replay.TogglePause" ), *pPlayPause );
    SW_EXPECT_TRUE( bar.isPaused() );
    SW_EXPECT_FALSE( bar.shouldHostStep() );
    SW_EXPECT_STREQ( "Play", pScreen->getTree().findWidget<TextWidget>( "PlayPauseLabel" )->getText().c_str() );
    pScreen->dispatchCommand( hashed_string( "Replay.TogglePause" ), *pPlayPause );
    SW_EXPECT_FALSE( bar.isPaused() );
    SW_EXPECT_STREQ( "Pause", pScreen->getTree().findWidget<TextWidget>( "PlayPauseLabel" )->getText().c_str() );

    // 1 초(60 틱) 되감기 — 가장 오래된 키(0)보다 앞으로는 가지 않는다.
    Widget* pStepBack = pScreen->getTree().findWidgetByName( "StepBack" );
    SW_ASSERT_NOT_NULL( pStepBack );
    pScreen->dispatchCommand( hashed_string( "Replay.StepBack" ), *pStepBack );
    SW_EXPECT_EQUAL( 0u, world.getTick() );
    SW_EXPECT_EQUAL( listState[0], world._state );

    // 닫기 — 호스트가 평소대로 돈다.
    Widget* pClose = pScreen->getTree().findWidgetByName( "Close" );
    SW_ASSERT_NOT_NULL( pClose );
    pScreen->dispatchCommand( hashed_string( "Replay.Close" ), *pClose );
    SW_EXPECT_FALSE( bar.isOpen() );
    SW_EXPECT_TRUE( bar.shouldHostStep() );
}
