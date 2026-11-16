// BlendPoses 포즈 노드 — 무게가 한쪽에 다 있으면 다른 입력을 샘플하지 않는다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Animation/Skeletal/AnimationPoseNode.h"
#include "Engine/Animation/Skeletal/Pose.h"

using namespace sw;

namespace
{
    /** @brief 길이만 가진 입력입니다. */
    class BlendPosesTestClip final : public IAnimationPlayable
    {
    public:
        float32 getPlayLength() const override { return 1.0f; }
        bool    isLoopingByDefault() const override { return true; }
    };

    /** @brief 입력마다 샘플한 횟수를 셉니다. */
    class BlendPosesTestSampler final : public IAnimationPoseSampler
    {
    public:
        const IAnimationPlayable* _pInputA;
        uint32                    _sampleCountA{ 0 };
        uint32                    _sampleCountB{ 0 };

        explicit BlendPosesTestSampler( const IAnimationPlayable& inputA )
            : _pInputA{ &inputA }
        {
        }

        void samplePose( const IAnimationPlayable& playable, float32 time, Pose& inoutPose ) override
        {
            (void)time;
            (void)inoutPose;
            if ( &playable == _pInputA )
                ++_sampleCountA;
            else
                ++_sampleCountB;
        }
    };
} // namespace

/** @brief [BlendPosesNodeTest] 무게 0 이면 첫 입력만, 1 이면 둘째 입력만, 사이면 둘 다 샘플한다 */
SW_TEST_CASE( BlendPosesNodeTest, SkipsInputWithoutWeight )
{
    const BlendPosesTestClip clipA;
    const BlendPosesTestClip clipB;
    const hashed_string      weightParameter{ "Wave" };
    const BlendPosesNode     node( clipA, clipB, weightParameter );
    AnimationParameterSet    parameters;
    Pose                     pose;
    const float32            arrWeight[3]    = { 0.0f, 1.0f, 0.5f };
    const uint32             arrExpectedA[3] = { 1u, 0u, 1u };
    const uint32             arrExpectedB[3] = { 0u, 1u, 1u };
    for ( uint32 index = 0; index < 3; ++index )
    {
        BlendPosesTestSampler sampler( clipA );
        parameters.setFloat( weightParameter, arrWeight[index] );
        const AnimationPoseParams params{ &sampler, &parameters };
        SW_EXPECT_TRUE( node.evaluatePose( params, 0.25f, pose ) );
        SW_EXPECT_EQUAL( arrExpectedA[index], sampler._sampleCountA );
        SW_EXPECT_EQUAL( arrExpectedB[index], sampler._sampleCountB );
    }
}
