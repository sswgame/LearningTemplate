// AnimationCrowdBucketKeyTest — 군중 묶음 열쇠: 스키닝 방식이 다른 유닛은 같은 상태가 아니라 다른 묶음이다(묶음 팔레트를 한 방식으로 싣는다).
// 예전에는 열쇠에 방식이 없어 군중 묶음을 모두 선형으로 그렸고, 듀얼 쿼터니언 유닛이 묶음에 들면 방식이 바뀌었다. nogpu.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Scene/AnimationCrowd.h"

using namespace sw;

/** @brief [AnimationCrowdBucketKeyTest] 방식만 다른 두 열쇠는 같은 상태가 아니고 같은 열쇠도 아니며, 해시가 갈린다 */
SW_TEST_CASE( AnimationCrowdBucketKeyTest, SkinningMethodSplitsBuckets )
{
    AnimationCrowdBucketKey linear{};
    linear._skinDataID         = IdentityToken::make();
    linear._playRate           = 1.0f;
    AnimationCrowdBucketKey dq = linear;
    dq._skinningMethod         = SkinningMethod::DualQuaternion;

    SW_EXPECT_TRUE( linear.isSameState( linear ) );
    SW_EXPECT_FALSE( linear.isSameState( dq ) );
    SW_EXPECT_FALSE( linear == dq );
    const AnimationCrowdBucketKeyHash hash;
    SW_EXPECT_TRUE( hash( linear ) != hash( dq ) );

    AnimationCrowdBucketKey otherVariation = dq;
    otherVariation._variation              = 3;
    SW_EXPECT_TRUE( dq.isSameState( otherVariation ) ); // 변형 칸만 다르면 같은 상태다
}
