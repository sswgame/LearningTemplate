#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Character/Control/Pawn/CharacterPawnMovement2DComponent.h"

// PawnMovement2DTest — 의도에서 수평 속도로 가는 규칙(가감속 · 끝 맞춤). 실제 2D 물리 위의 걷기 · 점프는 시나리오 game.empty.twodphysics 가 본다(nogpu).

using namespace sw;

namespace
{
    struct PawnMovement2DTestUtil
    {
        static constexpr float32 kWalk         = 5.0f;
        static constexpr float32 kAcceleration = 40.0f;
        static constexpr float32 kDeceleration = 50.0f;
        static constexpr float32 kDelta        = 1.0f / 60.0f;

        static float32 step( float32 current, float32 moveRight )
        {
            return CharacterPawnMovement2DComponent::computeVelocity( current, moveRight, kWalk, kAcceleration, kDeceleration, kDelta );
        }
    };
    using Util = PawnMovement2DTestUtil;
} // namespace

/** @brief [PawnMovement2DTest] 의도가 있으면 가속으로 다가가고 걷는 속도에서 멈춘다 */
SW_TEST_CASE( PawnMovement2DTest, AcceleratesUpToTheWalkSpeed )
{
    const float32 first = Util::step( 0.0f, 1.0f );
    SW_EXPECT_TRUE( MathUtil::abs( first - Util::kAcceleration * Util::kDelta ) < 1.0e-4f );
    float32 velocity = first;
    for ( int32 frame = 0; frame < 120; ++frame )
    {
        velocity = Util::step( velocity, 1.0f );
    }
    SW_EXPECT_EQUAL( Util::kWalk, velocity );
}

/** @brief [PawnMovement2DTest] 의도가 없으면 감속으로 0 까지 간다(넘어가지 않는다) */
SW_TEST_CASE( PawnMovement2DTest, DeceleratesToZeroWithoutOvershoot )
{
    const float32 slowed = Util::step( Util::kWalk, 0.0f );
    SW_EXPECT_TRUE( MathUtil::abs( slowed - ( Util::kWalk - Util::kDeceleration * Util::kDelta ) ) < 1.0e-4f );
    SW_EXPECT_EQUAL( 0.0f, Util::step( 0.1f, 0.0f ) );
}

/** @brief [PawnMovement2DTest] 반대 의도는 음수 속도까지 가고, 1 을 넘는 의도는 자른다 */
SW_TEST_CASE( PawnMovement2DTest, OppositeIntentReversesAndClamps )
{
    float32 velocity = Util::kWalk;
    for ( int32 frame = 0; frame < 120; ++frame )
    {
        velocity = Util::step( velocity, -3.0f );
    }
    SW_EXPECT_EQUAL( -Util::kWalk, velocity );
}
