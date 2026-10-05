#include "pch.h"

#include "GameFramework/Match/MatchState.h"
#include "GameFramework/Match/TeamAttitude.h"

#include "TestFramework/TestFramework.h"

// 장르 공통 판 규칙 — 팀 데스매치(점수 · 도움 · 시간 제한), 코스트 게이지 부활(기체 대전), 부활 없는 생존전 순위(배틀로얄), 목표로 끝내기(비대칭 대전).

using namespace sw;

SW_TEST_CASE( MatchTest, DeathmatchScoresKillsAssistsAndEndsOnLimits )
{
    MatchSettings settings;
    settings._warmupTime   = 3.0f;
    settings._timeLimit    = 60.0f;
    settings._scoreLimit   = 3;
    settings._respawnDelay = 2.0f;
    MatchState match;
    match.initialize( settings );
    const int32 red   = match.addTeam( hashed_string( "Red" ) );
    const int32 blue  = match.addTeam( hashed_string( "Blue" ) );
    const int32 redA  = match.addParticipant( red, hashed_string( "Striker" ) );
    const int32 redB  = match.addParticipant( red, hashed_string( "Striker" ) );
    const int32 blueA = match.addParticipant( blue, hashed_string( "Striker" ) );
    match.start();
    SW_EXPECT_TRUE( match.getPhase() == MatchPhase::Warmup );
    match.reportKill( blueA, redA ); // 몸풀기 중에는 세지 않는다
    SW_EXPECT_EQUAL( 0, match.findTeam( red )->_score );
    match.update( 3.0f );
    SW_EXPECT_TRUE( match.getPhase() == MatchPhase::InProgress );

    match.reportDamage( redB, blueA, 30.0f );
    match.reportDamage( redA, blueA, 70.0f );
    match.reportKill( blueA, redA );
    SW_EXPECT_EQUAL( 1, match.findTeam( red )->_score );
    SW_EXPECT_EQUAL( 1, match.findParticipant( redA )->_kills );
    SW_EXPECT_EQUAL( 1, match.findParticipant( redB )->_assists );
    SW_EXPECT_FALSE( match.findParticipant( blueA )->_bAlive );
    match.update( 1.0f );
    SW_EXPECT_FALSE( match.findParticipant( blueA )->_bAlive );
    match.update( 1.1f );
    SW_EXPECT_TRUE( match.findParticipant( blueA )->_bAlive != SW_FALSE );

    // 도움 창은 10 초 — 오래된 피해는 도움이 아니다.
    match.reportDamage( redB, blueA, 10.0f );
    match.update( 11.0f );
    match.reportKill( blueA, redA );
    SW_EXPECT_EQUAL( 1, match.findParticipant( redB )->_assists );
    match.update( 2.1f );
    match.reportKill( blueA, -1 ); // 낙사 — 점수 없음
    SW_EXPECT_EQUAL( 2, match.findTeam( red )->_score );
    match.update( 2.1f );
    match.reportKill( blueA, redB );
    SW_EXPECT_TRUE( match.getPhase() == MatchPhase::Ended );
    SW_EXPECT_EQUAL( red, match.getWinningTeam() );

    // 시간 제한 — 점수가 같으면 무승부.
    MatchState draw;
    settings._scoreLimit = 0;
    settings._warmupTime = 0.0f;
    draw.initialize( settings );
    (void)draw.addTeam( hashed_string( "A" ) );
    (void)draw.addTeam( hashed_string( "B" ) );
    draw.start();
    draw.update( 30.0f );
    SW_EXPECT_NEAR_EQUAL( 30.0f, draw.getRemaining(), 1.0e-3f );
    draw.update( 31.0f );
    SW_EXPECT_TRUE( draw.getPhase() == MatchPhase::Ended );
    SW_EXPECT_EQUAL( -1, draw.getWinningTeam() );
}

SW_TEST_CASE( MatchTest, CostGaugesEliminationPlacementsAndObjectiveEnds )
{
    // 기체 대전 — 게이지 1000, 코스트 400 기체가 세 번 죽으면 진다.
    MatchSettings settings;
    settings._respawnDelay = 1.0f;
    settings._scorePerKill = 0;
    MatchState mech;
    mech.initialize( settings );
    const int32 federation = mech.addTeam( hashed_string( "Federation" ), 1000 );
    const int32 zeon       = mech.addTeam( hashed_string( "Zeon" ), 1000 );
    const int32 gundam     = mech.addParticipant( federation, hashed_string( "Near" ), 400 );
    const int32 zaku       = mech.addParticipant( zeon, hashed_string( "Mid" ), 200 );
    mech.start();
    for ( int32 index = 0; index < 2; ++index )
    {
        mech.reportKill( gundam, zaku );
        mech.update( 1.1f );
    }
    SW_EXPECT_EQUAL( 200, mech.findTeam( federation )->_costPool );
    SW_EXPECT_TRUE( mech.getPhase() == MatchPhase::InProgress );
    mech.reportKill( gundam, zaku );
    SW_EXPECT_TRUE( mech.findTeam( federation )->_bEliminated != SW_FALSE );
    SW_EXPECT_EQUAL( zeon, mech.getWinningTeam() );

    // 배틀로얄 — 부활 없음, 떨어진 순서대로 순위.
    MatchSettings royaleSettings;
    royaleSettings._bRespawn              = SW_FALSE;
    royaleSettings._bLastTeamStandingWins = SW_TRUE;
    MatchState royale;
    royale.initialize( royaleSettings );
    vector<int32> listMember;
    for ( int32 team = 0; team < 4; ++team )
    {
        (void)royale.addTeam( hashed_string( "Squad" ) );
        listMember.push_back( royale.addParticipant( team, hashed_string( "Player" ) ) );
        listMember.push_back( royale.addParticipant( team, hashed_string( "Player" ) ) );
    }
    royale.start();
    royale.reportKill( listMember[0], listMember[2] );
    SW_EXPECT_FALSE( royale.findTeam( 0 )->_bEliminated );
    SW_EXPECT_EQUAL( 1, royale.countStanding( 0 ) );
    royale.reportKill( listMember[1], -1 ); // 자기장
    SW_EXPECT_EQUAL( 4, royale.findTeam( 0 )->_placement );
    royale.eliminate( listMember[6] );
    royale.eliminate( listMember[7] );
    SW_EXPECT_EQUAL( 3, royale.findTeam( 3 )->_placement );
    royale.reportKill( listMember[4], listMember[2] );
    royale.reportKill( listMember[5], listMember[3] );
    SW_EXPECT_TRUE( royale.getPhase() == MatchPhase::Ended );
    SW_EXPECT_EQUAL( 1, royale.getWinningTeam() );
    SW_EXPECT_EQUAL( 1, royale.findTeam( 1 )->_placement );
    SW_EXPECT_EQUAL( 2, royale.findTeam( 2 )->_placement );
    SW_EXPECT_EQUAL( 2, royale.findParticipant( listMember[2] )->_kills );

    // 비대칭 대전 — 목표(탈출)를 이루면 게임이 끝낸다.
    MatchState asymmetric;
    asymmetric.initialize( MatchSettings{} );
    const int32 killers   = asymmetric.addTeam( hashed_string( "Killer" ) );
    const int32 survivors = asymmetric.addTeam( hashed_string( "Survivors" ) );
    (void)asymmetric.addParticipant( killers, hashed_string( "Killer" ) );
    (void)asymmetric.addParticipant( survivors, hashed_string( "Survivor" ) );
    asymmetric.start();
    asymmetric.endMatch( survivors );
    SW_EXPECT_EQUAL( survivors, asymmetric.getWinningTeam() );
    vector<MatchEvent> listEvent;
    asymmetric.drainEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.back()._kind == MatchEvent::Kind::MatchEnded );
}

SW_TEST_CASE( MatchTest, TeamKillScoresNothingAndTeammateDamageIsNoAssist )
{
    MatchSettings settings; // 몸풀기 0 — start 하면 바로 진행
    MatchState    match;
    match.initialize( settings );
    const int32 red   = match.addTeam( hashed_string( "Red" ) );
    const int32 blue  = match.addTeam( hashed_string( "Blue" ) );
    const int32 redA  = match.addParticipant( red, hashed_string( "Striker" ) );
    const int32 redB  = match.addParticipant( red, hashed_string( "Striker" ) );
    const int32 blueA = match.addParticipant( blue, hashed_string( "Striker" ) );
    const int32 blueB = match.addParticipant( blue, hashed_string( "Striker" ) );
    match.start();
    SW_ASSERT_TRUE( match.getPhase() == MatchPhase::InProgress );

    // 죽은 쪽과 같은 팀이 준 피해는 도움이 아니다 — 적이 준 피해만 도움이다.
    match.reportDamage( blueB, blueA, 10.0f );
    match.reportDamage( redB, blueA, 10.0f );
    match.reportKill( blueA, redA );
    SW_EXPECT_EQUAL( 1, match.findParticipant( redA )->_kills );
    SW_EXPECT_EQUAL( 1, match.findTeam( red )->_score );
    SW_EXPECT_EQUAL( 1, match.findParticipant( redB )->_assists );
    SW_EXPECT_EQUAL( 0, match.findParticipant( blueB )->_assists );

    // 같은 팀을 죽이면 처치 · 점수가 없다(죽음은 센다).
    match.reportKill( redB, redA );
    SW_EXPECT_EQUAL( 1, match.findParticipant( redA )->_kills );
    SW_EXPECT_EQUAL( 1, match.findTeam( red )->_score );
    SW_EXPECT_EQUAL( 1, match.findParticipant( redB )->_deaths );
}

SW_TEST_CASE( MatchTest, TeamAttitudeIsFriendlyHostileOrNeutralWithoutTeam )
{
    static_assert( TeamAttitudeUtil::isHostile( 0, 1 ), "TeamAttitudeUtil must stay usable in constant expressions" );
    SW_EXPECT_TRUE( TeamAttitudeUtil::computeAttitude( 0, 0 ) == TeamAttitude::Friendly );
    SW_EXPECT_TRUE( TeamAttitudeUtil::computeAttitude( 0, 1 ) == TeamAttitude::Hostile );
    SW_EXPECT_TRUE( TeamAttitudeUtil::computeAttitude( TeamAttitudeUtil::kNoTeam, 0 ) == TeamAttitude::Neutral );
    SW_EXPECT_TRUE( TeamAttitudeUtil::computeAttitude( 1, TeamAttitudeUtil::kNoTeam ) == TeamAttitude::Neutral );
    SW_EXPECT_TRUE( TeamAttitudeUtil::computeAttitude( TeamAttitudeUtil::kNoTeam, TeamAttitudeUtil::kNoTeam ) == TeamAttitude::Neutral );
    SW_EXPECT_TRUE( TeamAttitudeUtil::isHostile( 2, 3 ) );
    SW_EXPECT_FALSE( TeamAttitudeUtil::isHostile( 2, 2 ) );
    SW_EXPECT_FALSE( TeamAttitudeUtil::isHostile( TeamAttitudeUtil::kNoTeam, 2 ) );
    SW_EXPECT_TRUE( TeamAttitudeUtil::isFriendly( 2, 2 ) );
    SW_EXPECT_FALSE( TeamAttitudeUtil::isFriendly( 2, 3 ) );
    SW_EXPECT_FALSE( TeamAttitudeUtil::isFriendly( TeamAttitudeUtil::kNoTeam, TeamAttitudeUtil::kNoTeam ) );
}
