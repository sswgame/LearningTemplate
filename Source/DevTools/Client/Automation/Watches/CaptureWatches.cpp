/**
 * @file CaptureWatches.cpp
 * @brief 캡처 준비 판정(`CaptureReadiness`)의 자동화 감시 값 `Assets.Pending` 과, 받았지만 아직 쓰지 않은 스크린샷의 끝 차단 조건 `ScreenshotRequests` 입니다.
 * @details 둘 다 렌더러가 비우는 일이라 그 곁(`Renderer/`)에서 등록한다 — 전용 서버는 이 폴더를 짓지 않고 찍을 그림도 없다. 렌더 정착 감시 값(`Render.*`)은 `Frame/View/RenderStabilityWatches.cpp` 에 있다.
 *          에셋 일은 바깥 일(워커 로드 · GPU)이라 벽시계 감시 값이다.
 */
#include "pch.h"

#include "Engine/Automation/Scenario/AutomationCaptureUtil.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/View/ScreenshotRequestQueue.h"

namespace sw
{
    namespace
    {
        /** @brief 감시 값 `Assets.Pending` 입니다. 씬이 기다리는 에셋 일의 수입니다(`AutomationCaptureUtil::countPendingJobs`). 0 이면 화면이 다 읽힌 상태입니다. */
        [[nodiscard]] bool readAssetsPending( const GameObjectManager* pManager, float64& outValue )
        {
            outValue = static_cast<float64>( AutomationCaptureUtil::countPendingJobs( pManager ) );
            return true;
        }

        /** @brief `Assets.Pending` 의 활동 수 — 남은 일 수 그 자체입니다(로드 · 업로드 · PSO 하나가 끝날 때마다 바뀐다). */
        [[nodiscard]] uint64 readAssetsActivity( const GameObjectManager* pManager )
        {
            return AutomationCaptureUtil::countPendingJobs( pManager );
        }

        /** @brief 받았지만 아직 쓰지 않은 스크린샷(에디터 툴바 · 개발 명령 `screenshot` · 패킷 사진)이 있으면 진행 중입니다 — 시나리오가 그 파일 · 로그를 보기 전에 끝나게 한다. */
        AutomationReadiness readScreenshotRequests()
        {
            const ScreenshotRequestQueue* pMailbox = engine::findSubsystem<ScreenshotRequestQueue>();
            return pMailbox != nullptr && pMailbox->hasUnfinishedRequest() ? AutomationReadiness::waiting( 0, "a screenshot request is not written yet" ) : AutomationReadiness::ready();
        }
    } // namespace

    SW_AUTOMATION_WALL_WATCH( assetsPending, "Assets.Pending", "Meshes still loading, waiting for animation or dithering in, plus textures still loading or uploading and batches waiting for their material PSO (0 = settled)",
                              &readAssetsPending, &readAssetsActivity );
    SW_AUTOMATION_FINISH_BLOCKER( screenshotRequests, "ScreenshotRequests", &readScreenshotRequests );
} // namespace sw
