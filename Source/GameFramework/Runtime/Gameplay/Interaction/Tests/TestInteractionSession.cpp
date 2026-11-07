// InteractionSessionTest — 연타 단계의 진행량은 누를 때 오르고 쉬면 줄며 0 에서 멈춘다(DepletingValue). 다 차면 끝난다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractionSession.h"

using namespace sw;

namespace
{
    struct InteractionSessionTestUtil
    {
        static shared_ptr<const InteractionDef> makeMashDef( int32 presses, float32 decay )
        {
            InteractionStepDef step;
            step._mode                      = InteractionInputMode::Mash;
            step._presses                   = presses;
            step._decay                     = decay;
            shared_ptr<InteractionDef> pDef = sw::make_shared<InteractionDef>();
            pDef->_listStep.push_back( step );
            return pDef;
        }
    };
    using Util = InteractionSessionTestUtil;
} // namespace

/** @brief [InteractionSessionTest] 연타는 1/presses 씩 오르고, 쉬면 decay × 시간만큼 줄되 0 아래로 가지 않으며, 다시 눌러 다 차면 끝난다 */
SW_TEST_CASE( InteractionSessionTest, MashDecayStopsAtZero )
{
    InteractionSession session;
    SW_REQUIRE_TRUE( session.begin( Util::makeMashDef( 4, 1.0f ), 1u ) );
    session.update( 0.0f, true, true );
    session.update( 0.0f, true, true );
    SW_EXPECT_NEAR( 0.5f, session.getStepProgress(), 1.0e-5f );

    session.update( 0.3f, false, false );
    SW_EXPECT_NEAR( 0.2f, session.getStepProgress(), 1.0e-5f );
    session.update( 1.0f, false, false ); // 0.2 보다 많이 깎는다 — 0 에서 멈춘다
    SW_EXPECT_NEAR( 0.0f, session.getStepProgress(), 0.0f );
    SW_EXPECT_TRUE( session.isActive() );

    for ( int32 press = 0; press < 4; ++press )
    {
        session.update( 0.0f, true, true );
    }
    SW_EXPECT_TRUE( session.getState() == InteractionSessionState::Completed );
}
