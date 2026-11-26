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
    view._listAttachmentHistory.emplace_back()._attachment = hashed_string( "TAAColor" );
    FrameViewHistory& taaHistory                           = *view.findAttachmentHistory( hashed_string( "TAAColor" ) );
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._bHistoryReset == SW_TRUE ); // 처음 그린다
    taaHistory.markWritten();
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._bHistoryReset == SW_FALSE );
    SW_EXPECT_FALSE( taaHistory.isReset() );

    view.markFrameSkipped(); // 10 Hz 뷰 — 다음 몇 프레임은 그리지 않는다
    view.markFrameSkipped();
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._bHistoryReset == SW_TRUE );
    SW_EXPECT_TRUE( taaHistory.isReset() );

    view.beginViewHistory(); // 다시 이어 그리면 기록을 쓴다
    SW_EXPECT_TRUE( view._bHistoryReset == SW_FALSE );
}

/** @brief [FrameViewTargetTest] 볼륨 구름 기록은 지난 프레임이 구름을 그렸을 때만 잇는다 — 다시 켠 첫 프레임 · 건너뛴 프레임 뒤는 버린다 */
SW_TEST_CASE( FrameViewTargetTest, SkyCloudHistoryResetsWhenCloudsDidNotRun )
{
    FrameViewTarget view;
    view._listAttachmentHistory.emplace_back()._attachment = hashed_string( "SkyCloudColor" );
    FrameViewHistory& cloudHistory                         = *view.findAttachmentHistory( hashed_string( "SkyCloudColor" ) );
    view.beginViewHistory();
    SW_EXPECT_TRUE( cloudHistory.isReset() ); // 처음 그린다
    cloudHistory.markWritten();
    view.beginViewHistory();
    SW_EXPECT_FALSE( cloudHistory.isReset() ); // 이번 프레임은 구름이 없다(쓰지 않는다)
    view.beginViewHistory();
    SW_EXPECT_TRUE( cloudHistory.isReset() );
    cloudHistory.markWritten();
    view.markFrameSkipped();
    view.beginViewHistory();
    SW_EXPECT_TRUE( cloudHistory.isReset() );
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

/**
 * @brief [FrameViewTargetTest] 컷이 든 프레임은 뷰 기록과 기록 첨부(XML 이 더한 것도)를 모두 버리고, 다음 프레임부터 다시 잇는다
 * @details 변이: `beginViewHistory` 가 기록 첨부에 뷰의 버림을 넘기지 않으면 지난 프레임에 쓴 첨부가 컷 프레임에 지난 그림을 읽어 둘째 단언이 진다.
 */
SW_TEST_CASE( FrameViewTargetTest, CutResetsHistory )
{
    FrameViewTarget view;
    view._listAttachmentHistory.emplace_back()._attachment = hashed_string( "TrailColor" );
    FrameViewHistory& trailHistory                         = *view.findAttachmentHistory( hashed_string( "TrailColor" ) );
    view.beginViewHistory();
    trailHistory.markWritten();
    view._settings._bCut = SW_TRUE;
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._bHistoryReset == SW_TRUE );
    SW_EXPECT_TRUE( trailHistory.isReset() );
    trailHistory.markWritten();
    view._settings._bCut = SW_FALSE;
    view.beginViewHistory();
    SW_EXPECT_FALSE( trailHistory.isReset() );
}

/**
 * @brief [FrameViewTargetTest] 새로 세운 뷰 대상(시나리오 시작 프레임의 `FrameRenderer::discardViewTargets`)은 첫 프레임에 기록이 없고 정지 프레임도 0 부터 센다
 * @details 시작 전 프레임 수가 실행마다 달라도 사진이 같으려면 시작 프레임의 뷰 상태가 늘 같아야 한다. 변이: 새 뷰 대상의 `_bHasHistory` 를 세우면 첫 단언이 진다.
 */
SW_TEST_CASE( FrameViewTargetTest, FreshViewTargetStartsWithoutHistory )
{
    FrameViewTarget view;
    view._listAttachmentHistory.emplace_back()._attachment = hashed_string( "TrailColor" );
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._bHistoryReset == SW_TRUE );
    SW_EXPECT_TRUE( view.findAttachmentHistory( hashed_string( "TrailColor" ) )->isReset() );
    SW_EXPECT_TRUE( view._autoExposureHistory.isReset() );
    SW_EXPECT_EQUAL( 0u, view._stillFrameCount );
    SW_EXPECT_EQUAL( 0u, view._cameraStillFrameCount );
}

/**
 * @brief [FrameViewTargetTest] 기록은 첨부 이름으로 찾고, 하나라도 쌓이면 시간 누적이다 — 패스 종류를 보지 않으므로 XML 이 더한 기록 첨부도 같은 길이다
 * @details 변이: 시간 누적 판정이 첫 칸만 보면 둘째 칸(사용자 기록)만 쌓인 프레임에서 마지막 단언이 진다.
 */
SW_TEST_CASE( FrameViewTargetTest, AttachmentHistoriesAreFoundByName )
{
    FrameViewTarget view;
    view._listAttachmentHistory.emplace_back()._attachment = hashed_string( "TAAColor" );
    view._listAttachmentHistory.emplace_back()._attachment = hashed_string( "UserTrail" );
    SW_EXPECT_TRUE( view.findAttachmentHistory( hashed_string( "SceneColor" ) ) == nullptr );
    SW_EXPECT_FALSE( view.hasWrittenAttachmentHistory() );
    view.findAttachmentHistory( hashed_string( "UserTrail" ) )->markWritten();
    SW_EXPECT_TRUE( view.hasWrittenAttachmentHistory() );
}
