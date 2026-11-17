#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Timeline/GPUFrameClock.h"

// GPU 프레임 시계 — 표본은 끝 - 시작, 기록되지 않은 슬롯은 표본이 없고, 타임스탬프 없는 디바이스는 Unsupported, 순번은 읽을 때마다 하나씩. GPU 없이 돈다.

using namespace sw;

namespace
{
    RHITimestampFrame makeFrame( float32 beginMicro, float32 endMicro )
    {
        RHITimestampFrame frame;
        frame._listMicro.assign( constant::render::kMaxGPUTimestampSlot, -1.0f );
        frame._listMicro[FrameRendererUtil::kGPUTimestampSlotFrameBegin] = beginMicro;
        frame._listMicro[FrameRendererUtil::kGPUTimestampSlotFrameEnd]   = endMicro;
        return frame;
    }

    /** @brief 슬롯 수와 읽을 프레임을 테스트가 정하는 타임스탬프 능력입니다. */
    class FakeTimestampProvider final : public IRHITimestampProvider
    {
    public:
        RHITimestampFrame _frame{};
        uint32            _slotCount{ constant::render::kMaxGPUTimestampSlot };
        uint8             _bEnabled  : 1;
        uint8             _bHasFrame : 1;

        FakeTimestampProvider()
            : _bEnabled{ SW_FALSE }
            , _bHasFrame{ SW_TRUE }
        {
        }

        void   setTimestampEnabled( bool bEnabled ) override { _bEnabled = bEnabled ? SW_TRUE : SW_FALSE; }
        uint32 getTimestampSlotCount() const override { return _bEnabled != SW_FALSE ? _slotCount : 0u; }
        bool   readTimestamps( RHITimestampFrame& outFrame ) override
        {
            if ( _bHasFrame == SW_FALSE )
                return false;
            outFrame = _frame;
            return true;
        }
        bool readGPUClockNanos( int64& outGPUNanos ) override
        {
            outGPUNanos = 0;
            return false;
        }
        bool isGPUClockReadCheap() const override { return true; }
    };

    bool isNear( float32 a, float32 b )
    {
        return MathUtil::abs( a - b ) < 1.0e-4f;
    }
} // namespace

/** @brief [GPUFrameClockTest] 표본은 끝 슬롯 - 시작 슬롯(ms)이고 순번은 1 부터다. */
SW_TEST_CASE( GPUFrameClockTest, SampleIsEndMinusBegin )
{
    GPUFrameClock clock;
    SW_EXPECT_EQUAL( clock.getLatest()._serial, 0u );
    SW_EXPECT_TRUE( clock.publishFrame( makeFrame( 100.0f, 4100.0f ) ) );
    const GPUFrameSample sample = clock.getLatest();
    SW_EXPECT_EQUAL( sample._serial, 1u );
    SW_EXPECT_TRUE( isNear( sample._gpuMs, 4.0f ) );
    SW_EXPECT_TRUE( clock.getState() == GPUFrameClockState::Ready );
}

/** @brief [GPUFrameClockTest] 시작이나 끝이 기록되지 않았으면(음수) 표본을 내지 않는다 — 옛 표본이 그대로다. */
SW_TEST_CASE( GPUFrameClockTest, UnrecordedSlotGivesNoSample )
{
    GPUFrameClock clock;
    SW_EXPECT_FALSE( clock.publishFrame( makeFrame( 100.0f, -1.0f ) ) );
    SW_EXPECT_FALSE( clock.publishFrame( makeFrame( -1.0f, 500.0f ) ) );
    SW_EXPECT_FALSE( clock.publishFrame( makeFrame( 500.0f, 100.0f ) ) ); // 거꾸로
    SW_EXPECT_EQUAL( clock.getLatest()._serial, 0u );
    SW_EXPECT_TRUE( clock.publishFrame( makeFrame( 0.0f, 1000.0f ) ) );
    SW_EXPECT_FALSE( clock.publishFrame( RHITimestampFrame{} ) ); // 빈 프레임
    SW_EXPECT_EQUAL( clock.getLatest()._serial, 1u );
}

/** @brief [GPUFrameClockTest] 타임스탬프 능력이 없거나, 켠 뒤에도 슬롯이 열리지 않으면 Unsupported 다. 원하지 않으면 Off 다. */
SW_TEST_CASE( GPUFrameClockTest, DeviceWithoutTimestampsIsUnsupported )
{
    GPUFrameClock clock;
    SW_EXPECT_TRUE( clock.readBack( nullptr, false ) == nullptr );
    SW_EXPECT_TRUE( clock.getState() == GPUFrameClockState::Off );
    clock.setWanted( true );
    SW_EXPECT_TRUE( clock.readBack( nullptr, false ) == nullptr );
    SW_EXPECT_TRUE( clock.getState() == GPUFrameClockState::Unsupported );

    GPUFrameClock         closedClock;
    FakeTimestampProvider provider;
    provider._slotCount = 0; // 드라이버가 쿼리를 주지 않는다
    closedClock.setWanted( true );
    SW_EXPECT_TRUE( closedClock.readBack( &provider, false ) == nullptr );
    SW_EXPECT_TRUE( provider._bEnabled != SW_FALSE );
    SW_EXPECT_TRUE( closedClock.getState() == GPUFrameClockState::WarmingUp );
    for ( uint32 frameIndex = 0; frameIndex < GPUFrameClock::kSlotOpenWaitFrames; ++frameIndex )
    {
        std::ignore = closedClock.readBack( &provider, false );
    }
    SW_EXPECT_TRUE( closedClock.getState() == GPUFrameClockState::Unsupported );
    SW_EXPECT_FALSE( closedClock.isWritingFrame() );
}

/** @brief [GPUFrameClockTest] 읽을 때마다 순번이 하나씩 오르고, 읽지 못한 프레임은 순번을 올리지 않는다. 아무도 원하지 않으면 스위치를 끈다. */
SW_TEST_CASE( GPUFrameClockTest, SerialAdvancesOncePerReadBack )
{
    GPUFrameClock         clock;
    FakeTimestampProvider provider;
    provider._frame = makeFrame( 0.0f, 2000.0f );
    clock.setWanted( true );
    SW_EXPECT_TRUE( clock.readBack( &provider, false ) != nullptr );
    SW_EXPECT_TRUE( clock.isWritingFrame() );
    SW_EXPECT_EQUAL( clock.getLatest()._serial, 1u );
    SW_EXPECT_TRUE( clock.readBack( &provider, false ) != nullptr );
    SW_EXPECT_EQUAL( clock.getLatest()._serial, 2u );

    provider._bHasFrame = SW_FALSE; // GPU 가 아직 끝내지 않았다
    SW_EXPECT_TRUE( clock.readBack( &provider, false ) == nullptr );
    SW_EXPECT_EQUAL( clock.getLatest()._serial, 2u );
    SW_EXPECT_TRUE( isNear( clock.getLatest()._gpuMs, 2.0f ) );

    // 프로파일러만 기록 중이어도 스위치는 켜져 있다. 둘 다 아니면 끈다.
    clock.setWanted( false );
    provider._bHasFrame = SW_TRUE;
    SW_EXPECT_TRUE( clock.readBack( &provider, true ) != nullptr );
    SW_EXPECT_TRUE( provider._bEnabled != SW_FALSE );
    SW_EXPECT_TRUE( clock.readBack( &provider, false ) == nullptr );
    SW_EXPECT_TRUE( provider._bEnabled == SW_FALSE );
    SW_EXPECT_TRUE( clock.getState() == GPUFrameClockState::Off );
    SW_EXPECT_FALSE( clock.isWritingFrame() );
}
