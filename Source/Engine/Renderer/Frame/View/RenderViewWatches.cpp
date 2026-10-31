/**
 * @file RenderViewWatches.cpp
 * @brief 추가 뷰 수집의 자동화 감시 값 `RenderView.HiddenCount` 입니다.
 * @details 시나리오만 읽는 감시 값이라 수집 TU 가 아니라 이 파일에 둔다 — 수집기는 진단 읽기(`RenderViewCollector::getLastHiddenViewCount`)만 연다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Renderer/Frame/View/RenderViewCollector.h"

namespace sw
{
    namespace
    {
        /** @brief 마지막 수집에서 보임 판정으로 쉰 추가 뷰 수입니다. 씬과 무관하다(수집기는 프로세스에 하나다). */
        [[nodiscard]] bool readHiddenViewCount( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            outValue = static_cast<float64>( RenderViewCollector::getLastHiddenViewCount() );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( renderViewHiddenCount, "RenderView.HiddenCount", "Extra views the visibility test rested last frame (monitor off screen, behind a wall or too small)",
                         &readHiddenViewCount );
} // namespace sw
