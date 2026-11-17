/**
 * @file AutomationCaptureWatches.cpp
 * @brief 캡처 준비 판정(`CaptureReadiness`)의 자동화 감시 값 `Assets.Pending` 입니다. 렌더 정착 감시 값(`Render.*`)은 클라이언트만 짓는 `Client/AutomationRenderWatches.cpp` 에 있다(전용 서버는 렌더러를 짓지 않는다).
 * @details 에셋 일은 바깥 일(워커 로드 · GPU)이라 벽시계 감시 값이다.
 */
#include "pch.h"

#include "Engine/Automation/AutomationCaptureUtil.h"
#include "Engine/Automation/Watch/AutomationWatch.h"

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
    } // namespace

    SW_AUTOMATION_WALL_WATCH( assetsPending, "Assets.Pending", "Meshes still loading, waiting for animation or dithering in, plus textures still loading or uploading and batches waiting for their material PSO (0 = settled)",
                              &readAssetsPending );
} // namespace sw
