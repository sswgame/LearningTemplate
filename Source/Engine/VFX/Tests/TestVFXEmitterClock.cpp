#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Memory/Memory.h"
#include "Core/TestFramework/TestFramework.h"

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

/**
 * @brief [VFXEmitterClockTest] 단계의 배수가 아닌 반복(0.025 초 = 1.5 단계)도 끝을 넘은 몫을 다음 바퀴에 센다 — 1 초 동안 생성률 몫은 정확히 초당 개수이고, 0 초 버스트는 바퀴마다 한 번(처음 + 40 바퀴)이다.
 * @details 예전에는 되감을 때 넘은 몫을 버려 바퀴가 두 단계(0.033 초)로 늘었다 — 생성이 모자라고 버스트가 31 번이었다.
 */
SW_TEST_CASE( VFXEmitterClockTest, LoopRemainderCarriesIntoTheNextLoop )
{
    auto pDef          = make_shared<VFXEmitterDef>();
    pDef->_name        = "Sparks";
    pDef->_duration    = 0.025f;
    pDef->_bLooping    = true;
    pDef->_spawnRate   = 400.0f;
    pDef->_lifetimeMin = 0.5f;
    pDef->_lifetimeMax = 0.5f;
    VFXBurstDef burst;
    burst._time  = 0.0f;
    burst._count = 1;
    pDef->_listBurst.push_back( burst );
    VFXEmitterClock clock( pDef, 1.0f );
    for ( int32 stepIndex = 0; stepIndex < 60; ++stepIndex )
        (void)clock.spawnStep( constant::time::kDefaultFixedStepSeconds );
    // 400(생성률 1 초) + 41(버스트) — 실수 나머지가 마지막 하나를 다음 단계로 넘길 수 있어 하나를 봐준다
    SW_EXPECT_TRUE( clock.getSpawnedCount() >= 440u && clock.getSpawnedCount() <= 441u );
}

/** @brief [VFXEmitterClockTest] 프레임 시간을 오래 쌓아도 단계 수가 어긋나지 않는다 — 144 Hz 프레임 8640 번(60 초)은 3600 단계다(단계의 배수가 아닌 프레임 시간). */
SW_TEST_CASE( VFXEmitterClockTest, DueStepsDoNotDriftOverLongRuns )
{
    VFXEmitterClock clock( VFXEmitterClockTestUtil::makeLoopingDef(), 1.0f );
    uint64          stepCount = 0;
    for ( int32 frameIndex = 0; frameIndex < 8640; ++frameIndex )
        stepCount += clock.takeDueSteps( 1.0f / 144.0f );
    SW_EXPECT_TRUE( stepCount >= 3599u && stepCount <= 3600u );
}

/** @brief [VFXEmitterClockTest] 단계보다 짧은 반복은 한 단계에 지난 바퀴 수만큼 버스트 · 생성률을 냅니다(넘는 몫을 버리지 않는다) */
SW_TEST_CASE( VFXEmitterClockTest, LoopShorterThanTheStepCountsEveryLap )
{
    auto pDef       = make_shared<VFXEmitterDef>();
    pDef->_name     = "Sparks";
    pDef->_duration = 0.25f;
    pDef->_bLooping = true;
    VFXBurstDef burst;
    burst._time  = 0.0f;
    burst._count = 3;
    pDef->_listBurst.push_back( burst );
    VFXEmitterClock clock{ pDef, 1.0f };
    uint32          total = 0;
    for ( uint32 stepIndex = 0; stepIndex < 4; ++stepIndex )
        total += clock.spawnStep( 1.0f )._count; // 한 단계에 네 바퀴
    SW_EXPECT_EQUAL( 3u * 17u, total );          // 4 초 = 16 바퀴 + 열여섯째 경계에서 막 시작한 바퀴
}
