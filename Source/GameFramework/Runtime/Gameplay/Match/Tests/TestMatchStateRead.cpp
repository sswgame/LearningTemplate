#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/Tests/StateIOTestUtil.h"

#include "GameFramework/Runtime/Gameplay/Match/MatchState.h"

// MatchState 상태 읽기. 결함: 팀 −1 과 범위 밖 공격자 번호를 받아 `reportKill` 이 팀 · 참가자 목록 밖을 읽었다.

namespace
{
    /** @brief 팀 하나, 피해 기록 하나(자기 자신)인 참가자 하나의 `writeState` 바이트에서 팀 · 공격자 번호를 덮어써 읽습니다. */
    struct MatchStateReadTestUtil
    {
        // 리플렉션 본문 끝에서 센 칸 폭 — 참가자의 팀 뒤: 처치 · 죽음 · 도움 · 부활 비용(int32 넷) + 깃발 둘, 점수판 끝: 경과 · 단계 시간 · 승리 팀(4 바이트 셋) + 단계(열거 int64).
        static constexpr size_t kAfterTeamBytes      = sizeof( int32 ) * 4 + sizeof( uint8 ) * 2;
        static constexpr size_t kMatchTailBytes      = sizeof( float32 ) * 2 + sizeof( int32 ) + sizeof( int64 );
        static constexpr size_t kTeamToAttackerBytes = sizeof( int32 ) + sizeof( float32 ) * 2; ///< 공격자 뒤 시각, 준 피해

        static bool readFixture( int32 team, int32 attacker )
        {
            sw::MatchState written;
            written.initialize( sw::MatchSettings{} );
            written.addTeam( sw::hashed_string( "A" ) );
            const bool bAdded = written.addParticipant( 0, sw::hashed_string( "role" ) ).has_value();
            written.reportDamage( 0, 0, 1.0f );

            sw::vector<uint8> listBytes  = sw::StateIOTestUtil::computeStateBytes( written );
            const size_t      teamAt     = sw::StateIOTestUtil::computeReflectedBytes( written ) - kMatchTailBytes - kAfterTeamBytes - sizeof( int32 );
            const size_t      attackerAt = teamAt - kTeamToAttackerBytes;
            // 바탕을 만들지 못하면 거짓 — 고치지 않은 값의 읽기(`readFixture( 0, 0 )`)가 참이어야 하므로 거기서 드러난다
            if ( bAdded == false || sw::StateIOTestUtil::overwrite( listBytes, teamAt, team ) == false || sw::StateIOTestUtil::overwrite( listBytes, attackerAt, attacker ) == false )
                return false;

            sw::MatchState state;
            sw::Archive    reading( listBytes.data(), listBytes.size() );
            return state.readState( reading );
        }
    };
} // namespace

/**
 * @brief [MatchStateTest] 없는 팀 · 없는 공격자를 가리키는 바이트는 거절한다
 */
SW_TEST_CASE( MatchStateTest, ReadRefusesUnknownTeamAndAttacker )
{
    SW_EXPECT_TRUE( MatchStateReadTestUtil::readFixture( 0, 0 ) ); // 고치지 않은 값과 같다 — 아래 거절이 칸 값 때문임을 보인다
    SW_EXPECT_FALSE( MatchStateReadTestUtil::readFixture( -1, 0 ) );
    SW_EXPECT_FALSE( MatchStateReadTestUtil::readFixture( 1, 0 ) );
    SW_EXPECT_FALSE( MatchStateReadTestUtil::readFixture( 0, -1 ) );
    SW_EXPECT_FALSE( MatchStateReadTestUtil::readFixture( 0, 1 ) );
}
