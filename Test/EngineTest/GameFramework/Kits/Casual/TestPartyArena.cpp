// 파티 아레나 키트 — 튕김 타이밍 콤보 · 높이 상한 · 놓치면 끊김 · 미리 누르기 창, 공중 공격으로 밀어 떨어뜨리기 · 점수 귀속 · 부활,
// 내려찍기는 낮은 상대만 민다, 무작위 아이템(씨앗 결정성 · 수명 · 슈퍼 튕김), 라운드 시간 제한, 입력 직렬화 · 같은 입력이면 같은 판, 라운드 묶음 순위 점수 · 우승.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Casual/PartyArena/PartyItemSpawner.h"
#include "GameFramework/Kits/Genre/Casual/PartyArena/PartyRoundSeries.h"
#include "GameFramework/Kits/Genre/Casual/PartyArena/TrampolineArena.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr float32 kPartyArenaStep = 1.0f / 60.0f;

    constexpr const utf8* kPartyItemXml = R"(<PartyItems minInterval="0.5" maxInterval="0.5" lifetime="30" radius="6" height="1.5" maxActive="1">
        <Item id="spring" effect="SuperBounce"/>
      </PartyItems>)";

    /** @brief 아이템이 늦게 나오게 하고 둘이 하는 아레나입니다. */
    bool beginArena( TrampolineArena& outArena, const TrampolineSettings& settings = TrampolineSettings{}, float32 roundTime = 0.0f, uint32 seed = 7u )
    {
        PartyItemSpawnSettings item;
        item._minInterval = 1000.0f;
        item._maxInterval = 1000.0f;
        outArena.getItemSpawner().initialize( item, seed );
        if ( outArena.initialize( settings, 2, roundTime, 0, seed ) == false )
            return false;
        outArena.start();
        return true;
    }

    /** @brief 한 걸음 — 입력을 넣고 고정 걸음 하나를 나아갑니다. */
    void stepWith( TrampolineArena& arena, int32 player, const TrampolineInput& input )
    {
        arena.setInput( player, input );
        arena.step();
    }

    int32 countEvents( const vector<TrampolineEvent>& listEvent, TrampolineEvent::Kind kind )
    {
        int32 count = 0;
        for ( const TrampolineEvent& event : listEvent )
        {
            count += event._kind == kind ? 1 : 0;
        }
        return count;
    }

    /** @brief 플레이어 @p player 가 다음에 면에 닿는 걸음까지 나아갑니다(그 걸음의 알림을 돌려준다). */
    bool runUntilLanded( TrampolineArena& arena, int32 player, vector<TrampolineEvent>& outListEvent )
    {
        for ( int32 frame = 0; frame < 600; ++frame )
        {
            outListEvent.clear();
            arena.step();
            arena.drainEvents( outListEvent );
            for ( const TrampolineEvent& event : outListEvent )
            {
                if ( event._kind == TrampolineEvent::Kind::Landed && event._player == player )
                    return true;
            }
        }
        return false;
    }

    /** @brief 다음 튕김의 꼭대기 높이입니다. */
    float32 measureApex( TrampolineArena& arena, int32 player )
    {
        float32 apex = 0.0f;
        bool    bAir = false;
        for ( int32 frame = 0; frame < 600; ++frame )
        {
            arena.step();
            const TrampolinePlayer* pBody = arena.findPlayer( player );
            if ( pBody->_state == TrampolinePlayerState::Air )
            {
                bAir = true;
                apex = MathUtil::max( apex, pBody->_position._y );
            }
            else if ( bAir )
            {
                break;
            }
        }
        return apex;
    }

    /** @brief 묶음 알림 하나가 기대와 같은가입니다. */
    bool isPartySeriesEvent( const PartySeriesEvent& event, PartySeriesEvent::Kind kind, int32 player, int32 value, int32 points, const hashed_string& roundId )
    {
        return event._kind == kind && event._player == player && event._value == value && event._points == points && event._roundId == roundId;
    }

    /** @brief 셋이 하는 아레나를 대본 입력으로 @p firstFrame 부터 @p frameCount 걸음 돌립니다(같은 걸음이면 같은 입력). */
    void runArenaScript( TrampolineArena& arena, int32 firstFrame, int32 frameCount )
    {
        for ( int32 frame = firstFrame; frame < firstFrame + frameCount; ++frame )
        {
            for ( int32 player = 0; player < 3; ++player )
            {
                TrampolineInput scriptInput;
                scriptInput._moveX          = MathUtil::sin( static_cast<float32>( frame + player * 20 ) * 0.05f );
                scriptInput._moveZ          = MathUtil::cos( static_cast<float32>( frame * 2 + player ) * 0.03f );
                scriptInput._bJumpPressed   = ( frame + player ) % 7 == 0 ? SW_TRUE : SW_FALSE;
                scriptInput._bAttackPressed = ( frame + player * 13 ) % 50 == 0 ? SW_TRUE : SW_FALSE;
                scriptInput._bPoundPressed  = ( frame + player * 31 ) % 97 == 0 ? SW_TRUE : SW_FALSE;
                arena.setInput( player, scriptInput );
            }
            arena.step();
        }
    }

    /** @brief 아이템이 0.5 초마다 나오는 셋의 아레나를 엽니다. */
    bool beginItemArena( TrampolineArena& outArena, uint32 seed )
    {
        if ( outArena.getItemSpawner().loadFromXmlText( kPartyItemXml, "PartyArenaTest" ) == false )
            return false;
        if ( outArena.initialize( TrampolineSettings{}, 3, 30.0f, 0, seed ) == false )
            return false;
        outArena.start();
        return true;
    }

    /** @brief 상태 바이트를 꺼냅니다. */
    vector<uint8> captureArenaBytes( const TrampolineArena& arena )
    {
        Archive archive;
        arena.writeState( archive );
        vector<uint8> bytes;
        archive.writeData( bytes );
        return bytes;
    }
} // namespace

SW_TEST_CASE( PartyArenaTest, TimedBouncesBuildComboUpToTheCap )
{
    TrampolineArena arena;
    SW_EXPECT_FALSE( arena.initialize( TrampolineSettings{}, 1, 0.0f, 0, 1u ) );
    SW_EXPECT_FALSE( arena.initialize( TrampolineSettings{}, 5, 0.0f, 0, 1u ) );
    SW_ASSERT_TRUE( beginArena( arena ) );
    SW_EXPECT_NEAR_EQUAL( arena.computeBounceHeight( 2 ), 2.0f + 1.5f, 0.001f );
    SW_EXPECT_NEAR_EQUAL( arena.computeBounceHeight( 9 ), 2.0f + 3.0f, 0.001f ); // 상한 4

    // 닿는 순간마다 누르면 콤보가 1, 2, 3, 4, 4 로 오른다.
    vector<TrampolineEvent> listEvent;
    vector<int32>           listCombo;
    for ( int32 bounce = 0; bounce < 5; ++bounce )
    {
        SW_ASSERT_TRUE( runUntilLanded( arena, 0, listEvent ) );
        TrampolineInput jump;
        jump._bJumpPressed = SW_TRUE;
        stepWith( arena, 0, jump );
        for ( int32 frame = 0; frame < 20 && arena.findPlayer( 0 )->_state == TrampolinePlayerState::Contact; ++frame )
        {
            arena.step();
        }
        listCombo.push_back( arena.findPlayer( 0 )->_combo );
    }
    SW_ASSERT_EQUAL( static_cast<int32>( listCombo.size() ), 5 );
    SW_EXPECT_EQUAL( listCombo[0], 1 );
    SW_EXPECT_EQUAL( listCombo[3], 4 );
    SW_EXPECT_EQUAL( listCombo[4], 4 );
    SW_EXPECT_NEAR_EQUAL( measureApex( arena, 0 ), arena.computeBounceHeight( 4 ), 0.15f );

    // 누르지 않으면 콤보가 끊기고 기본 높이로 튄다.
    SW_ASSERT_TRUE( runUntilLanded( arena, 0, listEvent ) );
    listEvent.clear();
    for ( int32 frame = 0; frame < 20 && arena.findPlayer( 0 )->_state == TrampolinePlayerState::Contact; ++frame )
    {
        arena.step();
    }
    arena.drainEvents( listEvent );
    SW_EXPECT_EQUAL( countEvents( listEvent, TrampolineEvent::Kind::ComboBroken ), 1 );
    SW_EXPECT_EQUAL( arena.findPlayer( 0 )->_combo, 0 );
    SW_EXPECT_NEAR_EQUAL( measureApex( arena, 0 ), 2.0f, 0.15f );
}

SW_TEST_CASE( PartyArenaTest, EarlyPressCountsOnlyInsideTheWindow )
{
    // 닿기 0.033 초 전(Perfect ±0.05 안)은 성공, 0.25 초 전은 실패.
    auto bounceWithLead = [&]( int32 leadSteps ) -> int32
    {
        TrampolineArena arena;
        SW_EXPECT_TRUE( beginArena( arena ) );
        vector<TrampolineEvent> listEvent;
        (void)runUntilLanded( arena, 0, listEvent ); // 첫 착지 — 이후 튕김의 걸음 수를 잰다
        for ( int32 frame = 0; frame < 20 && arena.findPlayer( 0 )->_state == TrampolinePlayerState::Contact; ++frame )
        {
            arena.step();
        }
        int32 airSteps = 0;
        while ( arena.findPlayer( 0 )->_state == TrampolinePlayerState::Air && airSteps < 600 )
        {
            arena.step();
            ++airSteps;
        }
        // 같은 높이로 다시 튄 뒤, 닿기 leadSteps 걸음 전에 누른다.
        for ( int32 frame = 0; frame < 20 && arena.findPlayer( 0 )->_state == TrampolinePlayerState::Contact; ++frame )
        {
            arena.step();
        }
        for ( int32 frame = 0; frame < airSteps - leadSteps; ++frame )
        {
            arena.step();
        }
        TrampolineInput jump;
        jump._bJumpPressed = SW_TRUE;
        stepWith( arena, 0, jump );
        for ( int32 frame = 0; frame < 40 && arena.findPlayer( 0 )->_state != TrampolinePlayerState::Contact; ++frame )
        {
            arena.step();
        }
        for ( int32 frame = 0; frame < 20 && arena.findPlayer( 0 )->_state == TrampolinePlayerState::Contact; ++frame )
        {
            arena.step();
        }
        return arena.findPlayer( 0 )->_combo;
    };
    SW_EXPECT_EQUAL( bounceWithLead( 2 ), 1 );
    SW_EXPECT_EQUAL( bounceWithLead( 15 ), 0 );
}

SW_TEST_CASE( PartyArenaTest, AirAttackRingsOutOpponentAndCreditsAttacker )
{
    TrampolineSettings settings;
    settings._attackKnockback = 20.0f;
    settings._knockbackDrag   = 1.0f;
    settings._respawnDelay    = 1.0f;
    TrampolineArena arena;
    SW_ASSERT_TRUE( beginArena( arena, settings ) );

    // 둘은 x = +4 · −4 에서 시작한다. 0 번이 −x 로 다가가 가까우면 친다.
    vector<TrampolineEvent> listEvent;
    const TrampolineEvent*  pRingOut = nullptr;
    bool                    bSwung   = false;
    TrampolineEvent         ringOut;
    for ( int32 frame = 0; frame < 60 * 10 && pRingOut == nullptr; ++frame )
    {
        const float32   distance = float3::getDistance( arena.findPlayer( 0 )->_position, arena.findPlayer( 1 )->_position );
        TrampolineInput input;
        input._moveX = bSwung ? 0.0f : -1.0f;
        if ( bSwung == false && distance < 2.5f && arena.findPlayer( 0 )->_state == TrampolinePlayerState::Air )
        {
            input._bAttackPressed = SW_TRUE;
            bSwung                = true;
        }
        stepWith( arena, 0, input );
        listEvent.clear();
        arena.drainEvents( listEvent );
        for ( const TrampolineEvent& event : listEvent )
        {
            if ( event._kind == TrampolineEvent::Kind::RingOut )
            {
                ringOut  = event;
                pRingOut = &ringOut;
            }
        }
    }
    SW_ASSERT_TRUE( bSwung );
    SW_ASSERT_NOT_NULL( pRingOut );
    SW_EXPECT_EQUAL( ringOut._player, 1 );
    SW_EXPECT_EQUAL( ringOut._other, 0 ); // 민 사람이 점수를 받는다
    vector<int32> listScore;
    arena.computeRoundScores( listScore );
    SW_EXPECT_EQUAL( listScore[0], 1 );
    SW_EXPECT_EQUAL( listScore[1], 0 );
    SW_EXPECT_TRUE( arena.findPlayer( 1 )->_state == TrampolinePlayerState::Respawning );

    // 부활 대기 1 초 뒤 무적을 달고 돌아온다.
    for ( int32 frame = 0; frame < 70; ++frame )
    {
        arena.step();
    }
    SW_EXPECT_TRUE( arena.findPlayer( 1 )->_state != TrampolinePlayerState::Respawning );
    SW_EXPECT_TRUE( arena.findPlayer( 1 )->_invulnerableTimer.isActive() );
}

SW_TEST_CASE( PartyArenaTest, GroundPoundOnlyPushesLowOpponents )
{
    // 1 번은 타이밍 점프로 엇박이 되고, 0 번은 걸음 k 에 내려찍는다 — 상대가 낮을 때 닿은 경우만 밀린다.
    int32 pushedRuns = 0;
    int32 poundRuns  = 0;
    for ( int32 poundStep = 30; poundStep < 150; poundStep += 3 )
    {
        TrampolineSettings settings;
        settings._poundRadius = 9.0f; // 둘 사이(8 m)를 덮는다
        TrampolineArena arena;
        SW_ASSERT_TRUE( beginArena( arena, settings ) );
        vector<TrampolineEvent> listEvent;
        for ( int32 frame = 0; frame < 200; ++frame )
        {
            TrampolineInput first;
            first._bPoundPressed = frame == poundStep ? SW_TRUE : SW_FALSE;
            TrampolineInput second;
            second._bJumpPressed = arena.findPlayer( 1 )->_state == TrampolinePlayerState::Contact ? SW_TRUE : SW_FALSE;
            arena.setInput( 1, second );
            stepWith( arena, 0, first );
            listEvent.clear();
            arena.drainEvents( listEvent );
            for ( const TrampolineEvent& event : listEvent )
            {
                if ( event._kind != TrampolineEvent::Kind::GroundPound )
                    continue;
                ++poundRuns;
                pushedRuns += event._value > 0 ? 1 : 0;
                // 밀렸다면 그때 상대는 낮았고 기절했다.
                if ( event._value > 0 )
                    SW_EXPECT_TRUE( arena.findPlayer( 1 )->_stunTimer.isActive() );
            }
        }
    }
    SW_EXPECT_TRUE( poundRuns > 20 );
    SW_EXPECT_TRUE( pushedRuns > 0 );         // 낮을 때 맞춘 경우가 있고
    SW_EXPECT_TRUE( pushedRuns < poundRuns ); // 높이 떠 있을 때는 밀리지 않는다
}

SW_TEST_CASE( PartyArenaTest, ItemsSpawnDeterministicallyAndSuperBounceDoublesHeight )
{
    // 같은 씨앗이면 같은 자리 · 같은 순서다.
    auto collectSpawns = []( uint32 seed, vector<float3>& outListPosition )
    {
        PartyItemSpawner spawner;
        SW_EXPECT_TRUE( spawner.loadFromXmlText( kPartyItemXml, "PartyArenaTest" ) );
        PartyItemSpawnSettings settings = spawner.getSettings();
        settings._maxActive             = 3;
        settings._lifetime              = 1.0f;
        spawner.initialize( settings, seed );
        for ( int32 frame = 0; frame < 60 * 3; ++frame )
        {
            spawner.update( kPartyArenaStep );
        }
        vector<PartyItemEvent> listEvent;
        spawner.drainEvents( listEvent );
        for ( const PartyItemEvent& event : listEvent )
        {
            if ( event._kind == PartyItemEvent::Kind::Spawned )
                outListPosition.push_back( float3{ static_cast<float32>( event._serial ), 0.0f, 0.0f } );
        }
        for ( const PartyItemInstance& instance : spawner.getInstances() )
        {
            SW_EXPECT_TRUE( float3::getDistance( float3{ instance._position._x, 0.0f, instance._position._z }, float3{} ) <= 6.0f + 0.001f );
            outListPosition.push_back( instance._position );
        }
        return static_cast<int32>( listEvent.size() );
    };
    vector<float3> listFirst;
    vector<float3> listSecond;
    const int32    firstEvents  = collectSpawns( 42u, listFirst );
    const int32    secondEvents = collectSpawns( 42u, listSecond );
    SW_EXPECT_EQUAL( firstEvents, secondEvents );
    SW_ASSERT_EQUAL( static_cast<int32>( listFirst.size() ), static_cast<int32>( listSecond.size() ) );
    for ( size_t index = 0; index < listFirst.size(); ++index )
    {
        SW_EXPECT_NEAR_EQUAL( float3::getDistance( listFirst[index], listSecond[index] ), 0.0f, 0.0001f );
    }
    SW_EXPECT_TRUE( firstEvents > 5 ); // 0.5 초마다 나오고 1 초 수명으로 사라진다

    // 아레나 — 0 번이 아이템 쪽으로 가서 주우면 다음 튕김이 두 배 높이.
    TrampolineArena arena;
    SW_ASSERT_TRUE( arena.getItemSpawner().loadFromXmlText( kPartyItemXml, "PartyArenaTest" ) );
    SW_ASSERT_TRUE( arena.initialize( TrampolineSettings{}, 2, 0.0f, 0, 11u ) );
    arena.start();
    bool                    bPicked = false;
    vector<TrampolineEvent> listEvent;
    for ( int32 frame = 0; frame < 60 * 20 && bPicked == false; ++frame )
    {
        TrampolineInput                  input;
        const vector<PartyItemInstance>& listInstance = arena.getItemSpawner().getInstances();
        if ( listInstance.empty() == false )
        {
            const float3  body   = arena.findPlayer( 0 )->_position;
            const float3  delta  = float3{ listInstance[0]._position._x - body._x, 0.0f, listInstance[0]._position._z - body._z };
            const float32 length = MathUtil::sqrt( delta._x * delta._x + delta._z * delta._z );
            if ( length > 0.2f )
            {
                input._moveX = delta._x / length;
                input._moveZ = delta._z / length;
            }
        }
        stepWith( arena, 0, input );
        listEvent.clear();
        arena.drainEvents( listEvent );
        bPicked = countEvents( listEvent, TrampolineEvent::Kind::ItemPicked ) > 0;
    }
    SW_ASSERT_TRUE( bPicked );
    SW_EXPECT_TRUE( arena.findPlayer( 0 )->_bSuperBounce == SW_TRUE );
    SW_ASSERT_TRUE( runUntilLanded( arena, 0, listEvent ) );
    const float32 apex = measureApex( arena, 0 );
    SW_EXPECT_TRUE( apex > arena.computeBounceHeight( 0 ) * 2.0f - 0.2f );
    SW_EXPECT_TRUE( arena.findPlayer( 0 )->_bSuperBounce == SW_FALSE ); // 한 번 쓰면 사라진다
}

SW_TEST_CASE( PartyArenaTest, InputBytesRoundTripAndSameInputsReplay )
{
    TrampolineInput input;
    input._moveX          = -0.5f;
    input._moveZ          = 1.0f;
    input._bAttackPressed = SW_TRUE;
    BitWriter writer;
    TrampolineArena::writeInput( writer, input );
    SW_EXPECT_EQUAL( writer.getBitCount(), 19 );
    BitReader             reader( writer.getBytes().data(), writer.getByteCount() );
    const TrampolineInput decoded = TrampolineArena::readInput( reader );
    SW_EXPECT_NEAR_EQUAL( decoded._moveX, -0.5f, 0.01f );
    SW_EXPECT_NEAR_EQUAL( decoded._moveZ, 1.0f, 0.001f );
    SW_EXPECT_TRUE( decoded._bAttackPressed == SW_TRUE );
    SW_EXPECT_TRUE( decoded._bJumpPressed == SW_FALSE );

    // 같은 씨앗 · 같은 입력이면 같은 판(락스텝).
    auto runScript = []( vector<float32>& outListValue )
    {
        TrampolineArena arena;
        SW_EXPECT_TRUE( arena.getItemSpawner().loadFromXmlText( kPartyItemXml, "PartyArenaTest" ) );
        SW_EXPECT_TRUE( arena.initialize( TrampolineSettings{}, 3, 6.0f, 0, 99u ) );
        arena.start();
        for ( int32 frame = 0; frame < 60 * 7; ++frame )
        {
            for ( int32 player = 0; player < 3; ++player )
            {
                TrampolineInput scriptInput;
                scriptInput._moveX          = MathUtil::sin( static_cast<float32>( frame + player * 20 ) * 0.05f );
                scriptInput._moveZ          = MathUtil::cos( static_cast<float32>( frame * 2 + player ) * 0.03f );
                scriptInput._bJumpPressed   = ( frame + player ) % 7 == 0 ? SW_TRUE : SW_FALSE;
                scriptInput._bAttackPressed = ( frame + player * 13 ) % 50 == 0 ? SW_TRUE : SW_FALSE;
                scriptInput._bPoundPressed  = ( frame + player * 31 ) % 97 == 0 ? SW_TRUE : SW_FALSE;
                arena.setInput( player, scriptInput );
            }
            (void)arena.update( kPartyArenaStep );
        }
        SW_EXPECT_TRUE( arena.isRoundOver() ); // 6 초 제한
        for ( int32 player = 0; player < 3; ++player )
        {
            const TrampolinePlayer* pBody = arena.findPlayer( player );
            outListValue.push_back( pBody->_position._x );
            outListValue.push_back( pBody->_position._y );
            outListValue.push_back( pBody->_position._z );
            outListValue.push_back( static_cast<float32>( pBody->_combo ) );
        }
        vector<int32> listScore;
        arena.computeRoundScores( listScore );
        for ( int32 score : listScore )
        {
            outListValue.push_back( static_cast<float32>( score ) );
        }
    };
    vector<float32> listFirst;
    vector<float32> listSecond;
    runScript( listFirst );
    runScript( listSecond );
    SW_ASSERT_EQUAL( static_cast<int32>( listFirst.size() ), static_cast<int32>( listSecond.size() ) );
    for ( size_t index = 0; index < listFirst.size(); ++index )
    {
        SW_EXPECT_TRUE( listFirst[index] == listSecond[index] );
    }
}

SW_TEST_CASE( PartyArenaTest, RoundSeriesRanksRoundsAndCrownsFirstToTarget )
{
    PartyRoundSeries series;
    SW_EXPECT_FALSE( series.start( 3 ) ); // 라운드가 없다
    SW_ASSERT_TRUE( series.loadFromXmlText( R"(<PartySeries winScore="5" placementPoints="3,2,1">
        <Round id="trampoline" time="60" scoreLimit="5"/><Round id="sumo" time="45"/></PartySeries>)",
                                            "PartyArenaTest" ) );
    SW_EXPECT_EQUAL( series.getWinScore(), 5 );
    SW_EXPECT_EQUAL( static_cast<int32>( series.getRounds().size() ), 2 );
    SW_EXPECT_FALSE( series.start( 1 ) );
    SW_ASSERT_TRUE( series.start( 3 ) );
    SW_EXPECT_TRUE( series.getCurrentRound()->_id == hashed_string( "trampoline" ) );
    SW_EXPECT_EQUAL( series.getCurrentRound()->_scoreLimit, 5 );

    // 1 라운드: 2 · 5 · 2 → 1 번 1 위(3), 0 · 2 번은 공동 2 위(2 씩).
    SW_ASSERT_TRUE( series.reportRound( vector<int32>{ 2, 5, 2 } ) );
    SW_EXPECT_EQUAL( series.getTotal( 0 ), 2 );
    SW_EXPECT_EQUAL( series.getTotal( 1 ), 3 );
    SW_EXPECT_EQUAL( series.getTotal( 2 ), 2 );
    SW_EXPECT_TRUE( series.getCurrentRound()->_id == hashed_string( "sumo" ) );
    SW_EXPECT_FALSE( series.reportRound( vector<int32>{ 1, 2 } ) ); // 사람 수가 다르다

    // 2 라운드: 0 번과 1 번이 함께 5 점 — 동점이라 서든 데스.
    SW_ASSERT_TRUE( series.reportRound( vector<int32>{ 9, 4, 0 } ) ); // 0: 2+3=5, 1: 3+2=5, 2: 2+1=3
    SW_EXPECT_FALSE( series.isFinished() );
    SW_EXPECT_TRUE( series.getCurrentRound()->_id == hashed_string( "trampoline" ) ); // 목록을 다시 돈다

    // 3 라운드: 1 번 1 위 → 8 점으로 우승.
    SW_ASSERT_TRUE( series.reportRound( vector<int32>{ 1, 3, 2 } ) );
    SW_EXPECT_TRUE( series.isFinished() );
    SW_EXPECT_EQUAL( series.getWinner(), 1 );
    SW_EXPECT_EQUAL( series.getTotal( 1 ), 8 );
    SW_EXPECT_FALSE( series.reportRound( vector<int32>{ 1, 1, 1 } ) );

    vector<PartySeriesEvent> listEvent;
    series.drainEvents( listEvent );
    int32 won = 0;
    for ( const PartySeriesEvent& event : listEvent )
    {
        won += event._kind == PartySeriesEvent::Kind::SeriesWon ? 1 : 0;
    }
    SW_EXPECT_EQUAL( won, 1 );
}

/**
 * @brief [PartyArenaTest] 라운드 묶음 알림 — 순서(순위 → 다음 라운드 · 우승), 라운드 id(끝난 라운드 · 새 라운드), 순위 · 순위 점수, 목록보다 낮은 순위는 0 점
 */
SW_TEST_CASE( PartyArenaTest, RoundSeriesEventsCarryRoundIdRankAndPoints )
{
    PartyRoundSeries series;
    SW_ASSERT_TRUE( series.loadFromXmlText( R"(<PartySeries winScore="4" placementPoints="3,1"><Round id="trampoline"/><Round id="sumo"/></PartySeries>)",
                                            "PartyArenaTest" ) );
    SW_ASSERT_TRUE( series.start( 3 ) );
    const hashed_string trampoline( "trampoline" );
    const hashed_string sumo( "sumo" );
    using Kind = PartySeriesEvent::Kind;

    // 1 라운드 1 · 4 · 0 → 순위 2 · 1 · 3, 점수 1 · 3 · 0(3 위는 목록 밖). 아무도 4 점이 아니라 2 라운드(sumo).
    SW_ASSERT_TRUE( series.reportRound( vector<int32>{ 1, 4, 0 } ) );
    // 2 라운드 0 · 5 · 5 → 순위 3 · 1 · 1(같은 점수 같은 순위), 총점 1 · 6 · 3 → 1 번 우승(끝난 라운드 id).
    SW_ASSERT_TRUE( series.reportRound( vector<int32>{ 0, 5, 5 } ) );

    vector<PartySeriesEvent> listEvent;
    series.drainEvents( listEvent );
    SW_ASSERT_EQUAL( 9, static_cast<int32>( listEvent.size() ) );
    SW_EXPECT_TRUE( isPartySeriesEvent( listEvent[0], Kind::RoundStarted, -1, 0, 0, trampoline ) );
    SW_EXPECT_TRUE( isPartySeriesEvent( listEvent[1], Kind::RoundRanked, 0, 2, 1, trampoline ) );
    SW_EXPECT_TRUE( isPartySeriesEvent( listEvent[2], Kind::RoundRanked, 1, 1, 3, trampoline ) );
    SW_EXPECT_TRUE( isPartySeriesEvent( listEvent[3], Kind::RoundRanked, 2, 3, 0, trampoline ) );
    SW_EXPECT_TRUE( isPartySeriesEvent( listEvent[4], Kind::RoundStarted, -1, 1, 0, sumo ) );
    SW_EXPECT_TRUE( isPartySeriesEvent( listEvent[5], Kind::RoundRanked, 0, 3, 0, sumo ) );
    SW_EXPECT_TRUE( isPartySeriesEvent( listEvent[6], Kind::RoundRanked, 1, 1, 3, sumo ) );
    SW_EXPECT_TRUE( isPartySeriesEvent( listEvent[7], Kind::RoundRanked, 2, 1, 3, sumo ) );
    SW_EXPECT_TRUE( isPartySeriesEvent( listEvent[8], Kind::SeriesWon, 1, 6, 0, sumo ) );
    SW_EXPECT_EQUAL( 1, series.getWinner() );
    SW_EXPECT_EQUAL( 1, series.getRoundNumber() ); // 우승한 라운드에서 멈춘다
}

/**
 * @brief [PartyArenaTest] 아이템 스폰 빈도는 간격을 따른다 — 0.47 초 간격을 60 · 30 fps 로 60 초 돌리면 127 개(±1)
 * @details 끝난 프레임에 간격으로 덮으면 지나친 몫을 버려 간격이 `ceil( 간격 / dt ) × dt` 로 는다 — 60 fps 124 개, 30 fps 120 개.
 */
SW_TEST_CASE( PartyArenaTest, ItemSpawnRateDoesNotDependOnFrameRate )
{
    for ( const float32 framesPerSecond : { 60.0f, 30.0f } )
    {
        PartyItemSpawner spawner;
        SW_ASSERT_TRUE( spawner.loadFromXmlText( kPartyItemXml, "PartyArenaTest" ) );
        PartyItemSpawnSettings settings = spawner.getSettings();
        settings._minInterval           = 0.47f;
        settings._maxInterval           = 0.47f;
        settings._maxActive             = 100000;
        settings._lifetime              = 0.0f; // 주울 때까지 — 자리가 차서 시계가 멈추지 않게
        spawner.initialize( settings, 5u );
        const float32          seconds    = 60.0f;
        const int32            frameCount = static_cast<int32>( seconds * framesPerSecond + 0.5f );
        int32                  spawnCount = 0;
        vector<PartyItemEvent> listEvent;
        for ( int32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            spawner.update( 1.0f / framesPerSecond );
            listEvent.clear();
            spawner.drainEvents( listEvent );
            for ( const PartyItemEvent& event : listEvent )
            {
                spawnCount += event._kind == PartyItemEvent::Kind::Spawned ? 1 : 0;
            }
        }
        SW_EXPECT_NEAR_EQUAL( seconds / 0.47f, static_cast<float32>( spawnCount ), 1.0f );
    }
}

/**
 * @brief [PartyArenaTest] 상태 바이트 — 몸 · 콤보 · 놓인 아이템 · 아이템 난수 · 경기 점수가 다른 씨앗으로 연 아레나에 그대로 오고, 같은 입력을 더 넣어도 바이트가 같다.
 *        잘린 바이트 · 인원이 다른 아레나는 거절하고 그대로 둔다
 */
SW_TEST_CASE( PartyArenaTest, StateRoundTripContinuesTheSameArena )
{
    TrampolineArena arena;
    SW_ASSERT_TRUE( beginItemArena( arena, 99u ) );
    runArenaScript( arena, 0, 60 * 3 );
    vector<TrampolineEvent> listEvent;
    arena.drainEvents( listEvent );

    const vector<uint8> written = captureArenaBytes( arena );
    TrampolineArena     restored;
    SW_ASSERT_TRUE( beginItemArena( restored, 5u ) );
    Archive reader( written.data(), written.size() );
    SW_ASSERT_TRUE( restored.readState( reader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, reader.getRemainingBytes() );
    SW_EXPECT_TRUE( arena.getTime() == restored.getTime() );
    SW_EXPECT_EQUAL( arena.findPlayer( 1 )->_combo, restored.findPlayer( 1 )->_combo );
    SW_EXPECT_TRUE( arena.findPlayer( 2 )->_position._y == restored.findPlayer( 2 )->_position._y );
    SW_EXPECT_EQUAL( static_cast<int32>( arena.getItemSpawner().getInstances().size() ), static_cast<int32>( restored.getItemSpawner().getInstances().size() ) );
    SW_EXPECT_TRUE( written == captureArenaBytes( restored ) );

    // 같은 입력을 더 넣으면 같은 판 — 다음 아이템 자리(난수) · 부활 대기 · 점수까지 갈리지 않는다.
    runArenaScript( arena, 60 * 3, 60 * 3 );
    runArenaScript( restored, 60 * 3, 60 * 3 );
    SW_EXPECT_TRUE( captureArenaBytes( arena ) == captureArenaBytes( restored ) );

    TrampolineArena pair;
    SW_ASSERT_TRUE( pair.getItemSpawner().loadFromXmlText( kPartyItemXml, "PartyArenaTest" ) );
    SW_ASSERT_TRUE( pair.initialize( TrampolineSettings{}, 2, 30.0f, 0, 5u ) );
    Archive pairReader( written.data(), written.size() );
    SW_EXPECT_FALSE( pair.readState( pairReader ) );
    TrampolineArena truncated;
    SW_ASSERT_TRUE( beginItemArena( truncated, 5u ) );
    Archive cut( written.data(), written.size() - 1 );
    SW_EXPECT_FALSE( truncated.readState( cut ) );
    SW_EXPECT_TRUE( truncated.getTime() == 0.0f );
}
