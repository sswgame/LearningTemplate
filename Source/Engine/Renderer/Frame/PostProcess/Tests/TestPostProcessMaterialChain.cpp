#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Renderer/Frame/PostProcess/FramePostProcessMaterials.h"

// 같은 자리 후처리 머티리얼의 이어 읽기 — 둘째부터 앞 결과를 읽는다(언리얼 블렌더블의 PostProcessInput0). 톤맵 대체 자리 · PSO 없는 그리기는 잇지 않는다.

using namespace sw;

namespace
{
    struct PostProcessMaterialChainTestUtil
    {
        static FrameViewTarget::PostProcessMaterialPass makeDraw( PostProcessBlendableLocation location, RHIPipelineStateHandle pso )
        {
            FrameViewTarget::PostProcessMaterialPass draw;
            draw._location = location;
            draw._pso      = pso;
            return draw;
        }
    };
} // namespace

/** @brief [PostProcessMaterialChainTest] 같은 자리의 둘째 · 셋째 머티리얼은 이어 읽고, 첫째 · 다른 자리의 첫째는 원본을 읽는다 */
SW_TEST_CASE( PostProcessMaterialChainTest, LaterDrawsAtTheSameLocationReadThePreviousResult )
{
    using Util                                                      = PostProcessMaterialChainTestUtil;
    const vector<FrameViewTarget::PostProcessMaterialPass> listDraw = {
        Util::makeDraw( PostProcessBlendableLocation::BeforeTonemap, RHIPipelineStateHandle{ 11 } ),
        Util::makeDraw( PostProcessBlendableLocation::AfterTonemap, RHIPipelineStateHandle{ 12 } ),
        Util::makeDraw( PostProcessBlendableLocation::BeforeTonemap, RHIPipelineStateHandle{ 13 } ),
        Util::makeDraw( PostProcessBlendableLocation::BeforeTonemap, RHIPipelineStateHandle{ 14 } ),
    };
    SW_EXPECT_FALSE( FramePostProcessMaterials::isChainedDraw( listDraw, 0 ) );
    SW_EXPECT_FALSE( FramePostProcessMaterials::isChainedDraw( listDraw, 1 ) );
    SW_EXPECT_TRUE( FramePostProcessMaterials::isChainedDraw( listDraw, 2 ) );
    SW_EXPECT_TRUE( FramePostProcessMaterials::isChainedDraw( listDraw, 3 ) );
}

/** @brief [PostProcessMaterialChainTest] 앞 그리기가 PSO 를 못 만들었으면(그리지 않는다) 다음 그리기는 원본을 읽고, 톤맵 대체 자리는 잇지 않는다 */
SW_TEST_CASE( PostProcessMaterialChainTest, SkippedDrawsAndReplacingTonemapDoNotChain )
{
    using Util                                                      = PostProcessMaterialChainTestUtil;
    const vector<FrameViewTarget::PostProcessMaterialPass> listDraw = {
        Util::makeDraw( PostProcessBlendableLocation::AfterTonemap, RHIPipelineStateHandle{ 0 } ),
        Util::makeDraw( PostProcessBlendableLocation::AfterTonemap, RHIPipelineStateHandle{ 21 } ),
        Util::makeDraw( PostProcessBlendableLocation::ReplacingTonemap, RHIPipelineStateHandle{ 22 } ),
        Util::makeDraw( PostProcessBlendableLocation::ReplacingTonemap, RHIPipelineStateHandle{ 23 } ),
    };
    SW_EXPECT_FALSE( FramePostProcessMaterials::isChainedDraw( listDraw, 1 ) );
    SW_EXPECT_FALSE( FramePostProcessMaterials::isChainedDraw( listDraw, 3 ) );
}
