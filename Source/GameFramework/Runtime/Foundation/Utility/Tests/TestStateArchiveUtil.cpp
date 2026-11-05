#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/Countdown.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Foundation/Utility/StateArchiveUtil.h"

// StateArchiveUtilTest — 상태 구간: 기대한 표 · 버전의 구간만 읽히고(다른 버전은 거절), 표로 갈라 읽는 쪽은 아무 구간이나 받는다.
// 누적기: 음수와 터무니없는 값을 거절한다(1e30 은 빼도 줄지 않아 소비 고리가 끝나지 않는다)(nogpu).

/**
 * @brief [StateArchiveUtilTest] 결함: 걸음 타이머가 음수만 보아, 터무니없는 누적 하나로 `FixedStepTimer::consume` 이 끝나지 않았다. 한 프레임 상한까지만 받고 값을 둔다
 */
SW_TEST_CASE( StateArchiveUtilTest, StepTimerRefusesAccumulatorPastOneFrame )
{
    const auto readForged = []( float32 accumulator, sw::FixedStepTimer& inoutTimer )
    {
        sw::Archive written;
        written << accumulator;
        sw::Archive reading( written.getData(), written.getSize() );
        return sw::StateArchiveUtil::readStepTimer( reading, inoutTimer );
    };
    sw::FixedStepTimer timer( 0.05f, 0.25f );
    SW_REQUIRE_TRUE( readForged( 0.03f, timer ) );
    SW_EXPECT_NEAR( 0.03f, timer._accumulator, 1.0e-6f );
    SW_EXPECT_TRUE( readForged( 0.2f, timer ) ); // 걸음을 줄인 핫 리로드는 그대로 읽힌다
    SW_EXPECT_TRUE( readForged( 0.03f, timer ) );

    SW_EXPECT_FALSE( readForged( -0.1f, timer ) );
    SW_EXPECT_FALSE( readForged( 0.26f, timer ) );
    SW_EXPECT_FALSE( readForged( 1.0e30f, timer ) );
    SW_EXPECT_NEAR( 0.03f, timer._accumulator, 1.0e-6f );

    sw::FixedStepTimer longStep( 2.0f, 0.25f ); // 걸음이 한 프레임 상한보다 길면 걸음까지 찬다
    SW_EXPECT_TRUE( readForged( 1.5f, longStep ) );
    SW_EXPECT_FALSE( readForged( 2.5f, longStep ) );
}

/**
 * @brief [StateArchiveUtilTest] 결함: 비율 누적이 음수만 보아, 터무니없는 값 하나로 `takeOne` 고리가 끝나지 않았다. 0..1 밖을 거절하고 값을 둔다
 */
SW_TEST_CASE( StateArchiveUtilTest, RateAccumulatorRefusesWholeAmounts )
{
    const auto readForged = []( float32 fraction, sw::RateAccumulator& inoutAccumulator )
    {
        sw::Archive written;
        written << fraction;
        sw::Archive reading( written.getData(), written.getSize() );
        return sw::StateArchiveUtil::readRateAccumulator( reading, inoutAccumulator );
    };
    sw::RateAccumulator accumulator;
    SW_REQUIRE_TRUE( readForged( 0.5f, accumulator ) );
    SW_EXPECT_NEAR( 0.5f, accumulator.getFraction(), 1.0e-6f );

    SW_EXPECT_FALSE( readForged( -0.1f, accumulator ) );
    SW_EXPECT_FALSE( readForged( 1.0f, accumulator ) );
    SW_EXPECT_FALSE( readForged( 1.0e30f, accumulator ) );
    SW_EXPECT_NEAR( 0.5f, accumulator.getFraction(), 1.0e-6f );
}

namespace
{
    constexpr uint32 kTag = 0x54534554u; // "TEST"

    sw::Archive makeSection( uint32 version, int32 value )
    {
        sw::Archive body;
        body << value;
        sw::Archive outer;
        sw::StateArchiveUtil::writeSection( outer, kTag, version, body );
        return outer;
    }
} // namespace

/**
 * @brief [StateArchiveUtilTest] readExpectedSection 은 표와 버전이 모두 같을 때만 본문을 주고, 버전 · 표가 다르면 거절한다(옛 판을 새 판으로 읽지 않는다)
 */
SW_TEST_CASE( StateArchiveUtilTest, ExpectedSectionRejectsOtherVersionOrTag )
{
    const sw::Archive written = makeSection( 2, 42 );

    sw::Archive same( written.getData(), written.getSize() );
    sw::Archive body;
    SW_REQUIRE_TRUE( sw::StateArchiveUtil::readExpectedSection( same, kTag, 2, body ) );
    int32 value = 0;
    body >> value;
    SW_EXPECT_EQUAL( 42, value );
    SW_EXPECT_EQUAL( size_t{ 0 }, same.getRemainingBytes() );

    sw::Archive oldVersion( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( sw::StateArchiveUtil::readExpectedSection( oldVersion, kTag, 1, body ) );
    sw::Archive otherTag( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( sw::StateArchiveUtil::readExpectedSection( otherTag, kTag + 1, 2, body ) );

    sw::Archive any( written.getData(), written.getSize() );
    uint32      tag     = 0;
    uint32      version = 0;
    SW_REQUIRE_TRUE( sw::StateArchiveUtil::readSection( any, tag, version, body ) );
    SW_EXPECT_EQUAL( kTag, tag );
    SW_EXPECT_EQUAL( 2u, version );
}
