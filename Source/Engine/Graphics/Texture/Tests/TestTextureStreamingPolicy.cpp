#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

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
