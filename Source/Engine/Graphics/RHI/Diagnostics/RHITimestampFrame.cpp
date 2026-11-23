#include "pch.h"

#include "Engine/Graphics/RHI/Diagnostics/RHITimestampFrame.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    bool RHITimestampFrame::resolve( const uint64* pTick, const GPUTimestampSlotMask& readyMask, const GPUTimestampSlotMask& computeQueueMask, float64 nanosPerTick,
                                     RHITimestampFrame& outFrame )
    {
        outFrame._listMicro.clear();
        outFrame._originNanos      = 0;
        outFrame._computeQueueMask = computeQueueMask;
        if ( pTick == nullptr || readyMask.any() == false )
            return false;

        uint64 origin{ UINT64_MAX };
        for ( uint32 slotIndex = 0; slotIndex < constant::render::kMaxGPUTimestampSlot; ++slotIndex )
        {
            if ( readyMask.test( slotIndex ) && pTick[slotIndex] < origin )
                origin = pTick[slotIndex];
        }

        const float64 microPerTick = nanosPerTick / constant::time::kNanosecondsPerMicrosecond;
        outFrame._originNanos      = convertTickToNanos( origin, nanosPerTick );
        outFrame._listMicro.resize( constant::render::kMaxGPUTimestampSlot );
        for ( uint32 slotIndex = 0; slotIndex < constant::render::kMaxGPUTimestampSlot; ++slotIndex )
        {
            if ( readyMask.test( slotIndex ) == false )
            {
                outFrame._listMicro[slotIndex] = -1.0f;
                continue;
            }
            const uint64 ticks             = ( pTick[slotIndex] >= origin ) ? ( pTick[slotIndex] - origin ) : 0;
            outFrame._listMicro[slotIndex] = static_cast<float32>( static_cast<float64>( ticks ) * microPerTick );
        }
        return true;
    }

    int64 RHITimestampFrame::convertTickToNanos( uint64 tick, float64 nanosPerTick )
    {
        // float64 는 53비트 정밀도다. 2^53 ns(약 104 일)까지 1 ns 안이다.
        return static_cast<int64>( static_cast<float64>( tick ) * nanosPerTick );
    }

    int64 RHITimestampFrame::computeComputeQueueTickOffset( const vector<RHIQueueClockSample>& listSample, uint64 gpuFrequency, uint64 cpuFrequency, float64 sameClockToleranceMicros )
    {
        if ( listSample.empty() || gpuFrequency == 0 || cpuFrequency == 0 )
            return 0;
        vector<int64> listOffset;
        listOffset.reserve( listSample.size() );
        for ( const RHIQueueClockSample& sample : listSample )
        {
            // 틱 자체는 float64 정밀도(53 비트)를 넘을 수 있다 — 차이만 부동소수로 바꾼다(부호 없는 뺄셈 뒤 부호로 읽는다).
            const int64   cpuDelta         = static_cast<int64>( sample._computeCPUTick - sample._graphicsCPUTick );
            const int64   gpuDelta         = static_cast<int64>( sample._computeGPUTick - sample._graphicsGPUTick );
            const float64 expectedGPUDelta = static_cast<float64>( cpuDelta ) * static_cast<float64>( gpuFrequency ) / static_cast<float64>( cpuFrequency );
            listOffset.push_back( static_cast<int64>( expectedGPUDelta ) - gpuDelta );
        }
        VectorUtil::sort( listOffset );
        const int64   offset       = listOffset[listOffset.size() / 2u];
        const float64 offsetMicros = MathUtil::abs( static_cast<float64>( offset ) ) * constant::time::kMicrosecondsPerSecond / static_cast<float64>( gpuFrequency );
        return offsetMicros <= sameClockToleranceMicros ? 0 : offset;
    }
} // namespace sw
