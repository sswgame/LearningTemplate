#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Gameplay/Match/MatchState.h"

// MatchState 상태 읽기. 결함: 팀 −1 과 범위 밖 공격자 번호를 받아 `reportKill` 이 팀 · 참가자 목록 밖을 읽었다.

namespace
{
    /** @brief 팀 하나, 피해 기록 하나인 참가자 하나의 상태 바이트를 씁니다. */
    struct MatchStateReadTestUtil
    {
        static bool readFixture( int32 team, int32 attacker )
        {
            sw::Archive written;
            written << static_cast<uint32>( 1 );
            sw::StateIO::writeName( written, sw::hashed_string( "A" ) );
            written << static_cast<int32>( 0 ); // 점수
            written << static_cast<int32>( 0 ); // 게이지
            written << static_cast<int32>( 0 ); // 순위
            written << static_cast<uint8>( SW_TRUE );
            written << static_cast<uint8>( SW_FALSE );
            written << static_cast<uint32>( 1 );
            sw::StateIO::writeName( written, sw::hashed_string( "role" ) );
            written << static_cast<uint32>( 1 );
            written << attacker;
            written << 0.0f;
            sw::StateIO::writeCountdown( written, sw::Countdown{} );
            written << 0.0f; // 준 피해
            written << team;
            for ( int32 stat = 0; stat < 4; ++stat )
            {
                written << static_cast<int32>( 0 ); // 처치, 죽음, 도움, 부활 비용
            }
            written << static_cast<uint8>( SW_TRUE );
            written << static_cast<uint8>( SW_FALSE );
            written << 0.0f;
            written << 0.0f;
            written << static_cast<int32>( -1 );
            written << static_cast<uint8>( sw::MatchPhase::InProgress );

            sw::MatchState state;
            sw::Archive    reading( written.getData(), written.getSize() );
            return state.readState( reading );
        }
    };
} // namespace

/**
 * @brief [MatchStateTest] 없는 팀 · 없는 공격자를 가리키는 바이트는 거절한다
 */
SW_TEST_CASE( MatchStateTest, ReadRefusesUnknownTeamAndAttacker )
{
    SW_EXPECT_TRUE( MatchStateReadTestUtil::readFixture( 0, 0 ) );
    SW_EXPECT_FALSE( MatchStateReadTestUtil::readFixture( -1, 0 ) );
    SW_EXPECT_FALSE( MatchStateReadTestUtil::readFixture( 1, 0 ) );
    SW_EXPECT_FALSE( MatchStateReadTestUtil::readFixture( 0, -1 ) );
    SW_EXPECT_FALSE( MatchStateReadTestUtil::readFixture( 0, 1 ) );
}
