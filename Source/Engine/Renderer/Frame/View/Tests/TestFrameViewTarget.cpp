#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"

// 뷰의 시간 누적 기록(RN-10) — 매 프레임 그리지 않는 뷰(갱신 주기를 낮춘 CCTV)는 그리는 프레임마다 기록을 버린다. 지난 뷰-투영은 몇 프레임 전인데 물체의 지난 행렬은 한 프레임 전이다.

using namespace sw;

/** @brief [FrameViewTargetTest] 이어 그리는 뷰는 첫 프레임만 기록을 버리고, 건너뛴 프레임 뒤에 그리는 프레임은 다시 버린다 */
SW_TEST_CASE( FrameViewTargetTest, SkippedFrameResetsHistory )
{
    FrameViewTarget view;
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._bHistoryReset == SW_TRUE ); // 처음 그린다
    view._taaHistory.markWritten();
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._bHistoryReset == SW_FALSE );
    SW_EXPECT_FALSE( view._taaHistory.isReset() );

    view.markFrameSkipped(); // 10 Hz 뷰 — 다음 몇 프레임은 그리지 않는다
    view.markFrameSkipped();
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._bHistoryReset == SW_TRUE );
    SW_EXPECT_TRUE( view._taaHistory.isReset() );

    view.beginViewHistory(); // 다시 이어 그리면 기록을 쓴다
    SW_EXPECT_TRUE( view._bHistoryReset == SW_FALSE );
}

/** @brief [FrameViewTargetTest] 볼륨 구름 기록은 지난 프레임이 구름을 그렸을 때만 잇는다 — 다시 켠 첫 프레임 · 건너뛴 프레임 뒤는 버린다 */
SW_TEST_CASE( FrameViewTargetTest, SkyCloudHistoryResetsWhenCloudsDidNotRun )
{
    FrameViewTarget view;
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._skyCloudHistory.isReset() ); // 처음 그린다
    view._skyCloudHistory.markWritten();
    view.beginViewHistory();
    SW_EXPECT_FALSE( view._skyCloudHistory.isReset() ); // 이번 프레임은 구름이 없다(쓰지 않는다)
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._skyCloudHistory.isReset() );
    view._skyCloudHistory.markWritten();
    view.markFrameSkipped();
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._skyCloudHistory.isReset() );
}

/** @brief [FrameViewTargetTest] 쉰 프레임 뒤에도 잇는 기록(자동 노출)은 컷 · 새 텍스처에서만 버리고, 번갈아 쓰는 두 칸은 쓴 프레임마다 바뀐다 */
SW_TEST_CASE( FrameViewTargetTest, KeptHistoryAlternatesAndSurvivesIdleFrames )
{
    FrameViewTarget view;
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._autoExposureHistory.isReset() ); // 쓴 적이 없다
    const RHITextureSlot* pFirst = &view._autoExposureHistory.getCurrent();
    view._autoExposureHistory.markWritten();
    SW_EXPECT_TRUE( &view._autoExposureHistory.getCurrent() != pFirst );
    SW_EXPECT_TRUE( &view._autoExposureHistory.getPrevious() == pFirst );

    view.beginViewHistory(); // 재지 않은 프레임
    view.beginViewHistory();
    SW_EXPECT_FALSE( view._autoExposureHistory.isReset() );
    SW_EXPECT_FALSE( view._autoExposureHistory.isWritten() );
    view._settings._bCut = SW_TRUE;
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._autoExposureHistory.isReset() );

    view._settings._bCut = SW_FALSE;
    view._autoExposureHistory.markWritten();
    view._autoExposureHistory.release( nullptr ); // 텍스처를 다시 만든다 — 새 텍스처의 값은 읽지 않는다
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._autoExposureHistory.isReset() );
}

/** @brief [FrameViewTargetTest] 컷이 든 프레임은 기록을 버린다 */
SW_TEST_CASE( FrameViewTargetTest, CutResetsHistory )
{
    FrameViewTarget view;
    view.beginViewHistory();
    view._settings._bCut = SW_TRUE;
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._bHistoryReset == SW_TRUE );
}
