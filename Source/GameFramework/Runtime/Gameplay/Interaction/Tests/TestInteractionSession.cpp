// InteractionSessionTest — 연타 단계의 진행량은 누를 때 오르고 쉬면 줄며 0 에서 멈춘다(DepletingValue). 다 차면 끝난다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

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

        static shared_ptr<const InteractionDef> makeReactionDef()
        {
            InteractionStepDef step;
            step._mode                      = InteractionInputMode::Reaction;
            step._delayMin                  = 0.5f;
            step._delayMax                  = 1.5f;
            step._window                    = 0.25f;
            shared_ptr<InteractionDef> pDef = sw::make_shared<InteractionDef>();
            pDef->_listStep.push_back( step );
            return pDef;
        }

        /** @brief 반응이 열릴 때까지 걸린 프레임 수입니다(열리지 않으면 -1). */
        static int32 runUntilOpen( InteractionSession& session )
        {
            for ( int32 frame = 0; frame < 240 && session.isActive(); ++frame )
            {
                if ( session.isReactionOpen() )
                    return frame;
                session.update( GameSeconds{ 1.0f / 60.0f }, false, false );
            }
            return -1;
        }
    };
    using Util = InteractionSessionTestUtil;
} // namespace

/** @brief [InteractionSessionTest] 연타는 1/presses 씩 오르고, 쉬면 decay × 시간만큼 줄되 0 아래로 가지 않으며, 다시 눌러 다 차면 끝난다 */
SW_TEST_CASE( InteractionSessionTest, MashDecayStopsAtZero )
{
    InteractionSession session;
    SW_REQUIRE_TRUE( session.begin( Util::makeMashDef( 4, 1.0f ), 1u ) );
    session.update( GameSeconds{ 0.0f }, true, true );
    session.update( GameSeconds{ 0.0f }, true, true );
    SW_EXPECT_NEAR( 0.5f, session.getStepProgress(), 1.0e-5f );

    session.update( GameSeconds{ 0.3f }, false, false );
    SW_EXPECT_NEAR( 0.2f, session.getStepProgress(), 1.0e-5f );
    session.update( GameSeconds{ 1.0f }, false, false ); // 0.2 보다 많이 깎는다 — 0 에서 멈춘다
    SW_EXPECT_NEAR( 0.0f, session.getStepProgress(), 0.0f );
    SW_EXPECT_TRUE( session.isActive() );

    for ( int32 press = 0; press < 4; ++press )
    {
        session.update( GameSeconds{ 0.0f }, true, true );
    }
    SW_EXPECT_TRUE( session.getState() == InteractionSessionState::Completed );
}

/** @brief [InteractionSessionTest] 반응 단계는 범위 안의 때에 열리고(같은 하는 쪽 · 같은 차례면 같은 때), 열린 동안 누르면 끝난다 */
SW_TEST_CASE( InteractionSessionTest, ReactionOpensAtADeterministicTime )
{
    InteractionSession first;
    InteractionSession second;
    SW_REQUIRE_TRUE( first.begin( Util::makeReactionDef(), 7u ) );
    SW_REQUIRE_TRUE( second.begin( Util::makeReactionDef(), 7u ) );
    const int32 frame = Util::runUntilOpen( first );
    SW_EXPECT_TRUE( 30 <= frame && frame <= 91 );
    SW_EXPECT_EQUAL( frame, Util::runUntilOpen( second ) );
    first.update( GameSeconds{ 1.0f / 60.0f }, true, true );
    SW_EXPECT_TRUE( first.getState() == InteractionSessionState::Completed );
}

/** @brief [InteractionSessionTest] 열리기 전에 누르거나 열린 동안 누르지 않으면 취소다 */
SW_TEST_CASE( InteractionSessionTest, ReactionTooEarlyOrTooLateCancels )
{
    InteractionSession early;
    SW_REQUIRE_TRUE( early.begin( Util::makeReactionDef(), 1u ) );
    early.update( GameSeconds{ 1.0f / 60.0f }, true, true );
    SW_EXPECT_TRUE( early.getState() == InteractionSessionState::Cancelled );

    InteractionSession late;
    SW_REQUIRE_TRUE( late.begin( Util::makeReactionDef(), 1u ) );
    SW_REQUIRE_TRUE( Util::runUntilOpen( late ) >= 0 );
    for ( int32 frame = 0; frame < 30; ++frame )
    {
        late.update( GameSeconds{ 1.0f / 60.0f }, false, false );
    }
    SW_EXPECT_TRUE( late.getState() == InteractionSessionState::Cancelled );
}
