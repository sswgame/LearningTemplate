// 파티 아레나 키트 — 튕김 타이밍 콤보 · 높이 상한 · 놓치면 끊김 · 미리 누르기 창, 공중 공격으로 밀어 떨어뜨리기 · 점수 귀속 · 부활,
// 내려찍기는 낮은 상대만 민다, 무작위 아이템(씨앗 결정성 · 수명 · 슈퍼 튕김), 라운드 시간 제한, 입력 직렬화 · 같은 입력이면 같은 판, 라운드 묶음 순위 점수 · 우승.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "GameFramework/Kits/Casual/PartyArena/PartyItemSpawner.h"
#include "GameFramework/Kits/Casual/PartyArena/PartyRoundSeries.h"
#include "GameFramework/Kits/Casual/PartyArena/TrampolineArena.h"

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
            count += event._kind == kind ? 1 : 0;
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
            arena.step();
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
        arena.step();
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
            arena.step();
        int32 airSteps = 0;
        while ( arena.findPlayer( 0 )->_state == TrampolinePlayerState::Air && airSteps < 600 )
        {
            arena.step();
            ++airSteps;
        }
        // 같은 높이로 다시 튄 뒤, 닿기 leadSteps 걸음 전에 누른다.
        for ( int32 frame = 0; frame < 20 && arena.findPlayer( 0 )->_state == TrampolinePlayerState::Contact; ++frame )
            arena.step();
        for ( int32 frame = 0; frame < airSteps - leadSteps; ++frame )
            arena.step();
        TrampolineInput jump;
        jump._bJumpPressed = SW_TRUE;
        stepWith( arena, 0, jump );
        for ( int32 frame = 0; frame < 40 && arena.findPlayer( 0 )->_state != TrampolinePlayerState::Contact; ++frame )
            arena.step();
        for ( int32 frame = 0; frame < 20 && arena.findPlayer( 0 )->_state == TrampolinePlayerState::Contact; ++frame )
            arena.step();
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
        arena.step();
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
            spawner.update( kPartyArenaStep );
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
        SW_EXPECT_NEAR_EQUAL( float3::getDistance( listFirst[index], listSecond[index] ), 0.0f, 0.0001f );
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
            outListValue.push_back( static_cast<float32>( score ) );
    };
    vector<float32> listFirst;
    vector<float32> listSecond;
    runScript( listFirst );
    runScript( listSecond );
    SW_ASSERT_EQUAL( static_cast<int32>( listFirst.size() ), static_cast<int32>( listSecond.size() ) );
    for ( size_t index = 0; index < listFirst.size(); ++index )
        SW_EXPECT_TRUE( listFirst[index] == listSecond[index] );
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
        won += event._kind == PartySeriesEvent::Kind::SeriesWon ? 1 : 0;
    SW_EXPECT_EQUAL( won, 1 );
}
