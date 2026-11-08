#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Action/Fighting/FightingMatch.h"

// 3D 격투 키트의 규칙: 프레임 판정 · 가드 높이 · 상쇄 · 잡기와 풀기 · 라운드 · 레이지 · 히트 · 자세 · 횡이동 · 저글 · 벽꽝 · 선입력 · 롤백과 세이브 상태 · 캐릭터 카탈로그.
// 결함: 같은 프레임에 닿으면 0 번만 맞혔다(맞으면 기술이 끊겨 뒤쪽 적용이 지금 기술을 찾지 못했다) · 잡기끼리 닿으면 0 번이 늘 잡았다 · 자세 시간 설정 0 이면 저장이 범위 밖으로 멈췄다 ·
// 정의의 최대를 넘는 체력을 읽었다 · 카탈로그의 겹친 캐릭터 id 가 앞 정의를 말없이 덮어썼다. 키트 GF_Fighting(nogpu).

using namespace sw;

namespace
{
    constexpr const utf8* kFightingMoveXML = R"(<MoveCatalog>
  <Move id="jab" startup="3" active="2" recovery="6" damage="10" chip="2" hitstun="12" blockstun="6" height="Mid"><Hitbox x="0.6" y="0.8" w="1.4" h="1.6"/><Cancel moves="jab2" from="5" to="10"/></Move>
  <Move id="jab2" startup="3" active="2" recovery="6" damage="10" hitstun="12" blockstun="6" height="Mid"><Hitbox x="0.6" y="0.8" w="1.4" h="1.6"/></Move>
  <Move id="low" startup="3" active="2" recovery="6" damage="10" chip="1" hitstun="12" blockstun="6" height="Low"><Hitbox x="0.6" y="0.8" w="1.4" h="1.6"/></Move>
  <Move id="high" startup="3" active="2" recovery="6" damage="10" hitstun="12" blockstun="6" height="High"><Hitbox x="0.6" y="1.6" w="1.4" h="0.2"/></Move>
  <Move id="grab" startup="3" active="2" recovery="5" damage="30" height="Throw"><Hitbox x="0.6" y="0.8" w="1.4" h="1.6"/></Move>
  <Move id="launch" startup="3" active="2" recovery="6" damage="20" hitstun="12" height="Low" launcher="true"><Hitbox x="0.6" y="0.8" w="1.4" h="1.6"/></Move>
  <Move id="poke" startup="3" active="2" recovery="6" damage="10" hitstun="12" height="Low"><Hitbox x="0.6" y="0.8" w="1.4" h="1.6"/></Move>
  <Move id="knock" startup="3" active="2" recovery="6" damage="10" height="Low" knockdown="true"><Hitbox x="0.6" y="0.8" w="1.4" h="1.6"/></Move>
  <Move id="stomp" startup="3" active="2" recovery="6" damage="5" height="Low"><Hitbox x="0.6" y="0.8" w="1.4" h="1.6"/></Move>
  <Move id="splat" startup="3" active="2" recovery="6" damage="10" hitstun="12" height="Low" wallSplat="true"><Hitbox x="0.6" y="0.8" w="1.4" h="1.6"/></Move>
  <Move id="slow" startup="14" active="2" recovery="2" damage="10" hitstun="12" height="Mid"><Hitbox x="0.6" y="0.8" w="1.4" h="1.6"/></Move>
  <Move id="burst" startup="3" active="2" recovery="6" damage="0" hitstun="12" height="Mid"><Hitbox x="0.6" y="0.8" w="1.4" h="1.6"/></Move>
  <Move id="art" startup="3" active="2" recovery="6" damage="40" hitstun="12" height="Low"><Hitbox x="0.6" y="0.8" w="1.4" h="1.6"/></Move>
</MoveCatalog>)";

    // 버튼 이름 1, 2, 3, 4 가 비트 0 .. 3 이다.
    constexpr const utf8* kFightingCatalogXML = R"(<FighterCatalog buttons="1,2,3,4">
  <Fighter id="basic" name="Basic" sidestep="u" sidestepDown="d,n" jump="u/f">
    <Move id="jab" command="1"/>
    <Move id="jab2" command="2" stringOnly="true"/>
    <Move id="low" command="3"/>
    <Move id="grab" command="4" breakButtons="1"/>
  </Fighter>
  <Fighter id="combo" name="Combo" sidestep="u" sidestepDown="d,n" jump="u/f">
    <Move id="launch" command="1"/>
    <Move id="poke" command="2"/>
    <Move id="spin" frames="poke" command="3" screw="true"/>
    <Move id="bounce" frames="poke" command="4" bound="true"/>
  </Fighter>
  <Fighter id="ender" name="Ender" sidestep="u" sidestepDown="d,n" jump="u/f">
    <Move id="knock" command="1"/>
    <Move id="stomp" command="2" groundHit="true"/>
    <Move id="splat" command="3"/>
    <Move id="slow" command="4"/>
  </Fighter>
  <Fighter id="special" name="Special" sidestep="u" sidestepDown="d,n" jump="u/f">
    <Move id="art" command="1" rageArt="true"/>
    <Move id="burst" command="2" heatBurst="true"/>
    <Move id="track" frames="slow" command="3" tracking="true"/>
    <Move id="high" command="4"/>
  </Fighter>
  <Fighter id="stancer" name="Stancer" sidestep="u" sidestepDown="d,n" jump="u/f">
    <Move id="enter" frames="jab" command="1" enterStance="crane"/>
    <Move id="crane" frames="low" command="2" stance="crane"/>
  </Fighter>
</FighterCatalog>)";

    constexpr uint16 kFightingButton1 = 1 << 0;
    constexpr uint16 kFightingButton2 = 1 << 1;
    constexpr uint16 kFightingButton3 = 1 << 2;
    constexpr uint16 kFightingButton4 = 1 << 3;

    constexpr uint8 kFightingUp        = 8;
    constexpr uint8 kFightingUpForward = 9;
    constexpr uint8 kFightingDown      = 2;
    constexpr uint8 kFightingLeft      = 4;

    constexpr int32   kFightingMaxHealth = 170; ///< `FighterDef::_health` 의 초기값
    constexpr float32 kFightingTolerance = 1.0e-3f;

    /** @brief 카탈로그와 대전 하나입니다. 입력은 `_arrHold` 를 유지하고, 버튼은 `press` 가 한 프레임만 누릅니다. */
    struct FightingRulesTest
    {
        MoveCatalog           _moves{};
        FighterCatalog        _fighters{};
        FightingMatch         _match{};
        FightingSettings      _settings{};
        InputFrame            _arrHold[FightingMatch::kPlayerCount]{};
        vector<FightingEvent> _listEvent{};

        FightingRulesTest()
        {
            SW_REQUIRE_TRUE( _moves.loadFromXMLText( kFightingMoveXML, "FightingTest" ) );
            SW_REQUIRE_TRUE( _fighters.loadFromXMLText( kFightingCatalogXML, _moves, "FightingTest" ) );
        }

        const FighterDef& def( const utf8* pID ) const { return *_fighters.find( hashed_string( pID ) ); }

        /** @brief 대전을 열고 중립을 한 프레임 넣습니다(첫 입력이 "새로 누름" 으로 읽히도록). */
        [[nodiscard]] bool open( const utf8* pID0, const utf8* pID1 )
        {
            if ( _match.initialize( def( pID0 ), def( pID1 ), _settings ) == false )
                return false;
            _arrHold[0] = InputFrame{};
            _arrHold[1] = InputFrame{};
            _listEvent.clear();
            run( 1 );
            return true;
        }

        void run( int32 frames )
        {
            for ( int32 frame = 0; frame < frames; ++frame )
            {
                _match.advanceFrame( _arrHold[0], _arrHold[1] );
            }
        }

        /** @brief 두 쪽의 버튼을 이번 프레임에만 함께 누릅니다. */
        void pressBoth( uint16 buttons0, uint16 buttons1 )
        {
            InputFrame input0 = _arrHold[0];
            InputFrame input1 = _arrHold[1];
            input0._buttons   = buttons0;
            input1._buttons   = buttons1;
            _match.advanceFrame( input0, input1 );
        }

        void press( int32 player, uint16 buttons ) { pressBoth( player == 0 ? buttons : 0, player == 1 ? buttons : 0 ); }

        /** @brief 방향을 이번 프레임에만 넣습니다. */
        void tap( int32 player, uint8 direction )
        {
            InputFrame input0                            = _arrHold[0];
            InputFrame input1                            = _arrHold[1];
            ( player == 0 ? input0 : input1 )._direction = direction;
            _match.advanceFrame( input0, input1 );
        }

        const FighterRuntime& fighter( int32 player ) const { return _match.getFighter( player ); }
        bool                  isState( int32 player, FighterState state ) const { return fighter( player )._state == state; }

        int32 countEvents( FightingEvent::Kind kind )
        {
            vector<FightingEvent> listNew;
            _match.pollEvents( listNew );
            for ( const FightingEvent& event : listNew )
            {
                _listEvent.push_back( event );
            }
            int32 count = 0;
            for ( const FightingEvent& event : _listEvent )
            {
                count += event._kind == kind ? 1 : 0;
            }
            return count;
        }

        vector<uint8> saveBytes() const
        {
            vector<uint8> listByte;
            _match.saveState( listByte );
            return listByte;
        }
    };
} // namespace

/**
 * @brief [FightingRulesTest] 기술은 발생 프레임에 맞고, 경직은 맞은 다음 프레임부터 센다
 */
SW_TEST_CASE_F( FightingRulesTest, HitLandsOnStartupAndHitstunCountsFromTheNextFrame )
{
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    press( 0, kFightingButton3 ); // 하단이라 서 있는 상대가 막지 못한다. 지금 1 프레임째
    run( 1 );
    SW_EXPECT_EQUAL( kFightingMaxHealth, fighter( 1 )._health );
    run( 1 ); // 3 프레임째가 첫 지속 프레임
    SW_EXPECT_EQUAL( kFightingMaxHealth - 10, fighter( 1 )._health );
    SW_EXPECT_TRUE( isState( 1, FighterState::Hitstun ) );
    SW_EXPECT_EQUAL( 12, fighter( 1 )._stateFrames );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::Hit ) );

    run( 12 );
    SW_EXPECT_TRUE( isState( 1, FighterState::Hitstun ) );
    run( 1 );
    SW_EXPECT_TRUE( isState( 1, FighterState::Neutral ) );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::Hit ) ); // 지속 프레임이 둘이어도 한 번만 맞는다
}

/**
 * @brief [FightingRulesTest] 가만히 서 있으면 상단 · 중단을 막고, 앉으면 하단을 막고 중단을 맞으며 상단은 머리 위로 지나간다
 */
SW_TEST_CASE_F( FightingRulesTest, GuardFollowsAttackHeight )
{
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    press( 0, kFightingButton1 );
    run( 2 );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 2, fighter( 1 )._health ); // 가드 피해
    SW_EXPECT_TRUE( isState( 1, FighterState::Blockstun ) );
    SW_EXPECT_EQUAL( 6, fighter( 1 )._stateFrames );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::Blocked ) );

    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    _arrHold[1]._direction = kFightingDown;
    run( 1 );
    press( 0, kFightingButton3 );
    run( 2 );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 1, fighter( 1 )._health ); // 앉아서 하단을 막았다
    SW_EXPECT_TRUE( isState( 1, FighterState::Blockstun ) );

    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    _arrHold[1]._direction = kFightingDown;
    run( 1 );
    press( 0, kFightingButton1 );
    run( 2 );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 10, fighter( 1 )._health ); // 앉아서 중단을 맞았다

    SW_REQUIRE_TRUE( open( "special", "basic" ) );
    _arrHold[1]._direction = kFightingDown;
    run( 1 );
    press( 0, kFightingButton4 );
    run( 8 );
    SW_EXPECT_EQUAL( kFightingMaxHealth, fighter( 1 )._health ); // 상단은 앉은 머리 위로 지나간다
    SW_EXPECT_EQUAL( 0, countEvents( FightingEvent::Kind::Blocked ) );
}

/**
 * @brief [FightingRulesTest] 같은 프레임에 닿은 타격은 둘 다 들어간다. 결함: 맞으면 기술이 끊겨 1 번의 타격이 사라졌다(0 번이 늘 이겼다)
 */
SW_TEST_CASE_F( FightingRulesTest, SimultaneousStrikesTradeForBothPlayers )
{
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    pressBoth( kFightingButton1, kFightingButton1 );
    run( 2 );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 10, fighter( 0 )._health );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 10, fighter( 1 )._health );
    SW_EXPECT_TRUE( isState( 0, FighterState::Hitstun ) );
    SW_EXPECT_TRUE( isState( 1, FighterState::Hitstun ) );
    SW_EXPECT_EQUAL( 2, countEvents( FightingEvent::Kind::Hit ) );
}

/**
 * @brief [FightingRulesTest] 타격과 잡기가 함께 닿으면 어느 쪽이 0 번이든 타격이 이기고, 잡기끼리는 둘 다 헛친다. 결함: 0 번이 늘 잡았다
 */
SW_TEST_CASE_F( FightingRulesTest, StrikeBeatsThrowAndThrowsWhiffEachOther )
{
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    pressBoth( kFightingButton3, kFightingButton4 );
    run( 2 );
    SW_EXPECT_EQUAL( kFightingMaxHealth, fighter( 0 )._health );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 10, fighter( 1 )._health );
    SW_EXPECT_TRUE( isState( 1, FighterState::Hitstun ) );
    SW_EXPECT_EQUAL( 0, countEvents( FightingEvent::Kind::ThrowGrab ) );

    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    pressBoth( kFightingButton4, kFightingButton3 );
    run( 2 );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 10, fighter( 0 )._health );
    SW_EXPECT_EQUAL( kFightingMaxHealth, fighter( 1 )._health );
    SW_EXPECT_TRUE( isState( 0, FighterState::Hitstun ) );
    SW_EXPECT_EQUAL( 0, countEvents( FightingEvent::Kind::ThrowGrab ) );

    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    pressBoth( kFightingButton4, kFightingButton4 );
    run( 3 );
    SW_EXPECT_TRUE( isState( 0, FighterState::Attacking ) );
    SW_EXPECT_TRUE( isState( 1, FighterState::Attacking ) );
    SW_EXPECT_EQUAL( 0, countEvents( FightingEvent::Kind::ThrowGrab ) );
}

/**
 * @brief [FightingRulesTest] 잡기는 풀기 창이 닫힐 때 들어가고, 맞는 버튼을 첫 시도로 누르면 풀리며, 틀리면 더 풀 수 없다
 */
SW_TEST_CASE_F( FightingRulesTest, ThrowLandsUnlessTheFirstPressBreaksIt )
{
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    press( 0, kFightingButton4 );
    run( 2 );
    SW_EXPECT_TRUE( isState( 0, FighterState::Throwing ) );
    SW_EXPECT_TRUE( isState( 1, FighterState::ThrowBreak ) );
    SW_EXPECT_EQUAL( kFightingMaxHealth, fighter( 1 )._health );
    run( _settings._throwBreakFrames );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 30, fighter( 1 )._health );
    SW_EXPECT_TRUE( isState( 1, FighterState::Down ) );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::ThrowLanded ) );

    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    press( 0, kFightingButton4 );
    run( 2 );
    press( 1, kFightingButton1 );
    SW_EXPECT_TRUE( isState( 0, FighterState::Blockstun ) );
    SW_EXPECT_TRUE( isState( 1, FighterState::Blockstun ) );
    SW_EXPECT_EQUAL( kFightingMaxHealth, fighter( 1 )._health );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::ThrowBroken ) );

    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    press( 0, kFightingButton4 );
    run( 2 );
    press( 1, kFightingButton2 ); // 틀린 첫 시도
    press( 1, kFightingButton1 );
    SW_EXPECT_EQUAL( 0, countEvents( FightingEvent::Kind::ThrowBroken ) );
    run( _settings._throwBreakFrames );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 30, fighter( 1 )._health );
}

/**
 * @brief [FightingRulesTest] K.O. 는 라운드를 끝내고 대기 뒤 새 라운드가 열리며, 선승에 닿으면 대전이 끝나 프레임이 멈춘다
 */
SW_TEST_CASE_F( FightingRulesTest, KnockoutsDecideRoundsAndTheMatch )
{
    _settings._roundOverFrames = 3;
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    SW_EXPECT_EQUAL( 1, _match.getRound() );
    SW_EXPECT_EQUAL( -1, _match.getLastRoundWinner() );
    SW_EXPECT_EQUAL( -1, _match.getMatchWinner() );

    SW_REQUIRE_TRUE( _match.setFighterHealth( 1, 5 ) );
    press( 0, kFightingButton3 );
    run( 2 );
    SW_EXPECT_EQUAL( 0, fighter( 1 )._health );
    SW_EXPECT_TRUE( isState( 1, FighterState::Knockout ) );
    SW_EXPECT_EQUAL( 0, _match.getLastRoundWinner() );
    SW_EXPECT_EQUAL( 1, _match.getRoundWins( 0 ) );
    SW_EXPECT_EQUAL( 0, _match.getRoundWins( 1 ) );
    SW_EXPECT_TRUE( _match.getPhase() == RoundSeriesPhase::Intermission );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::Knockout ) );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::RoundEnd ) );

    run( 3 );
    SW_EXPECT_TRUE( _match.getPhase() == RoundSeriesPhase::RoundActive );
    SW_EXPECT_EQUAL( 2, _match.getRound() );
    SW_EXPECT_EQUAL( kFightingMaxHealth, fighter( 1 )._health );
    SW_EXPECT_TRUE( isState( 1, FighterState::Neutral ) );
    SW_EXPECT_NEAR( 0.6f, fighter( 1 )._x, kFightingTolerance );

    run( 1 );
    SW_REQUIRE_TRUE( _match.setFighterHealth( 1, 5 ) );
    press( 0, kFightingButton3 );
    run( 2 );
    SW_EXPECT_EQUAL( 0, _match.getMatchWinner() );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::MatchEnd ) );
    const int32 endFrame = _match.getFrame();
    run( 5 );
    SW_EXPECT_EQUAL( endFrame, _match.getFrame() );
}

/**
 * @brief [FightingRulesTest] 함께 쓰러지면 둘 다 1 승이고, 함께 선승에 닿으면 대전이 무승부다
 */
SW_TEST_CASE_F( FightingRulesTest, DoubleKnockoutIsADrawForBoth )
{
    _settings._roundsToWin = 1;
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    SW_REQUIRE_TRUE( _match.setFighterHealth( 0, 0 ) );
    SW_REQUIRE_TRUE( _match.setFighterHealth( 1, 0 ) );
    run( 1 );
    SW_EXPECT_EQUAL( FightingMatch::kDraw, _match.getLastRoundWinner() );
    SW_EXPECT_EQUAL( 1, _match.getRoundWins( 0 ) );
    SW_EXPECT_EQUAL( 1, _match.getRoundWins( 1 ) );
    SW_EXPECT_EQUAL( FightingMatch::kDraw, _match.getMatchWinner() );
}

/**
 * @brief [FightingRulesTest] 시간이 다 되면 체력이 많은 쪽이 이기고, 같으면 무승부 라운드다
 */
SW_TEST_CASE_F( FightingRulesTest, TimeOutComparesHealth )
{
    _settings._roundFrames = 5;
    _settings._roundsToWin = 3;
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    SW_EXPECT_EQUAL( 4, _match.getRoundFramesRemaining() );
    SW_REQUIRE_TRUE( _match.setFighterHealth( 1, 100 ) );
    run( 3 );
    SW_EXPECT_EQUAL( -1, _match.getLastRoundWinner() );
    run( 1 );
    SW_EXPECT_EQUAL( 0, _match.getLastRoundWinner() );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::TimeOut ) );

    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    run( 4 );
    SW_EXPECT_EQUAL( FightingMatch::kDraw, _match.getLastRoundWinner() );
}

/**
 * @brief [FightingRulesTest] 체력이 문턱 아래면 레이지가 켜지고 피해가 늘며, 레이지 아츠는 레이지에서만 나가고 쓰면 라운드 끝까지 다시 켜지지 않는다
 */
SW_TEST_CASE_F( FightingRulesTest, RageEntersBelowThresholdAndArtSpendsIt )
{
    SW_REQUIRE_TRUE( open( "special", "basic" ) );
    press( 0, kFightingButton1 );
    SW_EXPECT_EQUAL( 0, countEvents( FightingEvent::Kind::MoveStarted ) ); // 레이지가 아니라 나가지 않는다

    SW_REQUIRE_TRUE( _match.setFighterHealth( 0, 40 ) );
    run( 1 );
    SW_EXPECT_EQUAL( SW_TRUE, fighter( 0 )._bRage );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::RageEntered ) );

    press( 0, kFightingButton1 );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::RageArt ) );
    SW_EXPECT_EQUAL( SW_FALSE, fighter( 0 )._bRage );
    SW_EXPECT_EQUAL( SW_TRUE, fighter( 0 )._bRageUsed );
    run( 2 );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 40, fighter( 1 )._health ); // 아츠를 내며 레이지가 끝나 배율이 붙지 않는다
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::RageEntered ) );
}

/**
 * @brief [FightingRulesTest] 레이지 중에는 주는 피해에 배율이 붙는다
 */
SW_TEST_CASE_F( FightingRulesTest, RageScalesDamage )
{
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    SW_REQUIRE_TRUE( _match.setFighterHealth( 0, 40 ) );
    run( 1 );
    press( 0, kFightingButton3 );
    run( 2 );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 11, fighter( 1 )._health ); // 10 x 1.1
}

/**
 * @brief [FightingRulesTest] 히트 발동은 켠 대전에서 라운드마다 한 번이고 그동안 피해에 배율이 붙는다
 */
SW_TEST_CASE_F( FightingRulesTest, HeatBurstOnceAndScalesDamage )
{
    SW_REQUIRE_TRUE( open( "special", "basic" ) );
    press( 0, kFightingButton2 );
    SW_EXPECT_EQUAL( 0, countEvents( FightingEvent::Kind::MoveStarted ) ); // 히트를 끈 대전

    _settings._bHeatEnabled = SW_TRUE;
    SW_REQUIRE_TRUE( open( "special", "basic" ) );
    _arrHold[1]._direction = kFightingLeft; // 1 번의 앞이라 가드하지 않는다
    press( 0, kFightingButton2 );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::HeatActivated ) );
    SW_EXPECT_EQUAL( SW_TRUE, fighter( 0 )._bHeatUsed );
    SW_EXPECT_EQUAL( _settings._heatFrames, fighter( 0 )._heatFrames );
    run( 10 );
    press( 0, kFightingButton2 ); // 두 번째는 나가지 않는다
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::MoveStarted ) );
    SW_EXPECT_EQUAL( kFightingMaxHealth, fighter( 1 )._health ); // 발동 기술의 피해는 0

    press( 0, kFightingButton3 ); // 추적 기술(14 프레임)
    run( 13 );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 11, fighter( 1 )._health ); // 10 x 1.1
}

/**
 * @brief [FightingRulesTest] 기술이 끝나 고유 자세로 들어가면 자세 기술이 나가고, 시간이 지나면 자세가 풀린다. 결함: 자세 시간 0 이면 −1 이 남아 저장이 범위 밖으로 멈췄다
 */
SW_TEST_CASE_F( FightingRulesTest, StanceOpensItsMovesUntilItExpires )
{
    SW_REQUIRE_TRUE( open( "stancer", "basic" ) );
    press( 0, kFightingButton2 );
    SW_EXPECT_EQUAL( 0, countEvents( FightingEvent::Kind::MoveStarted ) ); // 자세가 아니라 나가지 않는다

    press( 0, kFightingButton1 );
    run( 10 );
    SW_EXPECT_EQUAL( 0, fighter( 0 )._stanceIndex );
    SW_EXPECT_TRUE( fighter( 0 )._posture == FighterPosture::Stance );
    press( 0, kFightingButton2 );
    SW_EXPECT_EQUAL( 2, countEvents( FightingEvent::Kind::MoveStarted ) );

    SW_REQUIRE_TRUE( open( "stancer", "basic" ) );
    press( 0, kFightingButton1 );
    run( 10 + _settings._stanceFrames );
    SW_EXPECT_EQUAL( -1, fighter( 0 )._stanceIndex );
    SW_EXPECT_TRUE( fighter( 0 )._posture == FighterPosture::Standing );

    _settings._stanceFrames = 0;
    SW_REQUIRE_TRUE( open( "stancer", "basic" ) );
    press( 0, kFightingButton1 );
    run( 11 );
    SW_EXPECT_EQUAL( -1, fighter( 0 )._stanceIndex );
    SW_EXPECT_EQUAL( 0, fighter( 0 )._stanceFrames );
    SW_EXPECT_FALSE( saveBytes().empty() );
}

/**
 * @brief [FightingRulesTest] 횡이동은 직선 기술을 빗나가게 하고, 추적 기술은 따라가 맞힌다
 */
SW_TEST_CASE_F( FightingRulesTest, SidestepEvadesLinearMovesButNotTracking )
{
    SW_REQUIRE_TRUE( open( "ender", "basic" ) );
    press( 0, kFightingButton4 ); // 직선, 14 프레임 발생
    tap( 1, kFightingUp );
    SW_EXPECT_TRUE( isState( 1, FighterState::Sidestep ) );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::Sidestep ) );
    run( 18 );
    SW_EXPECT_EQUAL( kFightingMaxHealth, fighter( 1 )._health );
    SW_EXPECT_TRUE( isState( 1, FighterState::Neutral ) );
    SW_EXPECT_TRUE( fighter( 1 )._z > 0.1f ); // 상대를 축으로 돌았다

    SW_REQUIRE_TRUE( open( "special", "basic" ) );
    press( 0, kFightingButton3 ); // 추적
    tap( 1, kFightingUp );
    run( 14 );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 10, fighter( 1 )._health );
}

/**
 * @brief [FightingRulesTest] 점프는 뜨고 내려와 중립으로 돌아온다
 */
SW_TEST_CASE_F( FightingRulesTest, JumpArcsAndLands )
{
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    tap( 0, kFightingUpForward );
    SW_EXPECT_TRUE( isState( 0, FighterState::Jump ) );
    SW_EXPECT_EQUAL( SW_TRUE, fighter( 0 )._bAirborne );
    run( 20 );
    SW_EXPECT_NEAR( 1.0f, fighter( 0 )._y, 0.1f ); // 40 프레임의 절반에 높이 1(이산 적분이라 조금 넘는다)
    run( 25 );
    SW_EXPECT_TRUE( isState( 0, FighterState::Neutral ) );
    SW_EXPECT_NEAR( 0.0f, fighter( 0 )._y, kFightingTolerance );
    SW_EXPECT_EQUAL( SW_FALSE, fighter( 0 )._bAirborne );
}

/**
 * @brief [FightingRulesTest] 띄운 상대의 추가타는 감쇠되고, 떨어지면 다운 · 기상을 거쳐 콤보가 비워진다
 */
SW_TEST_CASE_F( FightingRulesTest, JuggleScalesDamageThenDownAndWakeup )
{
    _settings._downFrames   = 4;
    _settings._wakeupFrames = 4;
    SW_REQUIRE_TRUE( open( "combo", "basic" ) );
    press( 0, kFightingButton1 );
    run( 2 );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 20, fighter( 1 )._health );
    SW_EXPECT_TRUE( isState( 1, FighterState::Juggle ) );
    SW_EXPECT_EQUAL( 1, fighter( 1 )._airHits );

    run( 7 ); // 띄우기가 끝날 때까지
    press( 0, kFightingButton2 );
    run( 2 );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 20 - 9, fighter( 1 )._health ); // 10 x 0.85 를 반올림
    SW_EXPECT_EQUAL( 2, fighter( 1 )._airHits );
    SW_EXPECT_EQUAL( 2, fighter( 1 )._comboHits );

    int32 frames = 0;
    while ( isState( 1, FighterState::Juggle ) && frames < 120 )
    {
        run( 1 );
        ++frames;
    }
    SW_EXPECT_TRUE( isState( 1, FighterState::Down ) );
    SW_EXPECT_NEAR( 0.0f, fighter( 1 )._y, kFightingTolerance );
    run( 4 );
    SW_EXPECT_TRUE( isState( 1, FighterState::Wakeup ) );
    run( 4 );
    SW_EXPECT_TRUE( isState( 1, FighterState::Neutral ) );
    SW_EXPECT_EQUAL( 0, fighter( 1 )._airHits );
    SW_EXPECT_EQUAL( 0, fighter( 1 )._comboHits );
}

/**
 * @brief [FightingRulesTest] 스크류 · 바운드는 공중 상대를 다시 띄우고 쓴 표시를 남긴다
 */
SW_TEST_CASE_F( FightingRulesTest, ScrewAndBoundRelaunchTheAirborneTarget )
{
    SW_REQUIRE_TRUE( open( "combo", "basic" ) );
    press( 0, kFightingButton1 );
    run( 9 );
    press( 0, kFightingButton3 );
    run( 2 );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::Screw ) );
    SW_EXPECT_EQUAL( SW_TRUE, fighter( 1 )._bScrewUsed );
    SW_EXPECT_NEAR( _settings._screwVelocity, fighter( 1 )._velocityY, kFightingTolerance );

    SW_REQUIRE_TRUE( open( "combo", "basic" ) );
    press( 0, kFightingButton1 );
    run( 9 );
    press( 0, kFightingButton4 );
    run( 2 );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::Bound ) );
    SW_EXPECT_EQUAL( SW_TRUE, fighter( 1 )._bBoundUsed );
}

/**
 * @brief [FightingRulesTest] 벽 근처에서만 벽꽝이 되고 벽꽝 → 다운 → 기상 → 중립으로 간다. 벽에서 멀면 그냥 경직이다
 */
SW_TEST_CASE_F( FightingRulesTest, WallSplatOnlyNearTheWall )
{
    SW_REQUIRE_TRUE( open( "ender", "basic" ) );
    press( 0, kFightingButton3 );
    run( 2 );
    SW_EXPECT_TRUE( isState( 1, FighterState::Hitstun ) );
    SW_EXPECT_EQUAL( 0, countEvents( FightingEvent::Kind::WallSplat ) );

    _settings._wallSplatFrames = 3;
    _settings._downFrames      = 3;
    _settings._wakeupFrames    = 3;
    SW_REQUIRE_TRUE( open( "ender", "basic" ) );
    SW_EXPECT_FALSE( _match.isNearWall( 1 ) );
    _match.setFighterPosition( 1, 100.0f, 0.0f ); // 벽 안으로 잘린다
    SW_EXPECT_NEAR( _settings._stageHalfSize - def( "basic" )._pushRadius, fighter( 1 )._x, kFightingTolerance );
    _match.setFighterPosition( 0, 3.5f, 0.0f );
    SW_EXPECT_TRUE( _match.isNearWall( 1 ) );
    press( 0, kFightingButton3 );
    run( 2 );
    SW_EXPECT_TRUE( isState( 1, FighterState::WallSplat ) );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::WallSplat ) );
    run( 3 );
    SW_EXPECT_TRUE( isState( 1, FighterState::Down ) );
    run( 3 );
    SW_EXPECT_TRUE( isState( 1, FighterState::Wakeup ) );
    run( 3 );
    SW_EXPECT_TRUE( isState( 1, FighterState::Neutral ) );
    SW_EXPECT_EQUAL( SW_FALSE, fighter( 1 )._bWallSplatUsed );
}

/**
 * @brief [FightingRulesTest] 누운 상대는 다운 타격 기술만 맞고, 그 기술은 상대가 누웠을 때만 나간다
 */
SW_TEST_CASE_F( FightingRulesTest, GroundHitOnlyWhileTheTargetIsDown )
{
    SW_REQUIRE_TRUE( open( "ender", "basic" ) );
    press( 0, kFightingButton2 );
    SW_EXPECT_EQUAL( 0, countEvents( FightingEvent::Kind::MoveStarted ) );

    press( 0, kFightingButton1 );
    run( 2 );
    SW_EXPECT_TRUE( isState( 1, FighterState::Down ) );
    const int32 healthDown = fighter( 1 )._health;
    run( 7 );
    press( 0, kFightingButton2 );
    run( 2 );
    SW_EXPECT_EQUAL( healthDown - 5, fighter( 1 )._health );
}

/**
 * @brief [FightingRulesTest] 스트링 기술은 앞 기술의 캔슬 창에서만 나간다
 */
SW_TEST_CASE_F( FightingRulesTest, StringMoveOnlyInTheCancelWindow )
{
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    press( 0, kFightingButton2 );
    SW_EXPECT_EQUAL( 0, countEvents( FightingEvent::Kind::MoveStarted ) );

    press( 0, kFightingButton1 );
    run( 3 );
    press( 0, kFightingButton2 ); // 5 프레임째
    SW_EXPECT_EQUAL( 2, countEvents( FightingEvent::Kind::MoveStarted ) );
    SW_EXPECT_EQUAL( 1, fighter( 0 )._moveIndex );
}

/**
 * @brief [FightingRulesTest] 움직일 수 없을 때 넣은 커맨드는 선입력 시간 안에 풀리면 나가고, 넘기면 버린다
 */
SW_TEST_CASE_F( FightingRulesTest, BufferedMoveFiresOnlyInsideTheBufferWindow )
{
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    press( 0, kFightingButton3 );
    run( 6 );
    press( 0, kFightingButton3 ); // 8 프레임째, 후딜 중
    SW_EXPECT_EQUAL( 2, fighter( 0 )._moveIndex );
    SW_EXPECT_TRUE( fighter( 0 )._bufferedMove >= 0 );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::MoveStarted ) );
    run( 3 );
    SW_EXPECT_EQUAL( 2, countEvents( FightingEvent::Kind::MoveStarted ) );

    _settings._inputBufferFrames = 1;
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    press( 0, kFightingButton3 );
    run( 6 );
    press( 0, kFightingButton3 );
    run( 3 );
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::MoveStarted ) );
    SW_EXPECT_EQUAL( -1, fighter( 0 )._bufferedMove );
}

/**
 * @brief [FightingRulesTest] 입력 1 바이트는 왕복하고 범위 밖은 중립이며, 모자란 입력은 중립으로 읽는다. initialize 전에는 프레임이 돌지 않는다
 */
SW_TEST_CASE_F( FightingRulesTest, InputByteRoundTripsAndMissingPlayerIsNeutral )
{
    InputFrame frame;
    frame._buttons           = 0x1F; // 다섯째 버튼은 1 바이트에 담지 않는다
    frame._direction         = 9;
    const InputFrame decoded = FightingMatch::decodeInput( FightingMatch::encodeInput( frame ) );
    SW_EXPECT_EQUAL( 0x0F, static_cast<int32>( decoded._buttons ) );
    SW_EXPECT_EQUAL( 9, static_cast<int32>( decoded._direction ) );
    frame._direction = 0;
    SW_EXPECT_EQUAL( 5, static_cast<int32>( FightingMatch::decodeInput( FightingMatch::encodeInput( frame ) )._direction ) );
    SW_EXPECT_EQUAL( 5, static_cast<int32>( FightingMatch::decodeInput( 0xF0 )._direction ) ); // 깨진 바이트

    FightingMatch empty;
    empty.advanceFrame( vector<uint8>{} );
    SW_EXPECT_EQUAL( 0, empty.getFrame() );
    SW_EXPECT_EQUAL( 0, empty.getRound() );

    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    run( 1 );
    InputFrame pressLow;
    pressLow._buttons = kFightingButton3;
    _match.advanceFrame( vector<uint8>{ FightingMatch::encodeInput( pressLow ) } ); // 1 번의 입력이 없다
    SW_EXPECT_EQUAL( 1, countEvents( FightingEvent::Kind::MoveStarted ) );
    SW_EXPECT_TRUE( isState( 1, FighterState::Neutral ) );
}

/**
 * @brief [FightingRulesTest] 범위 밖 설정 · 범위 밖 체력은 오류로 거절하고 위치는 벽 안으로 자른다. 없는 사람은 무시한다
 */
SW_TEST_CASE_F( FightingRulesTest, BrokenSettingsAndHealthAreRefused )
{
    SW_EXPECT_LOG( "FightingMatch.FightingMatchRefusedSettings" );
    SW_EXPECT_LOG( "FightingMatch.FighterHealthOutOfRange" );
    FightingSettings broken;
    broken._roundsToWin = 0;
    SW_EXPECT_FALSE( _match.initialize( def( "basic" ), def( "basic" ), broken ) );

    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    SW_EXPECT_FALSE( _match.setFighterHealth( 0, -1 ) );
    SW_EXPECT_FALSE( _match.setFighterHealth( 0, kFightingMaxHealth + 1 ) );
    SW_EXPECT_FALSE( _match.setFighterHealth( 2, 1 ) );
    SW_EXPECT_EQUAL( kFightingMaxHealth, fighter( 0 )._health );
    const float32 before = fighter( 0 )._x;
    _match.setFighterPosition( 5, 1.0f, 1.0f );
    SW_EXPECT_NEAR( before, fighter( 0 )._x, kFightingTolerance );
}

/**
 * @brief [FightingRulesTest] 롤백 바이트로 되돌려 같은 입력을 다시 돌리면 같은 바이트가 나온다
 */
SW_TEST_CASE_F( FightingRulesTest, RollbackReplaysToTheSameBytes )
{
    SW_REQUIRE_TRUE( open( "combo", "basic" ) );
    press( 0, kFightingButton1 );
    run( 1 ); // 판정 전, 타임라인 재생 중
    const vector<uint8> listSaved = saveBytes();
    SW_REQUIRE_TRUE( listSaved.empty() == false );

    const auto replay = [&]()
    {
        run( 8 );
        press( 0, kFightingButton2 );
        run( 6 );
        press( 1, kFightingButton3 );
        run( 4 );
    };
    replay();
    const vector<uint8> listFirst = saveBytes();
    SW_EXPECT_EQUAL( kFightingMaxHealth - 29, fighter( 1 )._health );

    SW_REQUIRE_TRUE( _match.loadState( listSaved ) );
    SW_EXPECT_EQUAL( kFightingMaxHealth, fighter( 1 )._health );
    SW_EXPECT_TRUE( fighter( 0 )._timeline.isPlaying() );
    replay();
    SW_EXPECT_TRUE( saveBytes() == listFirst );
}

/**
 * @brief [FightingRulesTest] 잘리거나 남거나 다른 캐릭터의 롤백 바이트는 거절하고 상태를 바꾸지 않는다
 */
SW_TEST_CASE_F( FightingRulesTest, RollbackRejectsBrokenBytes )
{
    SW_EXPECT_LOG( "FightingMatch.FightingRollbackStateRejected" );
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    run( 3 );
    const vector<uint8> listSaved = saveBytes();

    SW_REQUIRE_TRUE( open( "combo", "basic" ) );
    const int32 frame = _match.getFrame();
    SW_EXPECT_FALSE( _match.loadState( listSaved ) ); // 다른 캐릭터
    SW_EXPECT_EQUAL( frame, _match.getFrame() );

    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    vector<uint8> listCut( listSaved.begin(), listSaved.begin() + static_cast<ptrdiff_t>( listSaved.size() / 2 ) );
    SW_EXPECT_FALSE( _match.loadState( listCut ) );
    vector<uint8> listLong = listSaved;
    listLong.push_back( 0 );
    listLong.push_back( 0 );
    SW_EXPECT_FALSE( _match.loadState( listLong ) );
    SW_EXPECT_EQUAL( frame, _match.getFrame() );
    SW_EXPECT_TRUE( _match.loadState( listSaved ) );
    SW_EXPECT_EQUAL( frame + 3, _match.getFrame() );
}

/**
 * @brief [FightingRulesTest] 세이브 왕복은 같은 상태를 만들고, 이어 돌리면 같은 바이트가 나온다
 */
SW_TEST_CASE_F( FightingRulesTest, SaveRoundTripContinuesIdentically )
{
    SW_REQUIRE_TRUE( open( "combo", "basic" ) );
    press( 0, kFightingButton1 );
    run( 4 );
    SW_REQUIRE_TRUE( _match.setFighterHealth( 0, 100 ) );

    Archive written;
    _match.writeState( written );
    SW_REQUIRE_TRUE( written.isOk() );

    FightingMatch copy;
    SW_REQUIRE_TRUE( copy.initialize( def( "combo" ), def( "basic" ), _settings ) );
    Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( copy.readState( reading ) );
    SW_EXPECT_EQUAL( _match.getFrame(), copy.getFrame() );
    SW_EXPECT_EQUAL( 100, copy.getFighter( 0 )._health );
    SW_EXPECT_EQUAL( _match.getFighter( 1 )._health, copy.getFighter( 1 )._health );

    vector<uint8> listSavedMain;
    vector<uint8> listSavedCopy;
    _match.saveState( listSavedMain );
    copy.saveState( listSavedCopy );
    SW_EXPECT_TRUE( listSavedMain == listSavedCopy );

    run( 5 );
    for ( int32 frame = 0; frame < 5; ++frame )
    {
        copy.advanceFrame( InputFrame{}, InputFrame{} );
    }
    _match.saveState( listSavedMain );
    copy.saveState( listSavedCopy );
    SW_EXPECT_TRUE( listSavedMain == listSavedCopy );
}

/**
 * @brief [FightingRulesTest] 세이브가 잘렸거나 정의와 맞지 않으면(다른 캐릭터 · 없는 기술 번호 · 최대를 넘는 체력) 거절하고 상태를 바꾸지 않는다. 결함: 최대를 넘는 체력을 받았다
 */
SW_TEST_CASE_F( FightingRulesTest, SaveRejectsBytesThatDoNotFitTheDefinition )
{
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    press( 0, kFightingButton3 ); // 정의의 2 번 기술을 재생 중이다
    Archive written;
    _match.writeState( written );
    SW_REQUIRE_TRUE( written.isOk() );

    const auto readInto = [&]( const FighterDef& def0, const FighterDef& def1 ) -> bool
    {
        FightingMatch target;
        if ( target.initialize( def0, def1, _settings ) == false )
            return false;
        const int32 frame = target.getFrame();
        Archive     reading( written.getData(), written.getSize() );
        const bool  bRead = target.readState( reading );
        SW_EXPECT_EQUAL( bRead ? _match.getFrame() : frame, target.getFrame() );
        return bRead;
    };
    SW_EXPECT_TRUE( readInto( def( "basic" ), def( "basic" ) ) );
    SW_EXPECT_FALSE( readInto( def( "combo" ), def( "basic" ) ) );

    FighterDef weak = def( "basic" );
    weak._health    = kFightingMaxHealth - 1;
    SW_EXPECT_FALSE( readInto( weak, def( "basic" ) ) );

    FighterDef shortList = def( "basic" );
    shortList._listMove.resize( 1 );
    SW_EXPECT_FALSE( readInto( shortList, def( "basic" ) ) );

    FightingMatch target;
    SW_REQUIRE_TRUE( target.initialize( def( "basic" ), def( "basic" ), _settings ) );
    Archive cut( written.getData(), written.getSize() / 2 );
    SW_EXPECT_FALSE( target.readState( cut ) );
    SW_EXPECT_EQUAL( 0, target.getFrame() );

    FightingMatch bare; // initialize 전이라 정의가 없다
    Archive       reading( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( bare.readState( reading ) );
}

/**
 * @brief [FightingRulesTest] 잡기 중인 상태도 두 저장 형식을 왕복한다. `validateState` 가 잡힌 쪽에 잡은 쪽을 요구하므로 진짜 잡기가 거절되지 않는지 본다
 */
SW_TEST_CASE_F( FightingRulesTest, ThrowInProgressSurvivesBothSaves )
{
    SW_REQUIRE_TRUE( open( "basic", "basic" ) );
    press( 0, kFightingButton4 );
    run( 2 );
    SW_REQUIRE_TRUE( isState( 1, FighterState::ThrowBreak ) );

    // 잡은 상태 그대로는 왕복한다. 롤백은 프레임마다 쓰므로 잡기 중에도 읽혀야 한다.
    const vector<uint8> listSaved = saveBytes();
    SW_EXPECT_TRUE( _match.loadState( listSaved ) );
    Archive written;
    _match.writeState( written );
    Archive reading( written.getData(), written.getSize() );
    SW_EXPECT_TRUE( _match.readState( reading ) );
    SW_EXPECT_TRUE( isState( 0, FighterState::Throwing ) );
    run( _settings._throwBreakFrames );
    SW_EXPECT_EQUAL( kFightingMaxHealth - 30, fighter( 1 )._health ); // 되돌린 뒤에도 잡기가 들어간다
}

/**
 * @brief [FightingTest] 카탈로그는 칸 · 커맨드 · 자세 · 다른 id 의 프레임 데이터를 읽고 고유 자세 목록을 모은다
 */
SW_TEST_CASE( FightingTest, CatalogReadsFieldsAndCollectsStances )
{
    MoveCatalog moves;
    SW_REQUIRE_TRUE( moves.loadFromXMLText( kFightingMoveXML, "FightingTest" ) );
    FighterCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<FighterCatalog buttons="LP,RP">
  <Fighter id="kaz" name="Kaz" health="120" walkSpeed="0.05" hurtRadius="0.4" pushRadius="0.2" standHeight="2" crouchHeight="1" sidestepAngle="0.5" sidestepFrames="8" sidestep="u" sidestepDown="d,n" jump="u/f" jumpFrames="20" jumpHeight="2" jumpDistance="1">
    <Move id="one" frames="jab" command="LP" priority="3" maxGap="5" from="Standing,Crouching" pushback="0.5" tracking="true"/>
    <Move id="grab" command="RP" breakButtons="LP+RP"/>
    <Move id="pose" frames="jab" command="LP" stance="crane" enterStance="tiger"/>
  </Fighter>
</FighterCatalog>)",
                                              moves, "FightingTest" ) );
    const FighterDef* pDef = catalog.find( hashed_string( "kaz" ) );
    SW_REQUIRE_NOT_NULL( pDef );
    SW_EXPECT_EQUAL( 120, pDef->_health );
    SW_EXPECT_EQUAL( 8, pDef->_sidestepFrames );
    SW_EXPECT_NEAR( 0.05f, pDef->_walkSpeed, kFightingTolerance );
    SW_REQUIRE_EQUAL( size_t( 3 ), pDef->_listMove.size() );

    const FighterMove& one = pDef->_listMove[0];
    SW_EXPECT_TRUE( one._frame._id == hashed_string( "one" ) ); // 기술 id 는 자기 이름이고 수치는 jab 의 것이다
    SW_EXPECT_EQUAL( 3, one._frame._startup );
    SW_EXPECT_EQUAL( 3, one._command._priority );
    SW_EXPECT_EQUAL( 5, one._command._maxGap );
    SW_EXPECT_TRUE( one.allowsPosture( FighterPosture::Standing ) );
    SW_EXPECT_TRUE( one.allowsPosture( FighterPosture::Crouching ) );
    SW_EXPECT_FALSE( one.allowsPosture( FighterPosture::Airborne ) );
    SW_EXPECT_TRUE( one._bTracking );
    SW_EXPECT_NEAR( 0.5f, one._pushback, kFightingTolerance );

    SW_EXPECT_EQUAL( 3, static_cast<int32>( pDef->_listMove[1]._breakButtons ) ); // LP + RP
    SW_EXPECT_TRUE( pDef->_listMove[2].allowsPosture( FighterPosture::Stance ) );
    SW_EXPECT_FALSE( pDef->_listMove[2].allowsPosture( FighterPosture::Standing ) );

    SW_EXPECT_EQUAL( 1, pDef->findMoveIndex( hashed_string( "grab" ) ).value() );
    SW_EXPECT_FALSE( pDef->findMoveIndex( hashed_string( "none" ) ).has_value() );
    SW_EXPECT_EQUAL( 0, pDef->findStanceIndex( hashed_string( "crane" ) ).value() );
    SW_EXPECT_EQUAL( 1, pDef->findStanceIndex( hashed_string( "tiger" ) ).value() );
    SW_EXPECT_FALSE( pDef->findStanceIndex( hashed_string( "none" ) ).has_value() );
}

/**
 * @brief [FightingTest] 틀린 기술은 그 기술만 버리고 오류를 남긴다
 */
SW_TEST_CASE( FightingTest, CatalogDropsOnlyTheBrokenMoves )
{
    SW_EXPECT_LOG( "FighterCatalog.MoveNoFrameData" );
    SW_EXPECT_LOG( "FighterCatalog.MoveUnknownPostureStanding" );
    SW_EXPECT_LOG( "FighterCatalog.IdNeedsReadableCommand" );
    SW_EXPECT_LOG( "FighterCatalog.IdNeedsIntegerMore" );
    SW_EXPECT_LOG( "FighterCatalog.MoveUnreadableBreakButtonsOne" );
    SW_EXPECT_LOG( "FighterCatalog.FighterMoveNotRead" );
    MoveCatalog moves;
    SW_REQUIRE_TRUE( moves.loadFromXMLText( kFightingMoveXML, "FightingTest" ) );
    FighterCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<FighterCatalog>
  <Fighter id="kaz" name="Kaz" sidestep="u" sidestepDown="d,n" jump="u/f">
    <Move id="good" frames="jab" command="1"/>
    <Move id="noframes" command="1"/>
    <Move id="badposture" frames="jab" command="1" from="Flying"/>
    <Move id="badcommand" frames="jab" command="zz"/>
    <Move id="badpriority" frames="jab" command="1" priority="-1"/>
    <Move id="badbreak" frames="jab" command="1" breakButtons="1,2"/>
  </Fighter>
</FighterCatalog>)",
                                              moves, "FightingTest" ) );
    const FighterDef* pDef = catalog.find( hashed_string( "kaz" ) );
    SW_REQUIRE_NOT_NULL( pDef );
    SW_REQUIRE_EQUAL( size_t( 1 ), pDef->_listMove.size() );
    SW_EXPECT_EQUAL( 0, pDef->findMoveIndex( hashed_string( "good" ) ).value() );
}

/**
 * @brief [FightingTest] 커맨드가 빠진 캐릭터 · 범위 밖 칸의 캐릭터는 넣지 않고, 기술 없는 캐릭터는 경고만 남긴다
 */
SW_TEST_CASE( FightingTest, CatalogRefusesBrokenFighters )
{
    SW_EXPECT_LOG( "FighterCatalog.IdNeedsReadableCommand" );
    SW_EXPECT_LOG( "FighterCatalog.FighterIdErrorsNot" );
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    SW_EXPECT_LOG( "FighterCatalog.FighterNoUsableMove" );
    MoveCatalog moves;
    SW_REQUIRE_TRUE( moves.loadFromXMLText( kFightingMoveXML, "FightingTest" ) );

    FighterCatalog noJump;
    SW_EXPECT_FALSE( noJump.loadFromXMLText( R"(<FighterCatalog><Fighter id="a" name="A" sidestep="u" sidestepDown="d,n"/></FighterCatalog>)", moves, "FightingTest" ) );
    SW_EXPECT_TRUE( noJump.isEmpty() );

    FighterCatalog badHealth;
    SW_EXPECT_FALSE( badHealth.loadFromXMLText( R"(<FighterCatalog><Fighter id="a" name="A" health="0" sidestep="u" sidestepDown="d,n" jump="u/f"/></FighterCatalog>)", moves, "FightingTest" ) );
    SW_EXPECT_TRUE( badHealth.isEmpty() );

    FighterCatalog noMoves;
    SW_EXPECT_TRUE( noMoves.loadFromXMLText( R"(<FighterCatalog><Fighter id="a" name="A" sidestep="u" sidestepDown="d,n" jump="u/f"/></FighterCatalog>)", moves, "FightingTest" ) );
    SW_EXPECT_NOT_NULL( noMoves.find( hashed_string( "a" ) ) );
}

/**
 * @brief [FightingTest] 겹친 캐릭터 id 는 오류로 버리고 앞 정의를 지킨다. 결함: 말없이 앞 정의를 덮어썼다
 */
SW_TEST_CASE( FightingTest, CatalogRefusesDuplicateFighterIDs )
{
    SW_EXPECT_LOG( "GameCatalog.DuplicateID" );
    MoveCatalog moves;
    SW_REQUIRE_TRUE( moves.loadFromXMLText( kFightingMoveXML, "FightingTest" ) );
    FighterCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<FighterCatalog>
  <Fighter id="a" name="First" health="100" sidestep="u" sidestepDown="d,n" jump="u/f"><Move id="jab" command="1"/></Fighter>
  <Fighter id="a" name="Second" health="50" sidestep="u" sidestepDown="d,n" jump="u/f"><Move id="jab" command="1"/></Fighter>
</FighterCatalog>)",
                                              moves, "FightingTest" ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), catalog.getCount() );
    SW_EXPECT_EQUAL( 100, catalog.find( hashed_string( "a" ) )->_health );
}
