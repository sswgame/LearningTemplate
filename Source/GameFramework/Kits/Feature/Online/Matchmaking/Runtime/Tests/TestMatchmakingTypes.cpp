// MatchmakingTypesTest — 매칭 공유 타입의 순수 규칙: 모드 id 의 글자 · 길이 경계, 로비 준비 판정(방장 빼고 · 둘 이상), 티켓 평균 실력(빈 티켓 · 넘침 없음),
// 파티 · 로비 멤버 찾기. 대기열 · 로비 서비스는 Matchmaking/Server 테스트가 본다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingTypes.h"

using namespace sw;

namespace
{
    struct MatchmakingTypesTestUtil
    {
        static LobbyMember makeMember( AccountID accountID, bool bReady )
        {
            LobbyMember member;
            member._accountID = accountID;
            member._bReady    = bReady ? SW_TRUE : SW_FALSE;
            return member;
        }
    };
    using Util = MatchmakingTypesTestUtil;
} // namespace

/** @brief [MatchmakingTypesTest] 모드 id 는 `[0-9a-z_]` 1..상한이다('.' 도 거절) */
SW_TEST_CASE( MatchmakingTypesTest, ModeIDAcceptsOnlyItsAlphabetAndLength )
{
    // 틀리면: 글자 · 빈 글 · 상한 검사 중 하나를 빼면 해당 거짓 단언이, 상한을 하나 줄이면 상한 그대로인 참 단언이 진다.
    SW_EXPECT_TRUE( isValidMatchModeID( "ranked_5v5" ) );
    SW_EXPECT_TRUE( isValidMatchModeID( string( static_cast<size_t>( MatchmakingLimit::kMaxIDSize ), 'm' ) ) );
    SW_EXPECT_FALSE( isValidMatchModeID( string( static_cast<size_t>( MatchmakingLimit::kMaxIDSize ) + 1, 'm' ) ) );
    SW_EXPECT_FALSE( isValidMatchModeID( "" ) );
    SW_EXPECT_FALSE( isValidMatchModeID( "Ranked" ) );
    SW_EXPECT_FALSE( isValidMatchModeID( "ranked.5v5" ) );
}

/** @brief [MatchmakingTypesTest] 로비는 둘 이상이고 방장을 뺀 모두가 준비해야 준비됐다 — 방장의 준비 표시는 보지 않는다 */
SW_TEST_CASE( MatchmakingTypesTest, LobbyIsReadyWhenEveryGuestIsReady )
{
    // 틀리면: 혼자인 로비를 준비로 보면 첫 거짓 단언이, 방장까지 보면 "방장 미준비" 참 단언이, 손님 하나를 놓치면 마지막 거짓 단언이 진다.
    LobbySnapshot lobby;
    lobby._listMember.push_back( Util::makeMember( 1, true ) );
    SW_EXPECT_FALSE( lobby.isEveryoneReady() );

    lobby._listMember[0]._bReady = SW_FALSE;
    lobby._listMember.push_back( Util::makeMember( 2, true ) );
    lobby._listMember.push_back( Util::makeMember( 3, true ) );
    SW_EXPECT_TRUE( lobby.isEveryoneReady() );
    SW_EXPECT_TRUE( lobby.hasMember( 3 ) );
    SW_EXPECT_FALSE( lobby.hasMember( 4 ) );
    SW_EXPECT_EQUAL( AccountID( 1 ), lobby.getOwnerID() );

    lobby._listMember[2]._bReady = SW_FALSE;
    SW_EXPECT_FALSE( lobby.isEveryoneReady() );
}

/** @brief [MatchmakingTypesTest] 평균 실력은 빈 티켓이면 0 이고, 큰 실력의 합이 int32 를 넘어도 맞다 */
SW_TEST_CASE( MatchmakingTypesTest, AverageRatingHandlesEmptyAndLargeTickets )
{
    // 틀리면: 빈 티켓에서 0 으로 나누면 멈추고, int32 로 더하면 넘쳐 큰 실력 단언이 진다.
    MatchTicket ticket;
    SW_EXPECT_EQUAL( 0, ticket.computeAverageRating() );

    MatchMember strong;
    strong._rating = 2000000000;
    ticket._listMember.push_back( strong );
    ticket._listMember.push_back( strong );
    SW_EXPECT_EQUAL( 2000000000, ticket.computeAverageRating() );

    MatchMember weak;
    weak._rating = 0;
    ticket._listMember.push_back( weak );
    SW_EXPECT_EQUAL( 1333333333, ticket.computeAverageRating() ); // 소수는 버린다

    PartySnapshot party;
    party._listMemberID = { 7, 8 };
    SW_EXPECT_TRUE( party.hasMember( 8 ) );
    SW_EXPECT_FALSE( party.hasMember( 9 ) );
    SW_EXPECT_EQUAL( AccountID( 7 ), party.getLeaderID() );
}
