#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Renderer/Frame/View/RenderViewScheduler.h"

namespace sw
{
    /** @brief 안을 보는 테스트 창구입니다(`friend`). */
    struct RenderViewSchedulerTestAccess
    {
        static uint32 getTrackedCount( const RenderViewScheduler& scheduler ) { return static_cast<uint32>( scheduler._listEntry.size() ); }
    };
} // namespace sw

// 추가 뷰 자리 고르기(RN-13) — 카메라가 자리 수보다 많으면 보이는 뷰 먼저, 그다음 늦은 뷰 먼저 자리를 준다. 등록 순서로 자르지 않는다.

using namespace sw;

namespace
{
    struct RenderViewSchedulerTestUtil
    {
        static constexpr uint32 kMaxListed = 8;

        /** @brief 카메라 12 대 — 앞 6 대는 보이지 않고(모니터가 화면 밖) 뒤 6 대가 보인다. 모두 렌더 텍스처 뷰, 매 프레임 갱신. */
        static vector<RenderViewScheduler::Candidate> makeCameras()
        {
            vector<RenderViewScheduler::Candidate> listCandidate( 12 );
            for ( uint32 index = 0; index < 12u; ++index )
            {
                listCandidate[index]._viewID     = 100u + index;
                listCandidate[index]._pixelCount = 256u * 256u;
                listCandidate[index]._bVisible   = index >= 6u ? SW_TRUE : SW_FALSE;
            }
            return listCandidate;
        }
    };
} // namespace

/** @brief [RenderViewSchedulerTest] 카메라 12 대 중 뒤에 등록한 보이는 6 대는 모두 자리를 얻어 그려지고, 자리는 8 개만 나간다 */
SW_TEST_CASE( RenderViewSchedulerTest, VisibleViewsWinSlotsOverRegistrationOrder )
{
    using Util                                           = RenderViewSchedulerTestUtil;
    const vector<RenderViewScheduler::Candidate> listCam = Util::makeCameras();
    RenderViewScheduler                          scheduler;
    vector<uint8>                                listListed( listCam.size(), SW_FALSE );
    vector<uint8>                                listRender( listCam.size(), SW_FALSE );
    const uint32                                 chosen = scheduler.schedule( 0.0, listCam.data(), static_cast<uint32>( listCam.size() ), Util::kMaxListed, RenderViewBudget{}, listListed.data(), listRender.data() );

    uint32 listedCount = 0;
    for ( uint32 index = 0; index < 12u; ++index )
    {
        listedCount += listListed[index] != SW_FALSE ? 1u : 0u;
        if ( index >= 6u )
        {
            SW_EXPECT_TRUE( listListed[index] != SW_FALSE );
            SW_EXPECT_TRUE( listRender[index] != SW_FALSE );
        }
        else
        {
            SW_EXPECT_TRUE( listRender[index] == SW_FALSE ); // 보이지 않는 뷰는 그리지 않는다
        }
    }
    SW_EXPECT_EQUAL( Util::kMaxListed, listedCount );
    SW_EXPECT_EQUAL( 6u, chosen );
    SW_EXPECT_EQUAL( 12u, RenderViewSchedulerTestAccess::getTrackedCount( scheduler ) ); // 자리를 얻지 못한 뷰도 기록이 남는다
}

/** @brief [RenderViewSchedulerTest] 모두 보이면 자리를 얻지 못한 뷰가 다음 프레임에 더 늦은 채로 자리를 얻는다 — 같은 뷰가 계속 굶지 않는다 */
SW_TEST_CASE( RenderViewSchedulerTest, StarvedViewsGetSlotsNextFrame )
{
    using Util                                     = RenderViewSchedulerTestUtil;
    vector<RenderViewScheduler::Candidate> listCam = Util::makeCameras();
    for ( RenderViewScheduler::Candidate& candidate : listCam )
    {
        candidate._bVisible = SW_TRUE;
    }
    RenderViewScheduler scheduler;
    vector<uint8>       listListed( listCam.size(), SW_FALSE );
    vector<uint8>       listRender( listCam.size(), SW_FALSE );
    vector<uint8>       listEverRendered( listCam.size(), SW_FALSE );
    for ( uint32 frame = 0; frame < 2u; ++frame )
    {
        (void)scheduler.schedule( static_cast<float64>( frame ) / 60.0, listCam.data(), static_cast<uint32>( listCam.size() ), Util::kMaxListed, RenderViewBudget{}, listListed.data(), listRender.data() );
        for ( uint32 index = 0; index < 12u; ++index )
        {
            if ( listRender[index] != SW_FALSE )
                listEverRendered[index] = SW_TRUE;
        }
    }
    for ( uint32 index = 0; index < 12u; ++index )
    {
        SW_EXPECT_TRUE( listEverRendered[index] != SW_FALSE );
    }
}
