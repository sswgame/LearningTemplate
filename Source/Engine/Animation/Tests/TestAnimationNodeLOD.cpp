// 애니메이션 노드 LOD — 등급 한 축(주기 단계 + 예산), 노드 가중치 페이드, 그래프 레이어 · 블렌드 스페이스 · 리그 노드의 `max_lod`.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Graph/BlendSpace.h"
#include "Engine/Animation/Scene/AnimationLOD.h"

using namespace sw;

namespace
{
    struct AnimationNodeLODTestUtil
    {
        /** @brief 단계 셋(0.25 · 0.10 · 0)의 표입니다. 페이드는 0.2 초입니다. */
        static AnimationLODSettings makeSettings()
        {
            AnimationLODSettings settings{};
            settings._listRateLevel.push_back( makeLevel( 0.25f, 1, false ) );
            settings._listRateLevel.push_back( makeLevel( 0.10f, 2, true ) );
            settings._listRateLevel.push_back( makeLevel( 0.0f, 4, false ) );
            settings._nodeFadeSeconds = 0.2f;
            return settings;
        }

        static AnimationLODRateLevel makeLevel( float32 minScreenSize, uint32 divisor, bool bInterpolate )
        {
            AnimationLODRateLevel level{};
            level._minScreenSize     = minScreenSize;
            level._updateRateDivisor = divisor;
            level._bInterpolate      = bInterpolate;
            return level;
        }
    };
    using Util = AnimationNodeLODTestUtil;
} // namespace

/** @brief [AnimationNodeLODTest] 등급은 주기 단계이고, 예산이 주기를 두 배로 늘린 횟수만큼 오른다 */
SW_TEST_CASE( AnimationNodeLODTest, GradeFollowsRateLevelAndBudget )
{
    const AnimationLODSettings settings  = Util::makeSettings();
    const AnimationLODState    nearState = AnimationLODState::makeState( settings, 0.5f, true, nullptr );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( nearState._lodGrade ) );
    SW_EXPECT_NEAR( 0.2f, nearState._nodeFadeSeconds, 0.0f );
    AnimationLODState middleState = AnimationLODState::makeState( settings, 0.15f, true, nullptr );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( middleState._lodGrade ) );
    // 예산이 2 를 8 로 늘렸다 — 두 배 두 번이라 등급 1 + 2 다.
    middleState.applyBudgetDivisor( 8 );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( middleState._lodGrade ) );
    SW_EXPECT_EQUAL( 8u, middleState._updateRateDivisor );
    SW_EXPECT_EQUAL( static_cast<uint32>( SW_TRUE ), static_cast<uint32>( middleState._bInterpolate ) );
    // 예산이 손대지 않으면 등급도 그대로다.
    AnimationLODState farState = AnimationLODState::makeState( settings, 0.01f, true, nullptr );
    farState.applyBudgetDivisor( 4 );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( farState._lodGrade ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( SW_FALSE ), static_cast<uint32>( farState._bInterpolate ) );
    // 기본 상태(LOD 판정이 없는 호스트)는 등급 0 — `max_lod` 가 무엇이든 켜진다.
    const AnimationLODState fullState{};
    SW_EXPECT_TRUE( fullState.isNodeOn( 0 ) );
    SW_EXPECT_TRUE( fullState.isNodeOn( -1 ) );
    SW_EXPECT_TRUE( farState.isNodeOn( -1 ) );
    SW_EXPECT_TRUE( farState.isNodeOn( 2 ) );
    SW_EXPECT_FALSE( farState.isNodeOn( 1 ) );
}

/** @brief [AnimationNodeLODTest] 페이드는 시간에 맞춰 내려가고, 도중에 다시 켜면 지금 값에서 이어 오른다(튀지 않는다). 페이드 0 은 바로 바뀐다 */
SW_TEST_CASE( AnimationNodeLODTest, FadeContinuesFromCurrentWeight )
{
    AnimationLODFade fade{};
    SW_EXPECT_NEAR( 1.0f, fade._weight, 0.0f );
    fade.update( false, 0.05f, 0.2f );
    SW_EXPECT_NEAR( 0.75f, fade._weight, 1.0e-5f );
    fade.update( false, 0.05f, 0.2f );
    SW_EXPECT_NEAR( 0.5f, fade._weight, 1.0e-5f );
    SW_EXPECT_FALSE( fade.isOff() );
    fade.update( true, 0.05f, 0.2f );
    SW_EXPECT_NEAR( 0.75f, fade._weight, 1.0e-5f );
    fade.update( false, 1.0f, 0.2f );
    SW_EXPECT_TRUE( fade.isOff() );
    SW_EXPECT_NEAR( 0.0f, fade._weight, 0.0f );
    fade.update( true, 0.1f, 0.2f );
    SW_EXPECT_NEAR( 0.5f, fade._weight, 1.0e-5f );
    fade.update( true, 1.0f, 0.2f );
    SW_EXPECT_NEAR( 1.0f, fade._weight, 0.0f );
    fade.update( false, 0.0f, 0.0f );
    SW_EXPECT_TRUE( fade.isOff() );
}

/** @brief [AnimationNodeLODTest] 블렌드 스페이스 가지 접기 — LOD 가중치 1 은 그대로, 0 은 가장 무거운 표본 하나, 사이는 섞이며 합은 늘 1 이다 */
SW_TEST_CASE( AnimationNodeLODTest, BlendSpaceFoldsToHeaviestSample )
{
    const vector<float32> listOriginal{ 0.3f, 0.7f };
    vector<float32>       listWeight = listOriginal;
    BlendSpace::foldToHeaviest( listWeight, 1.0f );
    SW_EXPECT_NEAR( 0.3f, listWeight[0], 0.0f );
    SW_EXPECT_NEAR( 0.7f, listWeight[1], 0.0f );
    listWeight = listOriginal;
    BlendSpace::foldToHeaviest( listWeight, 0.5f );
    SW_EXPECT_NEAR( 0.15f, listWeight[0], 1.0e-6f );
    SW_EXPECT_NEAR( 0.85f, listWeight[1], 1.0e-6f );
    listWeight = listOriginal;
    BlendSpace::foldToHeaviest( listWeight, 0.0f );
    SW_EXPECT_NEAR( 0.0f, listWeight[0], 0.0f );
    SW_EXPECT_NEAR( 1.0f, listWeight[1], 0.0f );
    // 같은 무게면 앞 표본이 남는다(결정적).
    listWeight = { 0.5f, 0.5f };
    BlendSpace::foldToHeaviest( listWeight, 0.0f );
    SW_EXPECT_NEAR( 1.0f, listWeight[0], 0.0f );
    SW_EXPECT_NEAR( 0.0f, listWeight[1], 0.0f );
}
