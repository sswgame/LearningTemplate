// PairedAnimationSessionTest — 세션 수명: 참가자가 계층에서 꺼지면(풀 반환) 세션이 "잃은 역할" 로 정해진 까닭(`ParticipantDeactivated`)과 알림 로그로 끝난다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Character/PairedAnimation/Tests/PairedAnimationTestRig.h"

using namespace sw;

/**
 * @brief [PairedAnimationSessionTest] 켜진 채로는 세션이 이어지고, 필수 참가자를 끄면 `ParticipantDeactivated` 로 중단된다 — 예전에는 시계가 멈춰 시각 어긋남(오용 ERROR)으로 끝났다
 * @details 변이: `loseVanishedRoles` 의 꺼짐 판정을 빼면 시계를 멈춘 이 틀에서는 세션이 계속 재생돼 둘째 단언이 진다.
 */
SW_TEST_CASE( PairedAnimationSessionTest, DeactivatedParticipantEndsTheSession )
{
    test::PairedAnimationTestRig rig;
    rig.update();
    rig.update();
    SW_REQUIRE_TRUE( rig.getSession().isPlaying() ); // 대조: 켜진 채로는 이어진다
    SW_EXPECT_TRUE( rig.getSession().getAbortReason() == PairedAnimationAbortReason::None );

    rig.getVictim().getOwner()->setActive( false );
    rig.update();
    SW_EXPECT_FALSE( rig.getSession().isPlaying() );
    SW_EXPECT_TRUE( rig.getSession().getState() == PairedAnimationSessionState::Aborted );
    SW_EXPECT_TRUE( rig.getSession().getAbortReason() == PairedAnimationAbortReason::ParticipantDeactivated );
}

/**
 * @brief [PairedAnimationSessionTest] 부른 쪽의 중단은 `Requested` 로 끝난다 — 까닭이 끝난 길마다 갈린다
 */
SW_TEST_CASE( PairedAnimationSessionTest, RequestedAbortHasItsOwnReason )
{
    test::PairedAnimationTestRig rig;
    rig.update();
    rig.getSession().abort( rig.getManager() );
    SW_EXPECT_TRUE( rig.getSession().getState() == PairedAnimationSessionState::Aborted );
    SW_EXPECT_TRUE( rig.getSession().getAbortReason() == PairedAnimationAbortReason::Requested );
}
