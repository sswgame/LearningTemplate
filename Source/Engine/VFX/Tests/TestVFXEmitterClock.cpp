#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/VFX/VFXEmitterClock.h"

using namespace sw;

namespace
{
    struct VFXEmitterClockTestUtil
    {
        /** @brief 2 초 수명 · 초당 30 개 · 1 초 반복 방출기입니다(버스트 하나 — 반복마다 다시 켜진다). */
        static shared_ptr<const VFXEmitterDef> makeLoopingDef()
        {
            auto pDef          = make_shared<VFXEmitterDef>();
            pDef->_name        = "Torch";
            pDef->_duration    = 1.0f;
            pDef->_bLooping    = true;
            pDef->_spawnRate   = 30.0f;
            pDef->_lifetimeMin = 1.0f;
            pDef->_lifetimeMax = 2.0f;
            VFXBurstDef burst;
            burst._time  = 0.25f;
            burst._count = 5;
            pDef->_listBurst.push_back( burst );
            return pDef;
        }
    };
} // namespace

/** @brief [VFXEmitterClockTest] 3600 단계 밀린 재생을 다시 실으면 디스패치할 단계가 수명 창 이하로 줄고, 그 뒤 일정은 처음부터 돈 시계와 같습니다(RN-9). */
SW_TEST_CASE( VFXEmitterClockTest, SkipExpiredStepsKeepsScheduleWithinLifetimeWindow )
{
    const float32                         step   = constant::time::kDefaultFixedStepSeconds;
    const shared_ptr<const VFXEmitterDef> pDef   = VFXEmitterClockTestUtil::makeLoopingDef();
    const uint64                          window = VFXEmitterClock::getLifetimeWindowSteps( *pDef, step );
    constexpr uint64                      kTarget{ 3600 };
    SW_REQUIRE_TRUE( window < kTarget );

    VFXEmitterClock skipped{ pDef, 1.0f };
    const uint64    skipCount = skipped.skipExpiredSteps( 0u, kTarget, step );
    SW_EXPECT_EQUAL( kTarget - skipCount, window );

    VFXEmitterClock full{ pDef, 1.0f };
    for ( uint64 stepIndex = 0; stepIndex < skipCount; ++stepIndex )
        static_cast<void>( full.spawnStep( step ) );
    for ( uint64 stepIndex = skipCount; stepIndex < kTarget; ++stepIndex )
    {
        const VFXSpawnStep expected = full.spawnStep( step );
        const VFXSpawnStep actual   = skipped.spawnStep( step );
        SW_EXPECT_EQUAL( actual._count, expected._count );
        SW_EXPECT_EQUAL( actual._firstParticleIndex, expected._firstParticleIndex );
    }
}

/** @brief [VFXEmitterClockTest] 밀린 단계가 수명 창 안이면 건너뛰지 않습니다(처음 실린 재생 · 매 프레임 이어 도는 재생). */
SW_TEST_CASE( VFXEmitterClockTest, SkipExpiredStepsKeepsStepsInsideWindow )
{
    const float32                         step   = constant::time::kDefaultFixedStepSeconds;
    const shared_ptr<const VFXEmitterDef> pDef   = VFXEmitterClockTestUtil::makeLoopingDef();
    const uint64                          window = VFXEmitterClock::getLifetimeWindowSteps( *pDef, step );
    VFXEmitterClock                       clock{ pDef, 1.0f };
    SW_EXPECT_EQUAL( clock.skipExpiredSteps( 0u, window, step ), 0u );
    SW_EXPECT_EQUAL( clock.skipExpiredSteps( 100u, 100u + window, step ), 0u );
    SW_EXPECT_EQUAL( clock.getSpawnedCount(), 0u );
}
