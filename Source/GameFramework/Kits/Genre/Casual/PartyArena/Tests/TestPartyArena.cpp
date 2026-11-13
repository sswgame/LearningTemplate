#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Casual/PartyArena/PartyItemSpawner.h"
#include "GameFramework/Kits/Genre/Casual/PartyArena/PartyRoundSeries.h"
#include "GameFramework/Kits/Genre/Casual/PartyArena/TrampolineArena.h"

#include <cstring>
#include <limits>

// 파티 아레나 키트의 규칙: 튕김 타이밍과 콤보 · 돌진 · 내려찍기 · 링 아웃 점수 · 부활 · 아이템 효과 · 입력 비트 · 상태 왕복, 아이템 스포너, 라운드 시리즈.
// 결함: 내려찍기가 방패를 뚫었다 · `start` 전에도 몸이 움직이고 아이템이 나왔다 · 사람 수 거절에 오류가 없었다 · 땅에 닿아도 돌진 시간이 남아 튀어 오른 뒤 중력 없이 떠올랐다 ·
// Heavy 의 시간 0 이하 · 음수 배율을 받았다 · 모르는 효과를 말없이 무시했다 · 겹친 아이템 id 가 앞 정의를 덮어썼다 · 시리즈 XML 이 앞 칸만 바꾼 채 실패하고 다시 읽으면 라운드가 덧붙었다 ·
// 상태 읽기가 최대를 넘는 콤보 · 음수 배율 · 음수 시각 · 경기와 어긋난 탈락 상태와 스포너의 수 초과 · 음수 나이 · 범위 밖 번호를 받았다. 키트 GF_PartyArena(nogpu).

using namespace sw;

namespace
{
    constexpr float32 kPartyStep      = 0.02f;
    constexpr float32 kPartyTolerance = 1.0e-3f;

    // 상한 1000 초 간격이라 둘째 아이템은 테스트 안에 나오지 않는다.
    constexpr const utf8* kPartyShieldXML = R"(<PartyItems minInterval="0" maxInterval="1000" lifetime="0" height="0" maxActive="1"><Item id="shield" effect="Shield"/></PartyItems>)";

    // `writePlayer` 의 배치(사람 머리부터의 바이트). 꾸민 바이트를 쓰려고 적는다.
    constexpr size_t kPartyFirstPlayer     = 4; ///< 사람 수(uint32) 뒤
    constexpr size_t kPartyFieldPosition   = 0;
    constexpr size_t kPartyFieldInputFlag  = 44;
    constexpr size_t kPartyFieldTaken      = 83;
    constexpr size_t kPartyFieldDealt      = 87;
    constexpr size_t kPartyFieldCombo      = 91;
    constexpr size_t kPartyFieldLastHitter = 95;
    constexpr size_t kPartyFieldFlag       = 99;
    constexpr size_t kPartyFieldState      = 103;

    /** @brief 2 x 2 m 무대의 아레나입니다. 둘은 x = ±1 위 높이 3 에서 시작합니다. */
    struct PartyArenaRulesTest
    {
        TrampolineSettings      _settings{};
        TrampolineArena         _arena{};
        vector<TrampolineEvent> _listEvent{};

        PartyArenaRulesTest()
        {
            _settings._step              = kPartyStep;
            _settings._arenaRadius       = 2.0f;
            _settings._spawnInvulnerable = 0.0f;
            _settings._respawnDelay      = 0.5f;
            _settings._attackDashSpeed   = 3.0f;
        }

        [[nodiscard]] bool open( float32 roundTime = 0.0f, int32 scoreLimit = 0 )
        {
            if ( _arena.initialize( _settings, 2, roundTime, scoreLimit, 5u ) == false )
                return false;
            _arena.start();
            _listEvent.clear();
            return true;
        }

        const TrampolinePlayer& body( int32 player ) const { return *_arena.findPlayer( player ); }

        void step( int32 stepCount = 1 )
        {
            for ( int32 index = 0; index < stepCount; ++index )
            {
                _arena.step();
            }
        }

        void collect()
        {
            vector<TrampolineEvent> listNew;
            _arena.pollEvents( listNew );
            for ( const TrampolineEvent& event : listNew )
            {
                _listEvent.push_back( event );
            }
        }

        int32 countEvents( TrampolineEvent::Kind kind, int32 player )
        {
            collect();
            int32 count = 0;
            for ( const TrampolineEvent& event : _listEvent )
            {
                count += event._kind == kind && event._player == player ? 1 : 0;
            }
            return count;
        }

        /** @brief 그 사람의 마지막 @p kind 이벤트입니다. 없으면 빈 이벤트입니다. */
        TrampolineEvent findLastEvent( TrampolineEvent::Kind kind, int32 player )
        {
            collect();
            TrampolineEvent found;
            for ( const TrampolineEvent& event : _listEvent )
            {
                if ( event._kind == kind && event._player == player )
                    found = event;
            }
            return found;
        }

        bool stepUntilState( int32 player, TrampolinePlayerState state, int32 maxSteps = 200 )
        {
            for ( int32 index = 0; index < maxSteps && body( player )._state != state; ++index )
            {
                step();
            }
            return body( player )._state == state;
        }

        bool stepUntilEvent( TrampolineEvent::Kind kind, int32 player, int32 maxSteps = 200 )
        {
            for ( int32 index = 0; index < maxSteps && countEvents( kind, player ) == 0; ++index )
            {
                step();
            }
            return countEvents( kind, player ) > 0;
        }

        void pressJump( int32 player )
        {
            TrampolineInput input;
            input._bJumpPressed = SW_TRUE;
            _arena.setInput( player, input );
        }

        /** @brief 땅에 닿아 튀어 오를 때까지 진행합니다. @p bPress 면 닿은 다음 걸음에 점프를 누릅니다(완벽 판정). */
        bool bounceOnce( int32 player, bool bPress )
        {
            if ( stepUntilState( player, TrampolinePlayerState::Contact ) == false )
                return false;
            if ( bPress )
                pressJump( player );
            return stepUntilState( player, TrampolinePlayerState::Air );
        }

        /** @brief 0 번이 1 번 쪽으로 돌진해 맞힙니다. */
        bool dashIntoOther()
        {
            TrampolineInput input;
            input._moveX          = -1.0f;
            input._bAttackPressed = SW_TRUE;
            _arena.setInput( 0, input );
            step();
            input._bAttackPressed = SW_FALSE;
            input._moveX          = 0.0f;
            _arena.setInput( 0, input );
            return stepUntilEvent( TrampolineEvent::Kind::Hit, 1, 30 );
        }

        vector<uint8> writeBytes( const TrampolineArena& arena ) const
        {
            Archive written;
            arena.writeState( written );
            vector<uint8> listByte;
            written.writeData( listByte );
            return listByte;
        }

        bool readInFreshArena( const vector<uint8>& listByte )
        {
            TrampolineArena target;
            if ( target.initialize( _settings, 2, 0.0f, 0, 5u ) == false )
                return false;
            Archive reading( listByte.data(), listByte.size() );
            return target.readState( reading );
        }

        template <typename T>
        bool readPatched( size_t offset, T value )
        {
            vector<uint8> listByte = writeBytes( _arena );
            std::memcpy( listByte.data() + offset, &value, sizeof( T ) );
            return readInFreshArena( listByte );
        }
    };

    /** @brief 스포너를 손으로 쓴 상태 바이트로 읽어 봅니다. */
    struct PartyArenaTestUtil
    {
        static PartyItemSpawnSettings makeSettings()
        {
            PartyItemSpawnSettings settings;
            settings._minInterval = 1.0f;
            settings._maxInterval = 1.0f;
            settings._lifetime    = 2.0f;
            settings._spawnRadius = 5.0f;
            settings._height      = 1.5f;
            settings._maxActive   = 2;
            return settings;
        }

        static PartyItemDef makeItem( const utf8* pID, float32 weight )
        {
            PartyItemDef def;
            def._id     = hashed_string( pID );
            def._effect = hashed_string( "Effect" );
            def._weight = weight;
            return def;
        }

        static bool readFixture( PartyItemSpawner& spawner, const utf8* pItemID, float32 age, int32 serial, int32 nextSerial, uint32 count = 1 )
        {
            GameRandom random;
            random.setSeed( 3 );
            Archive written;
            written << count;
            for ( uint32 index = 0; index < count; ++index )
            {
                written << float3{};
                StateIO::writeName( written, hashed_string( pItemID ) );
                written << age;
                written << serial + static_cast<int32>( index );
            }
            StateIO::writeRandom( written, random );
            StateIO::writeCountdown( written, Countdown{} );
            written << nextSerial;
            Archive reading( written.getData(), written.getSize() );
            return spawner.readState( reading );
        }

        static int32 countEvents( const vector<PartyItemEvent>& listEvent, PartyItemEvent::Kind kind )
        {
            int32 count = 0;
            for ( const PartyItemEvent& event : listEvent )
            {
                count += event._kind == kind ? 1 : 0;
            }
            return count;
        }

        static int32 countEvents( const vector<PartySeriesEvent>& listEvent, PartySeriesEvent::Kind kind )
        {
            int32 count = 0;
            for ( const PartySeriesEvent& event : listEvent )
            {
                count += event._kind == kind ? 1 : 0;
            }
            return count;
        }
    };
} // namespace

/**
 * @brief [PartyArenaRulesTest] 닿은 순간 가까이 누르면 콤보가 오르고 더 높이 튀며, 넓은 창은 낮은 등급이다. 누르지 않으면 콤보가 끊긴다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, BounceTimingRaisesAndBreaksCombo )
{
    SW_REQUIRE_TRUE( open() );
    SW_REQUIRE_TRUE( bounceOnce( 0, true ) );
    SW_EXPECT_EQUAL( 1, body( 0 )._combo );
    SW_EXPECT_NEAR( MathUtil::sqrt( 2.0f * 30.0f * 2.75f ), body( 0 )._velocity._y, kPartyTolerance );
    const TrampolineEvent perfect = findLastEvent( TrampolineEvent::Kind::Bounced, 0 );
    SW_EXPECT_EQUAL( 1, perfect._value );
    SW_EXPECT_TRUE( perfect._grade == hashed_string( "Perfect" ) );

    // 1 번은 누르지 않았다.
    SW_REQUIRE_TRUE( stepUntilState( 1, TrampolinePlayerState::Air ) );
    SW_EXPECT_EQUAL( 0, body( 1 )._combo );
    SW_EXPECT_TRUE( findLastEvent( TrampolineEvent::Kind::Bounced, 1 )._grade.empty() );

    SW_REQUIRE_TRUE( stepUntilState( 0, TrampolinePlayerState::Contact ) );
    step( 3 );
    pressJump( 0 ); // 닿은 뒤 0.08 초
    SW_REQUIRE_TRUE( stepUntilState( 0, TrampolinePlayerState::Air ) );
    SW_EXPECT_EQUAL( 2, body( 0 )._combo );
    SW_EXPECT_TRUE( findLastEvent( TrampolineEvent::Kind::Bounced, 0 )._grade == hashed_string( "Good" ) );

    SW_REQUIRE_TRUE( bounceOnce( 0, false ) );
    SW_EXPECT_EQUAL( 0, body( 0 )._combo );
    SW_EXPECT_EQUAL( 2, findLastEvent( TrampolineEvent::Kind::ComboBroken, 0 )._value );
}

/**
 * @brief [PartyArenaRulesTest] 닿기 직전에 미리 누른 입력은 받고, 너무 이른 입력과 창이 닫힌 뒤의 입력은 받지 않는다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, EarlyAndLatePressesFollowTheWindow )
{
    SW_REQUIRE_TRUE( open() );
    while ( body( 0 )._position._y >= 0.6f )
    {
        step();
    }
    pressJump( 0 );
    SW_REQUIRE_TRUE( stepUntilState( 0, TrampolinePlayerState::Contact ) );
    SW_REQUIRE_TRUE( stepUntilState( 0, TrampolinePlayerState::Air ) );
    SW_EXPECT_EQUAL( 1, body( 0 )._combo );

    SW_REQUIRE_TRUE( open() );
    pressJump( 0 ); // 높이 3, 닿기 0.4 초 전
    SW_REQUIRE_TRUE( bounceOnce( 0, false ) );
    SW_EXPECT_EQUAL( 0, body( 0 )._combo );

    _settings._contactTime = 0.2f;
    SW_REQUIRE_TRUE( open() );
    SW_REQUIRE_TRUE( stepUntilState( 0, TrampolinePlayerState::Contact ) );
    step( 6 );
    SW_EXPECT_TRUE( body( 0 )._state == TrampolinePlayerState::Contact );
    pressJump( 0 ); // 닿은 뒤 0.14 초, 가장 넓은 창(0.1)이 닫혔다
    SW_REQUIRE_TRUE( stepUntilState( 0, TrampolinePlayerState::Air ) );
    SW_EXPECT_EQUAL( 0, body( 0 )._combo );
}

/**
 * @brief [PartyArenaRulesTest] 콤보와 튀는 높이는 상한에서 멈춘다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, ComboStopsAtTheLimit )
{
    _settings._maxCombo = 1;
    SW_REQUIRE_TRUE( open() );
    SW_EXPECT_NEAR( 2.0f, _arena.computeBounceHeight( -1 ), kPartyTolerance );
    SW_EXPECT_NEAR( 2.0f, _arena.computeBounceHeight( 0 ), kPartyTolerance );
    SW_EXPECT_NEAR( 2.75f, _arena.computeBounceHeight( 1 ), kPartyTolerance );
    SW_EXPECT_NEAR( 2.75f, _arena.computeBounceHeight( 99 ), kPartyTolerance );
    SW_REQUIRE_TRUE( bounceOnce( 0, true ) );
    SW_REQUIRE_TRUE( bounceOnce( 0, true ) );
    SW_EXPECT_EQUAL( 1, body( 0 )._combo );
}

/**
 * @brief [PartyArenaRulesTest] 돌진은 닿은 상대를 한 번 밀고 경직을 걸며 쿨다운이 돈다. 경직 중인 쪽은 공격하지 못한다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, DashHitsOnceAndStuns )
{
    SW_REQUIRE_TRUE( open() );
    SW_REQUIRE_TRUE( dashIntoOther() );
    SW_EXPECT_EQUAL( 1, countEvents( TrampolineEvent::Kind::Attacked, 0 ) );
    SW_EXPECT_TRUE( findLastEvent( TrampolineEvent::Kind::Hit, 1 )._other == 0 );
    SW_EXPECT_EQUAL( 0, body( 1 )._lastHitter );
    SW_EXPECT_TRUE( body( 1 )._stunTimer.isActive() );
    SW_EXPECT_TRUE( body( 1 )._velocity._x < 0.0f );

    TrampolineInput attack;
    attack._bAttackPressed = SW_TRUE;
    _arena.setInput( 0, attack ); // 쿨다운 중
    _arena.setInput( 1, attack ); // 경직 중
    step( 3 );
    SW_EXPECT_EQUAL( 1, countEvents( TrampolineEvent::Kind::Attacked, 0 ) );
    SW_EXPECT_EQUAL( 0, countEvents( TrampolineEvent::Kind::Attacked, 1 ) );
    SW_EXPECT_EQUAL( 1, countEvents( TrampolineEvent::Kind::Hit, 1 ) );
}

/**
 * @brief [PartyArenaRulesTest] 무적인 상대는 돌진이 지나간다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, InvulnerableTargetIsNotHit )
{
    _settings._spawnInvulnerable = 10.0f;
    SW_REQUIRE_TRUE( open() );
    SW_EXPECT_FALSE( dashIntoOther() );
    SW_EXPECT_EQUAL( -1, body( 1 )._lastHitter );
}

/**
 * @brief [PartyArenaRulesTest] 내려찍기는 높이를 넘은 곳에서만 나가고 낮은 상대만 밀며 콤보를 끊는다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, GroundPoundPushesLowTargets )
{
    _settings._poundHitHeight = 100.0f;
    SW_REQUIRE_TRUE( open() );
    TrampolineInput pound;
    pound._bPoundPressed = SW_TRUE;
    _arena.setInput( 1, pound );
    SW_REQUIRE_TRUE( stepUntilEvent( TrampolineEvent::Kind::GroundPound, 1, 20 ) );
    SW_EXPECT_EQUAL( 1, findLastEvent( TrampolineEvent::Kind::GroundPound, 1 )._value );
    SW_EXPECT_EQUAL( 1, body( 0 )._lastHitter );
    SW_EXPECT_TRUE( body( 0 )._velocity._x > 0.0f );
    SW_EXPECT_TRUE( body( 1 )._state == TrampolinePlayerState::Contact );
    SW_EXPECT_EQUAL( SW_FALSE, body( 1 )._bPounding );

    _settings._poundHitHeight = 1.0f;
    SW_REQUIRE_TRUE( open() );
    _arena.setInput( 1, pound );
    SW_REQUIRE_TRUE( stepUntilEvent( TrampolineEvent::Kind::GroundPound, 1, 20 ) );
    SW_EXPECT_EQUAL( 0, findLastEvent( TrampolineEvent::Kind::GroundPound, 1 )._value ); // 0 번은 아직 높이 있다
    SW_EXPECT_EQUAL( -1, body( 0 )._lastHitter );

    SW_REQUIRE_TRUE( open() );
    while ( body( 1 )._position._y >= 0.9f )
    {
        step();
    }
    _arena.setInput( 1, pound ); // 너무 낮다
    step();
    SW_EXPECT_EQUAL( SW_FALSE, body( 1 )._bPounding );
}

/**
 * @brief [PartyArenaRulesTest] 방패는 돌진과 내려찍기를 한 번 막고 깨진다. 결함: 내려찍기가 방패를 뚫었다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, ShieldBlocksDashAndPoundOnce )
{
    _settings._pickupRadius   = 100.0f; // 먼저 보는 0 번이 줍는다
    _settings._poundHitHeight = 100.0f;
    SW_REQUIRE_TRUE( _arena.getItemSpawner().loadFromXMLText( kPartyShieldXML, "PartyArenaTest" ) );
    SW_REQUIRE_TRUE( open() );
    step();
    SW_EXPECT_EQUAL( SW_TRUE, body( 0 )._bShield );
    SW_EXPECT_TRUE( findLastEvent( TrampolineEvent::Kind::ItemPicked, 0 )._itemID == hashed_string( "shield" ) );

    TrampolineInput pound;
    pound._bPoundPressed = SW_TRUE;
    _arena.setInput( 1, pound );
    SW_REQUIRE_TRUE( stepUntilEvent( TrampolineEvent::Kind::GroundPound, 1, 20 ) );
    SW_EXPECT_EQUAL( 0, findLastEvent( TrampolineEvent::Kind::GroundPound, 1 )._value );
    SW_EXPECT_EQUAL( 1, countEvents( TrampolineEvent::Kind::ShieldBlocked, 0 ) );
    SW_EXPECT_EQUAL( SW_FALSE, body( 0 )._bShield );
    SW_EXPECT_EQUAL( -1, body( 0 )._lastHitter );
    SW_EXPECT_FALSE( body( 0 )._stunTimer.isActive() );

    // 돌진: 1 번이 방패를 든 0 번을 민다.
    SW_REQUIRE_TRUE( _arena.initialize( _settings, 2, 0.0f, 0, 5u ) );
    _arena.start();
    _listEvent.clear();
    step();
    SW_REQUIRE_TRUE( body( 0 )._bShield == SW_TRUE );
    TrampolineInput dash;
    dash._moveX          = 1.0f;
    dash._bAttackPressed = SW_TRUE;
    _arena.setInput( 1, dash );
    SW_REQUIRE_TRUE( stepUntilEvent( TrampolineEvent::Kind::ShieldBlocked, 0, 30 ) );
    SW_EXPECT_EQUAL( 0, countEvents( TrampolineEvent::Kind::Hit, 0 ) );
    SW_EXPECT_EQUAL( SW_FALSE, body( 0 )._bShield );
    SW_EXPECT_EQUAL( SW_TRUE, body( 1 )._bHitThisDash );
}

/**
 * @brief [PartyArenaRulesTest] 밀려 떨어지면 민 쪽이 점수를 받고, 떨어진 쪽은 대기 뒤 스폰 지점으로 돌아온다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, RingOutCreditsThePusherThenRespawns )
{
    SW_REQUIRE_TRUE( open() );
    SW_REQUIRE_TRUE( dashIntoOther() );
    SW_REQUIRE_TRUE( stepUntilEvent( TrampolineEvent::Kind::RingOut, 1 ) );
    SW_EXPECT_TRUE( findLastEvent( TrampolineEvent::Kind::RingOut, 1 )._other == 0 );
    SW_EXPECT_TRUE( body( 1 )._state == TrampolinePlayerState::Respawning );
    vector<int32> listScore;
    _arena.computeRoundScores( listScore );
    SW_REQUIRE_EQUAL( size_t( 2 ), listScore.size() );
    SW_EXPECT_EQUAL( 1, listScore[0] );
    SW_EXPECT_EQUAL( 0, listScore[1] );
    SW_EXPECT_EQUAL( 1, _arena.getMatch().findParticipant( 1 )->_deaths );
    SW_EXPECT_TRUE( readInFreshArena( writeBytes( _arena ) ) ); // 탈락한 상태도 읽힌다

    SW_REQUIRE_TRUE( stepUntilEvent( TrampolineEvent::Kind::Respawned, 1, 40 ) );
    SW_EXPECT_TRUE( body( 1 )._state == TrampolinePlayerState::Air );
    SW_EXPECT_NEAR( -1.0f, body( 1 )._position._x, kPartyTolerance );
    SW_EXPECT_EQUAL( -1, body( 1 )._lastHitter );
}

/**
 * @brief [PartyArenaRulesTest] 민 지 오래되었거나 혼자 떨어지면 점수가 없고, 벌점이 있으면 깎인다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, RingOutWithoutCredit )
{
    _settings._creditWindow = 0.0f;
    SW_REQUIRE_TRUE( open() );
    SW_REQUIRE_TRUE( dashIntoOther() );
    SW_REQUIRE_TRUE( stepUntilEvent( TrampolineEvent::Kind::RingOut, 1 ) );
    SW_EXPECT_FALSE( findLastEvent( TrampolineEvent::Kind::RingOut, 1 )._other.has_value() );
    SW_EXPECT_EQUAL( 0, _arena.getMatch().findTeam( 0 )->_score );

    _settings._creditWindow   = 3.0f;
    _settings._selfOutPenalty = 2;
    SW_REQUIRE_TRUE( open() );
    TrampolineInput walkOff;
    walkOff._moveX = 1.0f;
    _arena.setInput( 0, walkOff );
    SW_REQUIRE_TRUE( stepUntilEvent( TrampolineEvent::Kind::RingOut, 0 ) );
    SW_EXPECT_FALSE( findLastEvent( TrampolineEvent::Kind::RingOut, 0 )._other.has_value() );
    SW_EXPECT_EQUAL( -2, _arena.getMatch().findTeam( 0 )->_score );
}

/**
 * @brief [PartyArenaRulesTest] 점수 제한 · 시간 제한에 닿으면 라운드가 끝나고 시간이 멈춘다. `start` 전에도 시간이 흐르지 않는다. 결함: `start` 전에 몸이 원점에서 떨어졌다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, RoundEndsAtLimitsAndTimeOnlyRunsWhileInProgress )
{
    SW_REQUIRE_TRUE( open( 0.0f, 1 ) );
    SW_REQUIRE_TRUE( dashIntoOther() );
    SW_REQUIRE_TRUE( stepUntilEvent( TrampolineEvent::Kind::RingOut, 1 ) );
    SW_EXPECT_TRUE( _arena.isRoundOver() );
    SW_EXPECT_EQUAL( 0, _arena.getMatch().getWinningTeam() );
    const float32 endTime = _arena.getTime();
    step( 5 );
    SW_EXPECT_NEAR( endTime, _arena.getTime(), 0.0f );
    SW_EXPECT_TRUE( readInFreshArena( writeBytes( _arena ) ) );

    SW_REQUIRE_TRUE( open( 0.1f, 0 ) );
    step( 7 );
    SW_EXPECT_TRUE( _arena.isRoundOver() );
    SW_EXPECT_EQUAL( -1, _arena.getMatch().getWinningTeam() ); // 동점

    TrampolineArena waiting;
    SW_REQUIRE_TRUE( waiting.initialize( _settings, 2, 0.0f, 0, 5u ) );
    waiting.step();
    SW_EXPECT_EQUAL( 1, waiting.update( kPartyStep ) );
    SW_EXPECT_NEAR( 0.0f, waiting.getTime(), 0.0f );
    SW_EXPECT_NEAR( 0.0f, waiting.findPlayer( 0 )->_position._y, 0.0f );
}

/**
 * @brief [PartyArenaRulesTest] 땅에 닿으면 돌진이 끝난다. 결함: 남은 돌진이 튀어 오른 뒤 이어져 중력 없이 떠올랐다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, LandingEndsTheDash )
{
    _settings._baseBounceHeight = 0.0f;
    _settings._comboHeightStep  = 0.0f;
    _settings._contactTime      = 0.0f;
    SW_REQUIRE_TRUE( open() );
    SW_REQUIRE_TRUE( stepUntilState( 0, TrampolinePlayerState::Contact ) );
    SW_REQUIRE_TRUE( stepUntilState( 0, TrampolinePlayerState::Air ) ); // 높이 0 으로 튀었다
    TrampolineInput attack;
    attack._bAttackPressed = SW_TRUE;
    _arena.setInput( 0, attack );
    step();
    SW_EXPECT_TRUE( body( 0 )._state == TrampolinePlayerState::Contact );
    SW_EXPECT_FALSE( body( 0 )._attackTimer.isActive() );
}

/**
 * @brief [PartyArenaRulesTest] 사람 수 · 설정 · 시간 · 점수가 범위 밖이면 오류로 거절한다. 결함: 사람 수는 말없이 거절했다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, BrokenInitializeIsRefused )
{
    SW_EXPECT_LOG( "TrampolineArena.TrampolineArenaRefusedPlayerCount" );
    SW_EXPECT_LOG( "TrampolineArena.TrampolineArenaRefusedSettings" );
    SW_EXPECT_LOG( "TrampolineArena.TrampolineArenaRefusedRoundTimeScoreLimit" );
    SW_EXPECT_FALSE( _arena.initialize( _settings, 1, 0.0f, 0, 5u ) );
    SW_EXPECT_FALSE( _arena.initialize( _settings, 5, 0.0f, 0, 5u ) );
    SW_EXPECT_FALSE( _arena.initialize( _settings, 2, -1.0f, 0, 5u ) );
    SW_EXPECT_FALSE( _arena.initialize( _settings, 2, 0.0f, -1, 5u ) );
    TrampolineSettings broken = _settings;
    broken._arenaRadius       = 0.0f;
    SW_EXPECT_FALSE( _arena.initialize( broken, 2, 0.0f, 0, 5u ) );
    SW_EXPECT_EQUAL( 0, _arena.getPlayerCount() );
    SW_EXPECT_NULL( _arena.findPlayer( 0 ) );
}

/**
 * @brief [PartyArenaRulesTest] 눌림은 다음 걸음까지 모았다가 한 걸음에만 쓰고, 입력 비트는 왕복하며 축은 잘린다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, InputPressesAccumulateAndBitsRoundTrip )
{
    SW_REQUIRE_TRUE( open() );
    pressJump( 0 );
    _arena.setInput( 0, TrampolineInput{} );
    SW_EXPECT_EQUAL( SW_TRUE, body( 0 )._input._bJumpPressed );
    step();
    SW_EXPECT_EQUAL( SW_FALSE, body( 0 )._input._bJumpPressed );
    _arena.setInput( 9, TrampolineInput{} );
    SW_EXPECT_NULL( _arena.findPlayer( 9 ) );

    TrampolineInput input;
    input._moveX          = 5.0f;
    input._moveZ          = -1.0f;
    input._bAttackPressed = SW_TRUE;
    BitWriter writer;
    TrampolineArena::writeInput( writer, input );
    writer.alignToByte();
    BitReader             reader( writer.getBytes().data(), writer.getByteCount() );
    const TrampolineInput decoded = TrampolineArena::readInput( reader );
    SW_EXPECT_NEAR( 1.0f, decoded._moveX, kPartyTolerance );
    SW_EXPECT_NEAR( -1.0f, decoded._moveZ, kPartyTolerance );
    SW_EXPECT_EQUAL( SW_FALSE, decoded._bJumpPressed );
    SW_EXPECT_EQUAL( SW_TRUE, decoded._bAttackPressed );
    SW_EXPECT_EQUAL( SW_FALSE, decoded._bPoundPressed );
}

/**
 * @brief [PartyArenaRulesTest] SuperBounce 는 다음 튕김만 두 배로 높이고, Heavy 는 시간 동안 배율을 바꾼다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, ItemEffectsApply )
{
    _settings._pickupRadius = 100.0f;
    SW_REQUIRE_TRUE( _arena.getItemSpawner().loadFromXMLText( R"(<PartyItems minInterval="0" maxInterval="1000" lifetime="0" height="0" maxActive="1"><Item id="spring" effect="SuperBounce"/></PartyItems>)", "PartyArenaTest" ) );
    SW_REQUIRE_TRUE( open() );
    step();
    SW_EXPECT_EQUAL( SW_TRUE, body( 0 )._bSuperBounce );
    SW_REQUIRE_TRUE( bounceOnce( 0, false ) );
    SW_EXPECT_NEAR( MathUtil::sqrt( 2.0f * 30.0f * 4.0f ), body( 0 )._velocity._y, kPartyTolerance );
    SW_EXPECT_EQUAL( SW_FALSE, body( 0 )._bSuperBounce );

    TrampolineArena heavy;
    SW_REQUIRE_TRUE( heavy.getItemSpawner().loadFromXMLText( R"(<PartyItems minInterval="0" maxInterval="1000" lifetime="0" height="0" maxActive="1"><Item id="anvil" effect="Heavy" duration="0.1" knockbackTaken="0.5" knockbackDealt="2"/></PartyItems>)", "PartyArenaTest" ) );
    SW_REQUIRE_TRUE( heavy.initialize( _settings, 2, 0.0f, 0, 5u ) );
    heavy.start();
    heavy.step();
    SW_EXPECT_NEAR( 0.5f, heavy.findPlayer( 0 )->_knockbackTaken, kPartyTolerance );
    SW_EXPECT_NEAR( 2.0f, heavy.findPlayer( 0 )->_knockbackDealt, kPartyTolerance );
    for ( int32 index = 0; index < 7; ++index )
    {
        heavy.step();
    }
    SW_EXPECT_NEAR( 1.0f, heavy.findPlayer( 0 )->_knockbackTaken, kPartyTolerance );
    SW_EXPECT_NEAR( 1.0f, heavy.findPlayer( 0 )->_knockbackDealt, kPartyTolerance );
}

/**
 * @brief [PartyArenaRulesTest] 수치가 빠졌거나 범위 밖인 Heavy 와 모르는 효과는 오류로 알리고 효과를 내지 않는다. 결함: 시간 0 이하면 배율이 부활까지 남았고, 모르는 효과는 말없이 사라졌다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, BrokenItemsAreReported )
{
    SW_EXPECT_LOG( "TrampolineArena.ItemHeavyNeedsDuration" );
    SW_EXPECT_LOG( "TrampolineArena.ItemUnknownEffect" );
    _settings._pickupRadius    = 100.0f;
    const utf8* const arrXML[] = {
        R"(<PartyItems minInterval="0" maxInterval="1000" lifetime="0" height="0" maxActive="1"><Item id="a" effect="Heavy" duration="1"/></PartyItems>)",
        R"(<PartyItems minInterval="0" maxInterval="1000" lifetime="0" height="0" maxActive="1"><Item id="a" effect="Heavy" duration="-1" knockbackTaken="0.5" knockbackDealt="2"/></PartyItems>)",
        R"(<PartyItems minInterval="0" maxInterval="1000" lifetime="0" height="0" maxActive="1"><Item id="a" effect="Heavy" duration="1" knockbackTaken="-0.5" knockbackDealt="2"/></PartyItems>)",
        R"(<PartyItems minInterval="0" maxInterval="1000" lifetime="0" height="0" maxActive="1"><Item id="a" effect="Mystery"/></PartyItems>)",
    };
    for ( const utf8* const pXML : arrXML )
    {
        TrampolineArena arena;
        SW_REQUIRE_TRUE( arena.getItemSpawner().loadFromXMLText( pXML, "PartyArenaTest" ) );
        SW_REQUIRE_TRUE( arena.initialize( _settings, 2, 0.0f, 0, 5u ) );
        arena.start();
        arena.step();
        SW_EXPECT_NEAR( 1.0f, arena.findPlayer( 0 )->_knockbackTaken, kPartyTolerance );
        SW_EXPECT_FALSE( arena.findPlayer( 0 )->_heavyTimer.isActive() );
    }
}

/**
 * @brief [PartyArenaRulesTest] 상태 왕복은 몸 · 아이템 · 경기 · 시각을 이어 같은 바이트를 낸다. 다른 인원 · 잘린 바이트는 거절한다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, StateRoundTripContinuesIdentically )
{
    const auto loadItems = []( TrampolineArena& arena ) -> bool
    {
        return arena.getItemSpawner().loadFromXMLText( R"(<PartyItems minInterval="0" maxInterval="0.1" lifetime="0" height="50" maxActive="2"><Item id="spring" effect="SuperBounce"/></PartyItems>)", "PartyArenaTest" );
    };
    SW_REQUIRE_TRUE( loadItems( _arena ) );
    SW_REQUIRE_TRUE( open() );
    SW_REQUIRE_TRUE( dashIntoOther() );
    step( 5 );
    SW_EXPECT_FALSE( _arena.getItemSpawner().getInstances().empty() );

    const vector<uint8> listSaved = writeBytes( _arena );
    TrampolineArena     restored;
    SW_REQUIRE_TRUE( loadItems( restored ) );
    SW_REQUIRE_TRUE( restored.initialize( _settings, 2, 0.0f, 0, 5u ) );
    Archive reading( listSaved.data(), listSaved.size() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( writeBytes( restored ) == listSaved );

    for ( int32 index = 0; index < 60; ++index )
    {
        restored.step();
    }
    step( 60 );
    SW_EXPECT_TRUE( writeBytes( restored ) == writeBytes( _arena ) );

    TrampolineArena three;
    SW_REQUIRE_TRUE( loadItems( three ) );
    SW_REQUIRE_TRUE( three.initialize( _settings, 3, 0.0f, 0, 5u ) );
    Archive threeReading( listSaved.data(), listSaved.size() );
    SW_EXPECT_FALSE( three.readState( threeReading ) );
    SW_EXPECT_EQUAL( 3, three.getPlayerCount() );

    Archive cut( listSaved.data(), listSaved.size() / 2 );
    SW_EXPECT_FALSE( restored.readState( cut ) );
    SW_EXPECT_NEAR( _arena.getTime(), restored.getTime(), 0.0f );
}

/**
 * @brief [PartyArenaRulesTest] 꾸민 바이트: 콤보 · 때린 사람 · 상태 · 깃발 · 배율 · 시각이 범위 밖이거나 NaN 이면 거절한다. 결함: 최대를 넘는 콤보 · 음수 배율 · 음수 시각을 받았다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, CraftedBytesAreRefused )
{
    SW_REQUIRE_TRUE( open() );
    SW_REQUIRE_TRUE( readInFreshArena( writeBytes( _arena ) ) );
    const size_t head = kPartyFirstPlayer;
    SW_EXPECT_FALSE( readPatched<int32>( head + kPartyFieldCombo, -1 ) );
    SW_EXPECT_FALSE( readPatched<int32>( head + kPartyFieldCombo, _settings._maxCombo + 1 ) );
    SW_EXPECT_TRUE( readPatched<int32>( head + kPartyFieldCombo, _settings._maxCombo ) );
    SW_EXPECT_FALSE( readPatched<int32>( head + kPartyFieldLastHitter, 2 ) );
    SW_EXPECT_FALSE( readPatched<int32>( head + kPartyFieldLastHitter, -2 ) );
    SW_EXPECT_FALSE( readPatched<uint8>( head + kPartyFieldState, 4 ) );
    SW_EXPECT_FALSE( readPatched<uint8>( head + kPartyFieldFlag, 2 ) );
    SW_EXPECT_FALSE( readPatched<uint8>( head + kPartyFieldInputFlag, 2 ) );
    SW_EXPECT_FALSE( readPatched<float32>( head + kPartyFieldTaken, -1.0f ) );
    SW_EXPECT_FALSE( readPatched<float32>( head + kPartyFieldDealt, -1.0f ) );
    SW_EXPECT_FALSE( readPatched<float32>( head + kPartyFieldPosition, std::numeric_limits<float32>::quiet_NaN() ) );
    SW_EXPECT_FALSE( readPatched<float32>( writeBytes( _arena ).size() - sizeof( float32 ), -1.0f ) ); // 끝의 시각
}

/**
 * @brief [PartyArenaRulesTest] 진행 중에 몸은 탈락인데 경기는 살아 있으면 거절한다. 결함: 받으면 그 몸은 오지 않을 부활을 기다렸다
 */
SW_TEST_CASE_F( PartyArenaRulesTest, CraftedRespawnStateMustMatchTheMatch )
{
    SW_REQUIRE_TRUE( open() );
    SW_EXPECT_FALSE( readPatched<uint8>( kPartyFirstPlayer + kPartyFieldState, static_cast<uint8>( TrampolinePlayerState::Respawning ) ) );

    SW_REQUIRE_TRUE( dashIntoOther() );
    SW_REQUIRE_TRUE( stepUntilEvent( TrampolineEvent::Kind::RingOut, 1 ) );
    const size_t second = kPartyFirstPlayer + 104;                                                                        // 사람 하나의 크기
    SW_EXPECT_FALSE( readPatched<uint8>( second + kPartyFieldState, static_cast<uint8>( TrampolinePlayerState::Air ) ) ); // 경기는 탈락인데 몸은 공중이다
}

/**
 * @brief [PartyArenaTest] 첫 아이템은 최소 간격 뒤에 원판 안에 놓이고, 수는 상한을 넘지 않으며, 수명이 지나면 치운다. 같은 시드면 같은 위치다
 */
SW_TEST_CASE( PartyArenaTest, SpawnerPlacesCapsAndExpires )
{
    PartyItemSpawner spawner;
    PartyItemSpawner twin;
    SW_REQUIRE_TRUE( spawner.initialize( PartyArenaTestUtil::makeSettings(), 7u ) );
    SW_REQUIRE_TRUE( twin.initialize( PartyArenaTestUtil::makeSettings(), 7u ) );
    spawner.addItem( PartyArenaTestUtil::makeItem( "a", 1.0f ) );
    twin.addItem( PartyArenaTestUtil::makeItem( "a", 1.0f ) );

    spawner.update( 0.5f );
    spawner.update( 0.0f ); // 시간이 흐르지 않는다
    spawner.update( -1.0f );
    SW_EXPECT_TRUE( spawner.getInstances().empty() );
    SW_EXPECT_NEAR( 0.5f, spawner.getSpawnTimer(), kPartyTolerance );
    spawner.update( 0.5f );
    SW_REQUIRE_EQUAL( size_t( 1 ), spawner.getInstances().size() );
    const PartyItemInstance& first = spawner.getInstances()[0];
    SW_EXPECT_EQUAL( 1, first._serial );
    SW_EXPECT_NEAR( 1.5f, first._position._y, kPartyTolerance );
    SW_EXPECT_TRUE( first._position._x * first._position._x + first._position._z * first._position._z <= 25.0f + kPartyTolerance );

    twin.update( 1.0f );
    SW_REQUIRE_EQUAL( size_t( 1 ), twin.getInstances().size() );
    SW_EXPECT_NEAR( first._position._x, twin.getInstances()[0]._position._x, kPartyTolerance );
    SW_EXPECT_NEAR( first._position._z, twin.getInstances()[0]._position._z, kPartyTolerance );

    size_t most = 0;
    for ( int32 index = 0; index < 20; ++index )
    {
        spawner.update( 0.5f );
        most = spawner.getInstances().size() > most ? spawner.getInstances().size() : most;
    }
    SW_EXPECT_EQUAL( size_t( 2 ), most );
    vector<PartyItemEvent> listEvent;
    spawner.pollEvents( listEvent );
    SW_EXPECT_TRUE( PartyArenaTestUtil::countEvents( listEvent, PartyItemEvent::Kind::Expired ) > 0 );
    SW_EXPECT_TRUE( PartyArenaTestUtil::countEvents( listEvent, PartyItemEvent::Kind::Spawned ) > 2 );

    spawner.update( 0.5f );
    spawner.discardEvents();
    listEvent.clear();
    spawner.pollEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );
}

/**
 * @brief [PartyArenaTest] 정의가 없으면 나오지 않고 시간도 쓰지 않으며, 가중치 0 은 고르지 않는다. 수명 0 은 치우지 않는다
 */
SW_TEST_CASE( PartyArenaTest, SpawnerRespectsWeightsAndEmptyCatalog )
{
    PartyItemSpawnSettings settings = PartyArenaTestUtil::makeSettings();
    settings._lifetime              = 0.0f;
    settings._maxActive             = 20;
    PartyItemSpawner empty;
    SW_REQUIRE_TRUE( empty.initialize( settings, 7u ) );
    empty.update( 5.0f );
    SW_EXPECT_TRUE( empty.getInstances().empty() );
    SW_EXPECT_NEAR( 1.0f, empty.getSpawnTimer(), kPartyTolerance );

    PartyItemSpawner weightless;
    SW_REQUIRE_TRUE( weightless.initialize( settings, 7u ) );
    weightless.addItem( PartyArenaTestUtil::makeItem( "none", 0.0f ) );
    weightless.update( 1.0f );
    SW_EXPECT_TRUE( weightless.getInstances().empty() );

    PartyItemSpawner weighted;
    SW_REQUIRE_TRUE( weighted.initialize( settings, 7u ) );
    weighted.addItem( PartyArenaTestUtil::makeItem( "none", 0.0f ) );
    weighted.addItem( PartyArenaTestUtil::makeItem( "some", 1.0f ) );
    for ( int32 index = 0; index < 10; ++index )
    {
        weighted.update( 1.0f );
    }
    SW_EXPECT_EQUAL( size_t( 10 ), weighted.getInstances().size() );
    for ( const PartyItemInstance& instance : weighted.getInstances() )
    {
        SW_EXPECT_TRUE( instance._itemID == hashed_string( "some" ) );
    }
}

/**
 * @brief [PartyArenaTest] 줍기는 반지름 안의 가장 가까운 것을 가져가고, 없으면 false 다
 */
SW_TEST_CASE( PartyArenaTest, SpawnerPicksUpTheNearest )
{
    PartyItemSpawner spawner;
    SW_REQUIRE_TRUE( spawner.initialize( PartyArenaTestUtil::makeSettings(), 7u ) );
    spawner.addItem( PartyArenaTestUtil::makeItem( "a", 1.0f ) );
    spawner.update( 1.0f );
    spawner.update( 1.0f );
    SW_REQUIRE_EQUAL( size_t( 2 ), spawner.getInstances().size() );
    const float3 second = spawner.getInstances()[1]._position;

    PartyItemInstance item;
    SW_EXPECT_FALSE( spawner.tryPickUp( second + float3{ 100.0f, 0.0f, 0.0f }, 1.0f, 3, item ) );
    SW_REQUIRE_TRUE( spawner.tryPickUp( second, 0.5f, 3, item ) );
    SW_EXPECT_EQUAL( 2, item._serial );
    SW_REQUIRE_EQUAL( size_t( 1 ), spawner.getInstances().size() );
    SW_EXPECT_EQUAL( 1, spawner.getInstances()[0]._serial );

    vector<PartyItemEvent> listEvent;
    spawner.pollEvents( listEvent );
    SW_REQUIRE_EQUAL( 1, PartyArenaTestUtil::countEvents( listEvent, PartyItemEvent::Kind::PickedUp ) );
    SW_EXPECT_TRUE( listEvent.back()._player == 3 );
}

/**
 * @brief [PartyArenaTest] 스포너 상태 왕복은 난수 · 타이머 · 번호를 이어 같은 곳에 같은 번호로 놓는다
 */
SW_TEST_CASE( PartyArenaTest, SpawnerStateRoundTrips )
{
    PartyItemSpawnSettings settings = PartyArenaTestUtil::makeSettings();
    settings._lifetime              = 0.0f;
    PartyItemSpawner source;
    SW_REQUIRE_TRUE( source.initialize( settings, 7u ) );
    source.addItem( PartyArenaTestUtil::makeItem( "a", 1.0f ) );
    source.update( 1.25f );
    Archive written;
    source.writeState( written );

    PartyItemSpawner restored;
    SW_REQUIRE_TRUE( restored.initialize( settings, 99u ) );
    restored.addItem( PartyArenaTestUtil::makeItem( "a", 1.0f ) );
    Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_NEAR( source.getSpawnTimer(), restored.getSpawnTimer(), 0.0f );

    source.update( 1.0f );
    restored.update( 1.0f );
    SW_REQUIRE_EQUAL( size_t( 2 ), source.getInstances().size() );
    SW_REQUIRE_EQUAL( size_t( 2 ), restored.getInstances().size() );
    SW_EXPECT_EQUAL( 2, restored.getInstances()[1]._serial );
    SW_EXPECT_NEAR( source.getInstances()[1]._position._x, restored.getInstances()[1]._position._x, 0.0f );
    SW_EXPECT_NEAR( source.getInstances()[1]._position._z, restored.getInstances()[1]._position._z, 0.0f );
}

/**
 * @brief [PartyArenaTest] 꾸민 스포너 바이트: 모르는 id, 음수 나이, 범위 밖 번호, 상한을 넘는 수, 잘린 바이트는 거절하고 상태를 바꾸지 않는다. 결함: id 만 보고 나머지를 받았다
 */
SW_TEST_CASE( PartyArenaTest, SpawnerRefusesCraftedState )
{
    PartyItemSpawner spawner;
    SW_REQUIRE_TRUE( spawner.initialize( PartyArenaTestUtil::makeSettings(), 7u ) );
    spawner.addItem( PartyArenaTestUtil::makeItem( "a", 1.0f ) );
    using Util = PartyArenaTestUtil;
    SW_EXPECT_FALSE( Util::readFixture( spawner, "ghost", 0.0f, 1, 2 ) );
    SW_EXPECT_FALSE( Util::readFixture( spawner, "a", -1.0f, 1, 2 ) );
    SW_EXPECT_FALSE( Util::readFixture( spawner, "a", 0.0f, 0, 2 ) );
    SW_EXPECT_FALSE( Util::readFixture( spawner, "a", 0.0f, 2, 2 ) ); // 다음 번호와 같다
    SW_EXPECT_FALSE( Util::readFixture( spawner, "a", 0.0f, 1, 0 ) );
    SW_EXPECT_FALSE( Util::readFixture( spawner, "a", 0.0f, 1, 9, 3 ) ); // 상한은 둘이다
    SW_EXPECT_TRUE( spawner.getInstances().empty() );
    SW_EXPECT_TRUE( Util::readFixture( spawner, "a", 0.0f, 1, 3, 2 ) );
    SW_EXPECT_EQUAL( size_t( 2 ), spawner.getInstances().size() );

    Archive written;
    spawner.writeState( written );
    Archive cut( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( spawner.readState( cut ) );
    SW_EXPECT_EQUAL( size_t( 2 ), spawner.getInstances().size() );
}

/**
 * @brief [PartyArenaTest] 범위 밖 규칙은 오류로 거절하고 규칙을 그대로 둔다
 */
SW_TEST_CASE( PartyArenaTest, SpawnerRefusesBrokenSettings )
{
    SW_EXPECT_LOG( "PartyItemSpawner.PartyItemSpawnerRefusedSettings" );
    PartyItemSpawner       spawner;
    PartyItemSpawnSettings settings = PartyArenaTestUtil::makeSettings();
    settings._maxActive             = 0;
    SW_EXPECT_FALSE( spawner.initialize( settings, 7u ) );
    settings              = PartyArenaTestUtil::makeSettings();
    settings._maxInterval = 0.5f; // 최소보다 작다
    SW_EXPECT_FALSE( spawner.initialize( settings, 7u ) );
    SW_EXPECT_NEAR( PartyItemSpawnSettings{}._minInterval, spawner.getSettings()._minInterval, kPartyTolerance );
}

/**
 * @brief [PartyArenaTest] 스포너 XML: 규칙과 효과 수치를 읽고, 틀린 원소만 버린다. 겹친 id 는 앞 정의를 지키고, 루트가 틀리면 파일을 읽지 않는다. 결함: 겹친 id 가 덮어썼고 틀린 루트에도 성공했다
 */
SW_TEST_CASE( PartyArenaTest, SpawnerReadsXML )
{
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    SW_EXPECT_LOG( "PartyItemSpawner.ItemNeedsIdEffect" );
    SW_EXPECT_LOG( "GameDataXML.DuplicateID" );
    SW_EXPECT_LOG( "XMLCatalog.NoEntries" );
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    PartyItemSpawner spawner;
    SW_REQUIRE_TRUE( spawner.loadFromXMLText( R"(<PartyItems minInterval="2" maxInterval="3" lifetime="4" radius="5" height="6" maxActive="7">
  <Item id="anvil" effect="Heavy" weight="2" duration="8" knockbackTaken="0.5"/>
  <Item id="anvil" effect="Other"/>
  <Item id="bad" effect="Heavy" weight="-1"/>
  <Thing id="x"/>
</PartyItems>)",
                                              "PartyArenaTest" ) );
    SW_EXPECT_NEAR( 2.0f, spawner.getSettings()._minInterval, kPartyTolerance );
    SW_EXPECT_EQUAL( 7, spawner.getSettings()._maxActive );
    const PartyItemDef* pDef = spawner.findItem( hashed_string( "anvil" ) );
    SW_REQUIRE_NOT_NULL( pDef );
    SW_EXPECT_TRUE( pDef->_effect == hashed_string( "Heavy" ) );
    SW_EXPECT_NEAR( 2.0f, pDef->_weight, kPartyTolerance );
    SW_EXPECT_NEAR( 8.0f, pDef->_stats.getValue( hashed_string( "duration" ) ), kPartyTolerance );
    SW_EXPECT_NULL( spawner.findItem( hashed_string( "bad" ) ) );

    PartyItemSpawner none;
    SW_EXPECT_FALSE( none.loadFromXMLText( R"(<PartyItems/>)", "PartyArenaTest" ) );

    PartyItemSpawner brokenRoot;
    SW_EXPECT_FALSE( brokenRoot.loadFromXMLText( R"(<PartyItems maxActive="0"><Item id="a" effect="b"/></PartyItems>)", "PartyArenaTest" ) );
    SW_EXPECT_NULL( brokenRoot.findItem( hashed_string( "a" ) ) );
}

/**
 * @brief [PartyArenaTest] 라운드 순위대로 점수가 쌓이고 먼저 목표에 닿은 사람이 우승한다. 라운드 목록은 돌고, 끝나면 결과를 받지 않는다
 */
SW_TEST_CASE( PartyArenaTest, SeriesRanksRoundsAndFindsAWinner )
{
    PartyRoundSeries series;
    SW_EXPECT_NULL( series.getCurrentRound() );
    SW_EXPECT_FALSE( series.start( 3 ) ); // 라운드가 없다
    PartyRoundDef roundA;
    roundA._id = hashed_string( "a" );
    PartyRoundDef roundB;
    roundB._id = hashed_string( "b" );
    series.addRound( roundA );
    series.addRound( roundB );
    SW_EXPECT_FALSE( series.start( 1 ) );
    SW_EXPECT_FALSE( series.reportRound( { 1, 2, 3 } ) ); // 시작 전
    SW_REQUIRE_TRUE( series.start( 3 ) );
    SW_EXPECT_EQUAL( 3, series.getPlayerCount() );
    SW_EXPECT_TRUE( series.getCurrentRound()->_id == hashed_string( "a" ) );

    SW_EXPECT_FALSE( series.reportRound( { 1, 2 } ) ); // 사람 수가 다르다
    SW_REQUIRE_TRUE( series.reportRound( { 10, 5, 1 } ) );
    SW_EXPECT_EQUAL( 3, series.getTotal( 0 ) );
    SW_EXPECT_EQUAL( 2, series.getTotal( 1 ) );
    SW_EXPECT_EQUAL( 1, series.getTotal( 2 ) );
    SW_EXPECT_EQUAL( 1, series.getRoundNumber() );
    SW_EXPECT_TRUE( series.getCurrentRound()->_id == hashed_string( "b" ) );

    vector<PartySeriesEvent> listEvent;
    series.pollEvents( listEvent );
    SW_EXPECT_EQUAL( 2, PartyArenaTestUtil::countEvents( listEvent, PartySeriesEvent::Kind::RoundStarted ) );
    SW_EXPECT_EQUAL( 3, PartyArenaTestUtil::countEvents( listEvent, PartySeriesEvent::Kind::RoundRanked ) );
    for ( const PartySeriesEvent& event : listEvent )
    {
        if ( event._kind != PartySeriesEvent::Kind::RoundRanked )
            continue;
        SW_EXPECT_TRUE( event._roundID == hashed_string( "a" ) ); // 끝난 라운드의 id
        SW_EXPECT_EQUAL( event._player.value() + 1, event._value );
        SW_EXPECT_EQUAL( 3 - event._player.value(), event._points );
    }

    SW_REQUIRE_TRUE( series.reportRound( { 10, 5, 1 } ) );
    SW_EXPECT_TRUE( series.isFinished() );
    SW_EXPECT_EQUAL( 0, series.getWinner() );
    listEvent.clear();
    series.pollEvents( listEvent );
    SW_EXPECT_EQUAL( 1, PartyArenaTestUtil::countEvents( listEvent, PartySeriesEvent::Kind::SeriesWon ) );
    SW_EXPECT_FALSE( series.reportRound( { 10, 5, 1 } ) );
}

/**
 * @brief [PartyArenaTest] 같은 점수는 같은 순위이고, 함께 닿아 총점까지 같으면 서든 데스로 이어 간다. 목록보다 낮은 순위는 0 점이고 라운드 목록은 처음으로 돈다
 */
SW_TEST_CASE( PartyArenaTest, SeriesTiesGoToSuddenDeath )
{
    PartyRoundSeries series;
    PartyRoundDef    round;
    round._id = hashed_string( "only" );
    series.addRound( round );
    SW_REQUIRE_TRUE( series.start( 3 ) );
    SW_REQUIRE_TRUE( series.reportRound( { 5, 5, 1 } ) );
    SW_EXPECT_EQUAL( 3, series.getTotal( 0 ) );
    SW_EXPECT_EQUAL( 3, series.getTotal( 1 ) );
    SW_EXPECT_EQUAL( 1, series.getTotal( 2 ) ); // 3 위
    SW_REQUIRE_TRUE( series.reportRound( { 5, 5, 1 } ) );
    SW_EXPECT_FALSE( series.isFinished() );
    SW_EXPECT_EQUAL( -1, series.getWinner() );
    SW_EXPECT_TRUE( series.getCurrentRound()->_id == hashed_string( "only" ) );
    SW_REQUIRE_TRUE( series.reportRound( { 1, 9, 1 } ) );
    SW_EXPECT_TRUE( series.isFinished() );
    SW_EXPECT_EQUAL( 1, series.getWinner() );

    PartyRoundSeries custom;
    custom.addRound( round );
    custom.setPlacementPoints( { 1 } );
    SW_EXPECT_FALSE( custom.setWinScore( 0 ) );
    SW_EXPECT_EQUAL( 5, custom.getWinScore() );
    SW_REQUIRE_TRUE( custom.setWinScore( 2 ) );
    SW_REQUIRE_TRUE( custom.start( 2 ) );
    SW_REQUIRE_TRUE( custom.reportRound( { 9, 1 } ) );
    SW_EXPECT_EQUAL( 1, custom.getTotal( 0 ) );
    SW_EXPECT_EQUAL( 0, custom.getTotal( 1 ) );
}

/**
 * @brief [PartyArenaTest] 시리즈 XML 은 끝까지 읽혀야 옮겨진다. 결함: 깨진 순위 점수 앞의 우승 점수가 바뀐 채 실패했고, 다시 읽으면 라운드가 뒤에 덧붙었다
 */
SW_TEST_CASE( PartyArenaTest, SeriesLoadsXMLAllOrNothing )
{
    SW_EXPECT_LOG( "Xml.AttributeTypeMismatch" );
    SW_EXPECT_LOG( "PartyRoundSeries.WinScoreNotNumber" );
    SW_EXPECT_LOG( "PartyRoundSeries.WinScoreNeedsMore" );
    SW_EXPECT_LOG( "PartyRoundSeries.PlacementPointsNeedsIntegersMore" );
    SW_EXPECT_LOG( "XMLCatalog.NoEntries" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    PartyRoundSeries series;
    SW_REQUIRE_TRUE( series.loadFromXMLText( R"(<PartySeries winScore="9" placementPoints="4,2"><Round id="a" time="60" scoreLimit="5"/><Round id="b"/><Round id="bad" time="-1"/><Thing/></PartySeries>)", "PartyArenaTest" ) );
    SW_EXPECT_EQUAL( 9, series.getWinScore() );
    SW_REQUIRE_EQUAL( size_t( 2 ), series.getPlacementPoints().size() );
    SW_EXPECT_EQUAL( 4, series.getPlacementPoints()[0] );
    SW_REQUIRE_EQUAL( size_t( 2 ), series.getRounds().size() );
    SW_EXPECT_NEAR( 60.0f, series.getRounds()[0]._timeLimit, kPartyTolerance );
    SW_EXPECT_EQUAL( 5, series.getRounds()[0]._scoreLimit );

    SW_EXPECT_FALSE( series.loadFromXMLText( R"(<PartySeries winScore="abc"><Round id="c"/></PartySeries>)", "PartyArenaTest" ) );
    SW_EXPECT_FALSE( series.loadFromXMLText( R"(<PartySeries winScore="0"><Round id="c"/></PartySeries>)", "PartyArenaTest" ) );
    SW_EXPECT_FALSE( series.loadFromXMLText( R"(<PartySeries winScore="3" placementPoints="1,x"><Round id="c"/></PartySeries>)", "PartyArenaTest" ) );
    SW_EXPECT_FALSE( series.loadFromXMLText( R"(<PartySeries winScore="3" placementPoints="7"/>)", "PartyArenaTest" ) );
    SW_EXPECT_EQUAL( 9, series.getWinScore() );
    SW_EXPECT_EQUAL( 4, series.getPlacementPoints()[0] );
    SW_EXPECT_EQUAL( size_t( 2 ), series.getRounds().size() );

    SW_REQUIRE_TRUE( series.loadFromXMLText( R"(<PartySeries><Round id="c"/></PartySeries>)", "PartyArenaTest" ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), series.getRounds().size() ); // 덧붙지 않는다
    SW_EXPECT_TRUE( series.getRounds()[0]._id == hashed_string( "c" ) );
    SW_EXPECT_EQUAL( 9, series.getWinScore() ); // 적지 않은 칸은 그대로다
}
