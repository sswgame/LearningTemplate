#include "pch.h"

#include "GameFramework/Kits/Online/Server/Matchmaking/MatchMaker.h"

#include "TestFramework/TestFramework.h"

#include <cstdlib>

// 매처 — 혼자 넷이 균형 잡힌 2v2, 파티는 한 팀(나눌 수 없는 조합은 건너뜀), 실력 차가 크면 기다리다 창이 넓어져 묶임, 지역은 기다린 뒤 풀림,
// 시한, 같은 입력은 같은 결과(결정적), 표 넣기 · 빼기의 결과 코드.

using namespace sw;

namespace
{
    MatchTicket makeTicket( uint64 ticketId, const vector<int32>& listRating, string_view region = "kr", int64 enqueuedMs = 0 )
    {
        MatchTicket ticket;
        ticket._ticketId   = ticketId;
        ticket._region     = string( region );
        ticket._enqueuedMs = enqueuedMs;
        for ( size_t index = 0; index < listRating.size(); ++index )
        {
            ticket._listMember.push_back( MatchMember{ ticketId * 10 + index, listRating[index] } );
        }
        return ticket;
    }

    MatchModeDefinition makeMode( int32 teamCount, int32 teamSize )
    {
        MatchModeDefinition definition;
        definition._modeId    = "duo";
        definition._teamCount = teamCount;
        definition._teamSize  = teamSize;
        return definition;
    }

    int32 findTeamOf( const MatchFormed& match, AccountId accountId )
    {
        for ( int32 team = 0; team < static_cast<int32>( match._listTeam.size() ); ++team )
        {
            for ( const MatchMember& member : match._listTeam[static_cast<size_t>( team )] )
            {
                if ( member._accountId == accountId )
                    return team;
            }
        }
        return -1;
    }
} // namespace

SW_TEST_CASE( MatchMakerTest, FourSolosMakeABalancedTwoVersusTwo )
{
    MatchMaker maker;
    maker.initialize( makeMode( 2, 2 ), 7 );
    for ( uint64 ticketId = 1; ticketId <= 4; ++ticketId )
    {
        SW_ASSERT_TRUE( maker.addTicket( makeTicket( ticketId, { static_cast<int32>( 1450 + ticketId * 20 ) } ) ) == MatchmakingResult::Ok );
    }
    vector<MatchFormed> listMatch;
    vector<MatchTicket> listTimedOut;
    maker.process( 0, listMatch, listTimedOut );
    SW_ASSERT_EQUAL( listMatch.size(), size_t( 1 ) );
    SW_ASSERT_EQUAL( listMatch[0]._listTeam.size(), size_t( 2 ) );
    SW_ASSERT_EQUAL( listMatch[0]._listTeam[0].size(), size_t( 2 ) );
    SW_ASSERT_EQUAL( listMatch[0]._listTeam[1].size(), size_t( 2 ) );
    const int32 teamA = listMatch[0]._listTeam[0][0]._rating + listMatch[0]._listTeam[0][1]._rating;
    const int32 teamB = listMatch[0]._listTeam[1][0]._rating + listMatch[0]._listTeam[1][1]._rating;
    SW_EXPECT_TRUE( std::abs( teamA - teamB ) <= 40 );
    SW_EXPECT_EQUAL( listMatch[0]._matchId >> 32, uint64( 7 ) );
    SW_EXPECT_EQUAL( listMatch[0]._averageRating, 1500 );
    SW_EXPECT_STREQ( listMatch[0]._modeId.c_str(), "duo" );
    SW_EXPECT_EQUAL( maker.getTicketCount(), 0 );
    SW_EXPECT_EQUAL( listTimedOut.size(), size_t( 0 ) );
}

SW_TEST_CASE( MatchMakerTest, PartyStaysTogetherAndUnsplittableMixIsSkipped )
{
    MatchMaker maker;
    maker.initialize( makeMode( 2, 3 ), 1 );
    SW_ASSERT_TRUE( maker.addTicket( makeTicket( 1, { 1500, 1500 } ) ) == MatchmakingResult::Ok ); // 파티 셋
    SW_ASSERT_TRUE( maker.addTicket( makeTicket( 2, { 1500, 1500 } ) ) == MatchmakingResult::Ok );
    SW_ASSERT_TRUE( maker.addTicket( makeTicket( 3, { 1500, 1500 } ) ) == MatchmakingResult::Ok ); // 2+2+2 = 6 자리지만 3 인 팀 둘로 못 나눈다
    vector<MatchFormed> listMatch;
    vector<MatchTicket> listTimedOut;
    maker.process( 0, listMatch, listTimedOut );
    SW_EXPECT_EQUAL( listMatch.size(), size_t( 0 ) );
    SW_EXPECT_EQUAL( maker.getTicketCount(), 3 );

    SW_ASSERT_TRUE( maker.addTicket( makeTicket( 4, { 1500 } ) ) == MatchmakingResult::Ok );
    SW_ASSERT_TRUE( maker.addTicket( makeTicket( 5, { 1500 } ) ) == MatchmakingResult::Ok );
    maker.process( 1000, listMatch, listTimedOut );
    SW_ASSERT_EQUAL( listMatch.size(), size_t( 1 ) ); // 2+1 · 2+1 — 파티 하나는 남는다
    int32 partyCount = 0;
    for ( const MatchTicket& ticket : listMatch[0]._listTicket )
    {
        if ( ticket._listMember.size() != 2 )
            continue;
        ++partyCount;
        const int32 teamOfFirst = findTeamOf( listMatch[0], ticket._listMember[0]._accountId );
        SW_EXPECT_TRUE( teamOfFirst >= 0 );
        SW_EXPECT_EQUAL( teamOfFirst, findTeamOf( listMatch[0], ticket._listMember[1]._accountId ) );
    }
    SW_EXPECT_EQUAL( partyCount, 2 );
    SW_EXPECT_EQUAL( maker.getTicketCount(), 1 );
    SW_EXPECT_TRUE( maker.addTicket( makeTicket( 9, { 1, 2, 3, 4 } ) ) == MatchmakingResult::PartyTooLarge );
    SW_EXPECT_TRUE( maker.addTicket( makeTicket( 8, {} ) ) == MatchmakingResult::PartyTooLarge );

    // 가장 오래 기다린 혼자(닻)가 혼자 둘 + 셋 파티를 뽑으면 셋 파티가 한 팀을 통째로 가져가야 한다 — 작은 표부터 나누면 셋 파티가 들 팀이 없어
    // 그 닻이 실패하고, 파티를 닻으로 한 다른 조합(늦게 온 혼자 5 를 넣고 오래 기다린 혼자 3 을 남김)이 대신 묶인다.
    MatchMaker stackMaker;
    stackMaker.initialize( makeMode( 2, 3 ), 1 );
    for ( uint64 ticketId = 1; ticketId <= 3; ++ticketId )
    {
        SW_ASSERT_TRUE( stackMaker.addTicket( makeTicket( ticketId, { 1500 }, "kr", static_cast<int64>( ticketId ) ) ) == MatchmakingResult::Ok );
    }
    SW_ASSERT_TRUE( stackMaker.addTicket( makeTicket( 4, { 1520, 1520, 1520 }, "kr", 4 ) ) == MatchmakingResult::Ok );
    SW_ASSERT_TRUE( stackMaker.addTicket( makeTicket( 5, { 1520 }, "kr", 5 ) ) == MatchmakingResult::Ok );
    vector<MatchFormed> listStackMatch;
    stackMaker.process( 10, listStackMatch, listTimedOut );
    SW_ASSERT_EQUAL( listStackMatch.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( stackMaker.hasTicket( 5 ) ); // 닻(혼자 1) · 혼자 2 · 혼자 3 · 셋 파티
    SW_EXPECT_FALSE( stackMaker.hasTicket( 3 ) );
    const int32 stackTeam = findTeamOf( listStackMatch[0], 40 );
    SW_EXPECT_EQUAL( findTeamOf( listStackMatch[0], 41 ), stackTeam );
    SW_EXPECT_EQUAL( findTeamOf( listStackMatch[0], 42 ), stackTeam );
}

SW_TEST_CASE( MatchMakerTest, WindowWidensAndRegionRelaxesOverTime )
{
    MatchModeDefinition mode    = makeMode( 2, 1 );
    mode._baseRatingWindow      = 100;
    mode._ratingWindowPerSecond = 50;
    mode._regionRelaxMs         = 20000;
    MatchMaker maker;
    maker.initialize( mode, 1 );
    SW_ASSERT_TRUE( maker.addTicket( makeTicket( 1, { 1500 }, "kr", 0 ) ) == MatchmakingResult::Ok );
    SW_ASSERT_TRUE( maker.addTicket( makeTicket( 2, { 1900 }, "kr", 0 ) ) == MatchmakingResult::Ok ); // 차 400
    SW_EXPECT_EQUAL( maker.computeRatingWindow( makeTicket( 1, { 1500 }, "kr", 0 ), 5000 ), 350 );
    vector<MatchFormed> listMatch;
    vector<MatchTicket> listTimedOut;
    maker.process( 5000, listMatch, listTimedOut ); // 창 100 + 5 × 50 = 350
    SW_EXPECT_EQUAL( listMatch.size(), size_t( 0 ) );
    maker.process( 6000, listMatch, listTimedOut ); // 400
    SW_EXPECT_EQUAL( listMatch.size(), size_t( 1 ) );

    SW_ASSERT_TRUE( maker.addTicket( makeTicket( 3, { 1500 }, "kr", 10000 ) ) == MatchmakingResult::Ok );
    SW_ASSERT_TRUE( maker.addTicket( makeTicket( 4, { 1500 }, "eu", 10000 ) ) == MatchmakingResult::Ok );
    maker.process( 20000, listMatch, listTimedOut ); // 10 초 — 지역이 다르다
    SW_EXPECT_EQUAL( listMatch.size(), size_t( 1 ) );
    maker.process( 30000, listMatch, listTimedOut ); // 20 초 — 풀림
    SW_ASSERT_EQUAL( listMatch.size(), size_t( 2 ) );
    SW_EXPECT_STREQ( listMatch[1]._region.c_str(), "kr" ); // 닻(먼저 넣은 표)의 지역
}

SW_TEST_CASE( MatchMakerTest, TimeoutAndDeterminism )
{
    MatchModeDefinition mode = makeMode( 2, 2 );
    mode._maxWaitMs          = 60000;
    vector<MatchFormed> firstRun;
    vector<MatchFormed> secondRun;
    for ( vector<MatchFormed>* pOut : { &firstRun, &secondRun } )
    {
        MatchMaker maker;
        maker.initialize( mode, 3 );
        for ( uint64 ticketId = 1; ticketId <= 9; ++ticketId )
        {
            SW_ASSERT_TRUE( maker.addTicket( makeTicket( ticketId, { static_cast<int32>( 1000 + ( ticketId * 37 ) % 400 ) }, "kr", static_cast<int64>( ticketId ) ) ) == MatchmakingResult::Ok );
        }
        vector<MatchTicket> listTimedOut;
        maker.process( 20000, *pOut, listTimedOut );  // 창 100 + 19 × 25 = 575 — 실력 1037..1333 이 모두 든다
        maker.process( 90000, *pOut, listTimedOut );  // 남은 하나는 60 초를 넘겼다
        SW_EXPECT_EQUAL( pOut->size(), size_t( 2 ) ); // 아홉 중 여덟이 두 경기
        SW_ASSERT_EQUAL( listTimedOut.size(), size_t( 1 ) );
        SW_EXPECT_EQUAL( maker.getTicketCount(), 0 );
    }
    SW_ASSERT_EQUAL( firstRun.size(), secondRun.size() );
    for ( size_t index = 0; index < firstRun.size(); ++index )
    {
        SW_EXPECT_EQUAL( firstRun[index]._matchId, secondRun[index]._matchId );
        SW_EXPECT_EQUAL( firstRun[index]._listTeam[0][0]._accountId, secondRun[index]._listTeam[0][0]._accountId );
        SW_EXPECT_EQUAL( firstRun[index]._listTeam[1][1]._accountId, secondRun[index]._listTeam[1][1]._accountId );
    }

    MatchMaker maker;
    maker.initialize( mode, 3 );
    SW_EXPECT_TRUE( maker.addTicket( makeTicket( 1, { 1500 } ) ) == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( maker.addTicket( makeTicket( 1, { 1500 } ) ) == MatchmakingResult::AlreadyQueued );
    SW_EXPECT_TRUE( maker.hasTicket( 1 ) );
    SW_EXPECT_TRUE( maker.removeTicket( 1 ) );
    SW_EXPECT_FALSE( maker.removeTicket( 1 ) );
}
