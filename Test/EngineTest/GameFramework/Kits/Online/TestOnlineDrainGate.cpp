// 꺼내기 권한 표(DrainGate) — 클라이언트의 알림 버퍼는 한 표만 꺼낼 수 있다. 표를 놓으면 다른 표가 받고, 클라이언트와 표 중 어느 쪽이 먼저 소멸해도 안전하다.
// 두 번째 표 요청이 실패하는 경로는 어서션을 남기므로 여기서 부르지 않는다. 우편함 · 순위표 · 매칭 클라이언트가 같은 표를 쓰는지도 함께 본다.
#include "pch.h"

#include "Core/Container/DrainGate.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Shared/LeaderboardClient.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Shared/MailboxClient.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingClient.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( OnlineDrainGateTest, OnlyTheHolderOwnsTheGateAndReleaseHandsItOver )
{
    DrainGate  gate;
    DrainLease first;
    DrainLease second;
    SW_ASSERT_TRUE( gate.claim( first ) );
    SW_EXPECT_TRUE( gate.claim( first ) ); // 이미 쥔 표는 다시 받아도 true
    SW_EXPECT_TRUE( gate.isHeldBy( first ) );
    SW_EXPECT_TRUE( gate.isHeldBy( second ) == false );
    first.release();
    SW_EXPECT_TRUE( first.isHeld() == false );
    SW_ASSERT_TRUE( gate.claim( second ) );
    SW_EXPECT_TRUE( gate.isHeldBy( second ) );
}

SW_TEST_CASE( OnlineDrainGateTest, EitherSideMayDieFirst )
{
    DrainLease lease;
    {
        DrainGate gate;
        SW_ASSERT_TRUE( gate.claim( lease ) );
        SW_EXPECT_TRUE( lease.isHeld() );
    }
    SW_EXPECT_TRUE( lease.isHeld() == false ); // 게이트가 먼저 사라지면 표가 놓인다
    {
        DrainGate  gate;
        DrainLease inner;
        SW_ASSERT_TRUE( gate.claim( inner ) );
    }
    DrainGate gate;
    SW_EXPECT_TRUE( gate.claim( lease ) ); // 표가 먼저 사라져도 게이트는 다시 줄 수 있다
}

SW_TEST_CASE( OnlineDrainGateTest, MailboxLeaderboardAndMatchmakingClientsGateTheirBuffers )
{
    MailboxClient     mailbox;
    LeaderboardClient leaderboard;
    MatchmakingClient matchmaking;

    DrainLease mailLease;
    DrainLease unlockLease;
    DrainLease matchLease;
    SW_ASSERT_TRUE( mailbox.claimNewMail( mailLease ) );
    SW_ASSERT_TRUE( leaderboard.claimAchievementUnlocks( unlockLease ) );
    SW_ASSERT_TRUE( matchmaking.claimNotifications( matchLease ) );

    vector<MailboxNewMail> listMail;
    mailbox.drainNewMail( mailLease, listMail );
    SW_EXPECT_TRUE( listMail.empty() );
    vector<AchievementState> listUnlock;
    leaderboard.drainAchievementUnlocks( unlockLease, listUnlock );
    SW_EXPECT_TRUE( listUnlock.empty() );
    vector<MatchAssignment> listMatch;
    matchmaking.drainMatchResults( matchLease, listMatch );
    SW_EXPECT_TRUE( listMatch.empty() );

    // 표를 놓으면 다른 곳이 받는다
    mailLease.release();
    DrainLease otherMailLease;
    SW_EXPECT_TRUE( mailbox.claimNewMail( otherMailLease ) );
}
