/**
 * @file AssetArrivalTestUtil.h
 * @brief 워커가 읽는 에셋(메시 · 애니메이션 클립)이 도착할 때까지 씬 프레임의 도착 단계를 돌리는 테스트 도우미입니다.
 */
#pragma once
#include "Core/Time/MonotonicClock.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"

#include <thread>

namespace test
{
    /**
     * @brief 에셋을 기다리는 프리미티브(메시 로드 · 애니메이션 클립 · 등장 비율)가 없을 때까지 씬 프레임의 도착 단계(`AssetArrival`)를 돌립니다.
     * @details 스폰 · 시작(`dispatchBeginPlay`)은 디스크를 기다리지 않는다 — 클립 · 메시는 워커가 읽고, 다 읽히면 도착 단계가 넘긴다. 씬 프레임을 돌리지 않고
     *          컴포넌트를 바로 모는 테스트는 시작 뒤에 이것을 불러 런타임과 같은 순서로 준비를 마친다. 등장 비율은 한 번에 끝낸다(큰 프레임 시간).
     * @return 10 초 안에 모두 도착했으면 true 입니다.
     */
    [[nodiscard]] inline bool finishAssetArrivals( sw::GameObjectManager& manager )
    {
        constexpr float32      kOneShotFrameSeconds = 60.0f;
        sw::PrimitiveRegistry& registry             = manager.getPrimitiveRegistry();
        const sw::Deadline     deadline             = sw::Deadline::afterMilliseconds( 10000 );
        while ( registry.countAssetWaiters() > 0 )
        {
            registry.updateAssetWaiters( kOneShotFrameSeconds );
            if ( registry.countAssetWaiters() == 0 )
                break;
            if ( deadline.isExpired() )
                return false;
            std::this_thread::yield(); // 로드는 워커가 한다
        }
        return true;
    }
} // namespace test
