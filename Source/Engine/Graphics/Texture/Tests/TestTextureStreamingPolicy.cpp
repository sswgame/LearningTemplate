#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Graphics/Texture/TextureStreamingPolicy.h"

// 텍스처 밉 스트리밍 정책(R6-2) — 예산을 넘으면 화면에서 작은 것부터 내리고, 아래면 화면에서 큰 것부터 필요한 밉까지 올리며, 경계에서 흔들리지 않는다.

using namespace sw;

namespace
{
    struct TextureStreamingPolicyTestUtil
    {
        static TextureStreamingConfig makeConfig()
        {
            TextureStreamingConfig config;
            config._uploadsPerFrame         = 8;
            config._upgradesPerFrame        = 2;
            config._poolMegabytes           = 768;
            config._idleFramesBeforeRelease = 300;
            return config;
        }

        /** @brief 밉 4 개(1024 · 256 · 64 · 16 바이트), 지금 @p resident 부터 있고 화면은 @p wanted 부터 원한다. */
        static TextureStreamingCandidate make( uint32 resident, uint32 wanted, uint32 idleFrames = 0 )
        {
            TextureStreamingCandidate candidate;
            candidate._mipCount         = 4;
            candidate._residentFirstMip = resident;
            candidate._wantedFirstMip   = wanted;
            candidate._idleFrames       = idleFrames;
            candidate._topMipBytes      = 1024;
            return candidate;
        }

        static uint64 sumResident( const vector<TextureStreamingCandidate>& listCandidate )
        {
            uint64 total = 0;
            for ( const TextureStreamingCandidate& candidate : listCandidate )
                total += candidate.computeResidentBytes( candidate._residentFirstMip );
            return total;
        }

        static void apply( vector<TextureStreamingCandidate>& inoutListCandidate, const vector<TextureStreamingDecision>& listDecision )
        {
            for ( const TextureStreamingDecision& decision : listDecision )
                inoutListCandidate[decision._candidateIndex]._residentFirstMip = decision._targetFirstMip;
        }
    };
} // namespace

/** @brief [TextureStreamingPolicyTest] 예산을 넘으면 화면에서 가장 작은 것(필요한 밉 번호가 큰 것)부터 내리고, 예산 아래가 되면 멈춘다 */
SW_TEST_CASE( TextureStreamingPolicyTest, OverBudgetDemotesSmallestOnScreenFirst )
{
    using Util                                            = TextureStreamingPolicyTestUtil;
    const vector<TextureStreamingCandidate> listCandidate = {
        Util::make( 0, 0 ), // 화면에서 크다 — 밉 0 이 필요하다
        Util::make( 0, 2 ), // 화면에서 작다 — 밉 2 면 된다
        Util::make( 0, 1 ),
    };
    const uint64                     resident = Util::sumResident( listCandidate ); // 1360 × 3
    vector<TextureStreamingDecision> listDecision;
    TextureStreamingPolicy::decide( listCandidate, resident, resident - 1000, Util::makeConfig(), listDecision );
    SW_REQUIRE_EQUAL( size_t( 1 ), listDecision.size() ); // 1 번을 밉 2 로 내리면 1280 바이트가 풀려 예산 아래다
    SW_EXPECT_EQUAL( 1u, listDecision[0]._candidateIndex );
    SW_EXPECT_EQUAL( 2u, listDecision[0]._targetFirstMip );
}

/** @brief [TextureStreamingPolicyTest] 예산 아래면 화면에서 큰 것부터 필요한 밉까지 올리고, 이미 필요한 만큼 든 것 · 오래 안 보인 것은 올리지 않는다 */
SW_TEST_CASE( TextureStreamingPolicyTest, UnderBudgetPromotesToWantedMip )
{
    using Util                                            = TextureStreamingPolicyTestUtil;
    const vector<TextureStreamingCandidate> listCandidate = {
        Util::make( 3, 1 ),
        Util::make( 3, 0 ),
        Util::make( 1, 1 ),      // 이미 필요한 만큼이다
        Util::make( 3, 0, 300 ), // 오래 안 보였다
    };
    vector<TextureStreamingDecision> listDecision;
    TextureStreamingPolicy::decide( listCandidate, Util::sumResident( listCandidate ), 1u << 20, Util::makeConfig(), listDecision );
    SW_REQUIRE_EQUAL( size_t( 2 ), listDecision.size() );
    SW_EXPECT_EQUAL( 1u, listDecision[0]._candidateIndex ); // 화면에서 더 큰 것이 먼저
    SW_EXPECT_EQUAL( 0u, listDecision[0]._targetFirstMip );
    SW_EXPECT_EQUAL( 0u, listDecision[1]._candidateIndex );
    SW_EXPECT_EQUAL( 1u, listDecision[1]._targetFirstMip );
}

/** @brief [TextureStreamingPolicyTest] 예산 경계에서 내린 텍스처를 다음 프레임에 다시 올리지 않는다 — 같은 입력을 거듭 돌리면 첫 결정 뒤로는 바뀌는 것이 없다 */
SW_TEST_CASE( TextureStreamingPolicyTest, NoOscillationAtBudgetEdge )
{
    using Util                                      = TextureStreamingPolicyTestUtil;
    vector<TextureStreamingCandidate> listCandidate = {
        Util::make( 0, 0 ),
        Util::make( 0, 0 ),
    };
    const uint64                     budget = Util::sumResident( listCandidate ) - 100; // 하나를 한 밉 내리면(1024 풀림) 예산 아래
    vector<TextureStreamingDecision> listDecision;
    TextureStreamingPolicy::decide( listCandidate, Util::sumResident( listCandidate ), budget, Util::makeConfig(), listDecision );
    SW_REQUIRE_EQUAL( size_t( 1 ), listDecision.size() );
    Util::apply( listCandidate, listDecision );
    for ( uint32 frame = 0; frame < 4u; ++frame )
    {
        TextureStreamingPolicy::decide( listCandidate, Util::sumResident( listCandidate ), budget, Util::makeConfig(), listDecision );
        SW_EXPECT_TRUE( listDecision.empty() );
        Util::apply( listCandidate, listDecision );
    }
    SW_EXPECT_TRUE( Util::sumResident( listCandidate ) <= budget );
}

/**
 * @brief [TextureStreamingPolicyTest] 화면이 요구하는 텍셀 수를 필요한 밉으로 바꾼다 — 너비가 그보다 작아지지 않는 가장 작은 밉, 요구가 없으면 가장 작은 밉(R6-6)
 * @details 변이: 비교를 `>` 로 바꾸면 요구와 같은 너비의 밉을 건너뛰어 한 밉 더 내려 흐려진다.
 */
SW_TEST_CASE( TextureStreamingPolicyTest, WantedMipFromScreenTexels )
{
    constexpr uint32 kTopWidth = 1024;
    constexpr uint32 kMipCount = 11;                                                                       // 1024 → 1
    SW_EXPECT_EQUAL( 0u, TextureStreamingPolicy::computeWantedFirstMip( kTopWidth, kMipCount, 2000.0f ) ); // 밉 0 보다 크게 보인다
    SW_EXPECT_EQUAL( 0u, TextureStreamingPolicy::computeWantedFirstMip( kTopWidth, kMipCount, 1024.0f ) );
    SW_EXPECT_EQUAL( 1u, TextureStreamingPolicy::computeWantedFirstMip( kTopWidth, kMipCount, 512.0f ) ); // 512 너비 밉이면 된다
    SW_EXPECT_EQUAL( 0u, TextureStreamingPolicy::computeWantedFirstMip( kTopWidth, kMipCount, 600.0f ) ); // 밉 1(512)은 모자라다 — 밉 0
    SW_EXPECT_EQUAL( 1u, TextureStreamingPolicy::computeWantedFirstMip( kTopWidth, kMipCount, 300.0f ) ); // 512 ≥ 300 > 256
    SW_EXPECT_EQUAL( 3u, TextureStreamingPolicy::computeWantedFirstMip( kTopWidth, kMipCount, 100.0f ) ); // 128 ≥ 100 > 64
    SW_EXPECT_EQUAL( kMipCount - 1u, TextureStreamingPolicy::computeWantedFirstMip( kTopWidth, kMipCount, 0.0f ) );
}

/**
 * @brief [TextureStreamingPolicyTest] 다른 텍스처의 바이트가 예산 경계를 오가도 한 텍스처의 밉은 `_minFramesBetweenChanges` 안에 두 번 바뀌지 않는다(R6-6)
 * @details 여유 비율 0 이면 경계 위아래를 오갈 때마다 올렸다 내렸다 한다 — 최소 프레임만으로 간격을 본다. 여유가 있으면 그 띠 안에서는 올리지 않는다.
 *          변이: 정책이 `_framesSinceChange` 를 보지 않으면 매 프레임 바뀌어 간격 단언이 진다.
 */
SW_TEST_CASE( TextureStreamingPolicyTest, BoundaryOscillationIsDamped )
{
    using Util                               = TextureStreamingPolicyTestUtil;
    constexpr uint32       kMinFrames        = 10;
    constexpr uint32       kFrameCount       = 60;
    constexpr uint64       kBudget           = 4000;
    constexpr uint64       kOtherUnderBudget = 2400; // 이 텍스처를 밉 0 까지 올려도(1360) 예산 아래
    constexpr uint64       kOtherOverBudget  = 3800; // 밉 1 만 있어도(336) 예산을 넘는다
    TextureStreamingConfig config            = Util::makeConfig();
    config._minFramesBetweenChanges          = kMinFrames;
    config._promoteBudgetMarginPercent       = 0;
    vector<TextureStreamingCandidate> listCandidate{ Util::make( 1, 0 ) };
    listCandidate[0]._framesSinceChange = kMinFrames;
    vector<TextureStreamingDecision> listDecision;
    vector<uint32>                   listChangeFrame;
    for ( uint32 frame = 0; frame < kFrameCount; ++frame )
    {
        const uint64 other = ( frame % 2u == 0u ) ? kOtherUnderBudget : kOtherOverBudget;
        TextureStreamingPolicy::decide( vector_reference<const TextureStreamingCandidate>{ listCandidate }, other + Util::sumResident( listCandidate ), kBudget, config, listDecision );
        if ( listDecision.empty() )
        {
            ++listCandidate[0]._framesSinceChange;
            continue;
        }
        Util::apply( listCandidate, listDecision );
        listCandidate[0]._framesSinceChange = 0;
        listChangeFrame.push_back( frame );
    }
    SW_REQUIRE_TRUE( listChangeFrame.size() >= 2u ); // 경계를 오가므로 바뀌기는 한다
    for ( size_t index = 1; index < listChangeFrame.size(); ++index )
        SW_EXPECT_TRUE( listChangeFrame[index] - listChangeFrame[index - 1] >= kMinFrames );

    // 여유 띠: 예산의 50 % 를 남기면 아래쪽 바이트로도 올리지 않는다(2400 + 336 + 1024 > 2000).
    config._promoteBudgetMarginPercent  = 50;
    listCandidate[0]                    = Util::make( 1, 0 );
    listCandidate[0]._framesSinceChange = kMinFrames;
    TextureStreamingPolicy::decide( vector_reference<const TextureStreamingCandidate>{ listCandidate }, kOtherUnderBudget + Util::sumResident( listCandidate ), kBudget, config, listDecision );
    SW_EXPECT_TRUE( listDecision.empty() );
}
