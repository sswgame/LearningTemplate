// MatchMakerTest — 매치메이커 규칙: 실력 창은 기다린 초만큼 넓어지고 상한에서 멈춘다, 실력 차가 창 밖이면 묶지 않는다, 지역은 완화 시간 전에는 따로다,
// 시한을 넘긴 티켓은 돌려준다, 파티 인원 · 중복 티켓 거절, 팀 균형(실력 합이 낮은 팀부터), 백필은 한 팀의 빈 슬롯에 파티째 들어간다, 짧은 모드 글 읽기(nogpu).
// 서버 대상 키트라 `UnitTests.Server`(서버 실행 파일)가 돌린다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/MatchMaker.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/MatchModeCatalog.h"

using namespace sw;

namespace
{
    MatchModeDef makeMode( int32 teamCount, int32 teamSize )
    {
        MatchModeDef definition;
        definition._modeID                = "duel";
        definition._teamCount             = teamCount;
        definition._teamSize              = teamSize;
        definition._baseRatingWindow      = 100;
        definition._ratingWindowPerSecond = 25;
        definition._maxRatingWindow       = 300;
        definition._regionRelaxMs         = 10000;
        definition._maxWaitMs             = 60000;
        return definition;
    }

    MatchTicket makeTicket( uint64 ticketID, int32 rating, const utf8* pRegion, int64 enqueuedMs, int32 memberCount = 1 )
    {
        MatchTicket ticket;
        ticket._ticketID   = ticketID;
        ticket._region     = pRegion;
        ticket._enqueuedMs = enqueuedMs;
        for ( int32 index = 0; index < memberCount; ++index )
            ticket._listMember.push_back( MatchMember{ ticketID * 10 + static_cast<uint64>( index ), rating } );
        return ticket;
    }

    MatchMaker makeMaker( int32 teamCount, int32 teamSize )
    {
        MatchMaker maker;
        SW_EXPECT_TRUE( maker.initialize( makeMode( teamCount, teamSize ), 7 ) );
        return maker;
    }
} // namespace

/** @brief [MatchMakerTest] 실력 창은 기본값에 기다린 초 × 초당 넓힘을 더하고, 상한을 넘지 않는다(초 단위로 내린다) */
SW_TEST_CASE( MatchMakerTest, RatingWindowWidensPerSecondAndStopsAtTheCap )
{
    const MatchMaker  maker  = makeMaker( 2, 1 );
    const MatchTicket ticket = makeTicket( 1, 1500, "eu", 0 );
    SW_EXPECT_EQUAL( 100, maker.computeRatingWindow( ticket, 0 ) );
    SW_EXPECT_EQUAL( 100, maker.computeRatingWindow( ticket, 999 ) );
    SW_EXPECT_EQUAL( 200, maker.computeRatingWindow( ticket, 4000 ) );
    SW_EXPECT_EQUAL( 300, maker.computeRatingWindow( ticket, 1000000 ) );
}

/** @brief [MatchMakerTest] 창 안의 두 티켓은 한 경기로 묶이고, 창 밖의 티켓은 남는다 — 기다리면 창이 넓어져 묶인다 */
SW_TEST_CASE( MatchMakerTest, TicketsWithinTheWindowFormAMatch )
{
    MatchMaker maker = makeMaker( 2, 1 );
    SW_REQUIRE_TRUE( maker.addTicket( makeTicket( 1, 1500, "eu", 0 ) ) == MatchmakingResult::Ok );
    SW_REQUIRE_TRUE( maker.addTicket( makeTicket( 2, 1560, "eu", 0 ) ) == MatchmakingResult::Ok );
    SW_REQUIRE_TRUE( maker.addTicket( makeTicket( 3, 1900, "eu", 0 ) ) == MatchmakingResult::Ok );

    vector<MatchFormed> listMatch;
    vector<MatchTicket> listTimedOut;
    maker.process( 0, listMatch, listTimedOut );
    SW_REQUIRE_EQUAL( size_t( 1 ), listMatch.size() );
    SW_EXPECT_EQUAL( size_t( 2 ), listMatch[0]._listTeam.size() );
    SW_EXPECT_EQUAL( string( "eu" ), listMatch[0]._region );
    SW_EXPECT_TRUE( listMatch[0]._matchID != 0 );
    SW_EXPECT_TRUE( maker.hasTicket( 3 ) );
    SW_EXPECT_FALSE( maker.hasTicket( 1 ) || maker.hasTicket( 2 ) );
    SW_EXPECT_TRUE( listTimedOut.empty() );
}

/** @brief [MatchMakerTest] 지역이 다른 티켓은 완화 시간 전에는 묶지 않고, 넘긴 뒤에는 앵커의 지역으로 묶는다 */
SW_TEST_CASE( MatchMakerTest, RegionsStayApartUntilTheRelaxTime )
{
    MatchMaker maker = makeMaker( 2, 1 );
    SW_REQUIRE_TRUE( maker.addTicket( makeTicket( 1, 1500, "eu", 0 ) ) == MatchmakingResult::Ok );
    SW_REQUIRE_TRUE( maker.addTicket( makeTicket( 2, 1500, "us", 1000 ) ) == MatchmakingResult::Ok );

    vector<MatchFormed> listMatch;
    vector<MatchTicket> listTimedOut;
    maker.process( 5000, listMatch, listTimedOut );
    SW_EXPECT_TRUE( listMatch.empty() );
    maker.process( 10000, listMatch, listTimedOut );
    SW_REQUIRE_EQUAL( size_t( 1 ), listMatch.size() );
    SW_EXPECT_EQUAL( string( "eu" ), listMatch[0]._region ); // 가장 오래 기다린 티켓이 앵커다
}

/** @brief [MatchMakerTest] 시한을 넘긴 티켓은 빼서 돌려준다 */
SW_TEST_CASE( MatchMakerTest, TicketsPastTheMaxWaitAreReturned )
{
    MatchMaker maker = makeMaker( 2, 1 );
    SW_REQUIRE_TRUE( maker.addTicket( makeTicket( 1, 1500, "eu", 0 ) ) == MatchmakingResult::Ok );
    vector<MatchFormed> listMatch;
    vector<MatchTicket> listTimedOut;
    maker.process( 60001, listMatch, listTimedOut );
    SW_EXPECT_TRUE( listMatch.empty() );
    SW_REQUIRE_EQUAL( size_t( 1 ), listTimedOut.size() );
    SW_EXPECT_EQUAL( uint64( 1 ), listTimedOut[0]._ticketID );
    SW_EXPECT_EQUAL( 0, maker.getTicketCount() );
}

/** @brief [MatchMakerTest] 팀 인원을 넘는 파티 · 빈 티켓 · 같은 id 는 넣지 않는다 */
SW_TEST_CASE( MatchMakerTest, OversizedEmptyAndDuplicateTicketsAreRejected )
{
    MatchMaker maker = makeMaker( 2, 2 );
    SW_EXPECT_TRUE( maker.addTicket( makeTicket( 1, 1500, "eu", 0, 3 ) ) == MatchmakingResult::PartyTooLarge );
    SW_EXPECT_TRUE( maker.addTicket( makeTicket( 2, 1500, "eu", 0, 0 ) ) == MatchmakingResult::PartyTooLarge );
    SW_REQUIRE_TRUE( maker.addTicket( makeTicket( 3, 1500, "eu", 0, 2 ) ) == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( maker.addTicket( makeTicket( 3, 1500, "eu", 0, 1 ) ) == MatchmakingResult::AlreadyQueued );
    SW_EXPECT_EQUAL( 1, maker.getTicketCount() );
    SW_EXPECT_TRUE( maker.removeTicket( 3 ) );
    SW_EXPECT_FALSE( maker.removeTicket( 3 ) );
}

/** @brief [MatchMakerTest] 파티는 쪼개지 않고 한 팀에 들어가며, 나머지는 실력 합이 낮은 팀을 채운다 */
SW_TEST_CASE( MatchMakerTest, PartiesStayTogetherAndTeamsBalance )
{
    MatchMaker maker = makeMaker( 2, 2 );
    SW_REQUIRE_TRUE( maker.addTicket( makeTicket( 1, 1500, "eu", 0, 2 ) ) == MatchmakingResult::Ok );
    SW_REQUIRE_TRUE( maker.addTicket( makeTicket( 2, 1520, "eu", 0 ) ) == MatchmakingResult::Ok );
    SW_REQUIRE_TRUE( maker.addTicket( makeTicket( 3, 1480, "eu", 0 ) ) == MatchmakingResult::Ok );

    vector<MatchFormed> listMatch;
    vector<MatchTicket> listTimedOut;
    maker.process( 0, listMatch, listTimedOut );
    SW_REQUIRE_EQUAL( size_t( 1 ), listMatch.size() );
    const MatchFormed& match = listMatch[0];
    SW_REQUIRE_EQUAL( size_t( 2 ), match._listTeam.size() );
    SW_EXPECT_EQUAL( size_t( 2 ), match._listTeam[0]._listMember.size() );
    SW_EXPECT_EQUAL( size_t( 2 ), match._listTeam[1]._listMember.size() );
    // 파티(10 · 11)는 한 팀이다
    for ( const MatchFormedTeam& team : match._listTeam )
    {
        const bool bHasFirst  = team._listMember[0]._accountID == 10 || team._listMember[1]._accountID == 10;
        const bool bHasSecond = team._listMember[0]._accountID == 11 || team._listMember[1]._accountID == 11;
        SW_EXPECT_EQUAL( bHasFirst, bHasSecond );
    }
}

/** @brief [MatchMakerTest] 백필은 한 팀의 빈 슬롯에 들어가는 티켓으로 채우고, 다 찬 요청은 지운다 */
SW_TEST_CASE( MatchMakerTest, BackfillFillsOneTeamsOpenSlot )
{
    MatchMaker           maker = makeMaker( 2, 2 );
    MatchBackfillRequest request;
    request._modeID        = "duel";
    request._region        = "eu";
    request._matchID       = 900;
    request._listOpenSlot  = { 1, 0 };
    request._averageRating = 1500;
    SW_REQUIRE_TRUE( maker.addBackfill( request ) == MatchmakingResult::Ok );
    MatchBackfillRequest wrongMode = request;
    wrongMode._modeID              = "other";
    SW_EXPECT_TRUE( maker.addBackfill( wrongMode ).hasError( OnlineError::kInvalidRequest ) );
    SW_REQUIRE_TRUE( maker.addTicket( makeTicket( 1, 1520, "eu", 0, 2 ) ) == MatchmakingResult::Ok ); // 빈 슬롯 하나에 둘은 안 들어간다
    SW_REQUIRE_TRUE( maker.addTicket( makeTicket( 2, 1510, "eu", 0 ) ) == MatchmakingResult::Ok );

    vector<MatchBackfilled> listBackfilled;
    maker.processBackfills( 0, listBackfilled );
    SW_REQUIRE_EQUAL( size_t( 1 ), listBackfilled.size() );
    SW_EXPECT_EQUAL( uint64( 2 ), listBackfilled[0]._ticket._ticketID );
    SW_EXPECT_EQUAL( 0, listBackfilled[0]._team );
    SW_EXPECT_EQUAL( 0, maker.getBackfillCount() );
    SW_EXPECT_TRUE( maker.hasTicket( 1 ) );
}
