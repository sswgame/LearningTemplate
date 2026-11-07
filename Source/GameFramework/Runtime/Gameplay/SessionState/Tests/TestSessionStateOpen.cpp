#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"

using namespace sw;

/**
 * @brief [SessionStateOpenTest] 오브젝트에 붙지 않은(판 상태를 찾을 수 없는) 디렉터는 판을 열지 못하고 오류와 Refused 를 받는다
 */
SW_TEST_CASE( SessionStateOpenTest, MissingStateIsRefused )
{
    SW_EXPECT_LOG( "SessionState.MissingOnOwner" );
    SessionStateComponent  loose;
    SessionStateInitResult result = SessionStateInitResult::Fresh;

    SW_EXPECT_TRUE( SessionStateComponent::openOnOwner( loose, SessionStateSettings{}, result ) == nullptr );
    SW_EXPECT_TRUE( result == SessionStateInitResult::Refused );
    SW_EXPECT_TRUE( SessionStateComponent::openOnOwner( loose, SessionStateSettings{} ) == nullptr );
}
