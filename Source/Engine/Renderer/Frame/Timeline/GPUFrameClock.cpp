#include "pch.h"

#include "Engine/Renderer/Frame/Timeline/GPUFrameClock.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Renderer/Frame/Timeline/GPUTimestampSlotLayout.h"

#include <cstring>

namespace sw
{
    namespace
    {
        // 시작 · 끝 슬롯 둘 중 큰 번호가 열려 있어야 둘 다 쓴다.
        constexpr uint32 kHighestClockSlot = GPUTimestampSlotLayout::kFrameBegin > GPUTimestampSlotLayout::kFrameEnd ? GPUTimestampSlotLayout::kFrameBegin
                                                                                                                     : GPUTimestampSlotLayout::kFrameEnd;

        uint64 packSample( uint32 serial, float32 gpuMs )
        {
            uint32 msBits{ 0 };
            std::memcpy( &msBits, &gpuMs, sizeof( msBits ) );
            return ( static_cast<uint64>( serial ) << 32u ) | static_cast<uint64>( msBits );
        }
    } // namespace

    GPUFrameClock::GPUFrameClock()
        : _frame{}
        , _packedSample{ 0 }
        , _state{ static_cast<uint8>( GPUFrameClockState::Off ) }
        , _bWanted{ SW_FALSE }
        , _closedSlotFrames{ 0 }
        , _nextSerial{ 1 }
        , _bWritingFrame{ SW_FALSE }
    {
    }

    void GPUFrameClock::setWanted( bool bWanted )
    {
        _bWanted.store( bWanted ? SW_TRUE : SW_FALSE, std::memory_order_relaxed );
    }

    void GPUFrameClock::writeFrameBegin( IRHICommandList& cmd ) const
    {
        if ( _bWritingFrame != SW_FALSE )
            cmd.writeTimestamp( GPUTimestampSlotLayout::kFrameBegin );
    }

    void GPUFrameClock::writeFrameEnd( IRHICommandList& cmd ) const
    {
        if ( _bWritingFrame != SW_FALSE )
            cmd.writeTimestamp( GPUTimestampSlotLayout::kFrameEnd );
    }

    const RHITimestampFrame* GPUFrameClock::readBack( IRHITimestampProvider* pTimestamps, bool bProfilerRecording )
    {
        const bool bWanted  = _bWanted.load( std::memory_order_relaxed ) != SW_FALSE;
        const bool bEnabled = bWanted || bProfilerRecording;
        _bWritingFrame      = SW_FALSE;
        if ( pTimestamps == nullptr )
        {
            setState( bWanted ? GPUFrameClockState::Unsupported : GPUFrameClockState::Off );
            return nullptr;
        }
        // **여기가 계측 스위치다.** 아무도 원하지 않으면 백엔드는 쿼리 자원조차 만들지 않는다.
        pTimestamps->setTimestampEnabled( bEnabled );
        if ( bEnabled == false )
        {
            _closedSlotFrames = 0;
            setState( GPUFrameClockState::Off );
            return nullptr;
        }
        if ( pTimestamps->getTimestampSlotCount() <= kHighestClockSlot )
        {
            // 켠 다음 프레임에 자원이 생긴다. 넉넉히 기다려도 열리지 않으면 드라이버가 지원하지 않는 것이다.
            if ( _closedSlotFrames < kSlotOpenWaitFrames )
                ++_closedSlotFrames;
            const bool bGaveUp = _closedSlotFrames >= kSlotOpenWaitFrames;
            setState( bGaveUp ? GPUFrameClockState::Unsupported : GPUFrameClockState::WarmingUp );
            return nullptr;
        }
        _closedSlotFrames = 0;
        _bWritingFrame    = SW_TRUE; // 다음 프레임부터 시작 · 끝을 쓴다
        if ( getState() != GPUFrameClockState::Ready )
            setState( GPUFrameClockState::WarmingUp );
        if ( pTimestamps->readTimestamps( _frame ) == false )
            return nullptr;
        // 표본이 서지 않아도(프로파일러만 켰던 프레임 등) 프로파일러는 읽은 프레임을 받는다.
        std::ignore = publishFrame( _frame );
        return &_frame;
    }

    bool GPUFrameClock::publishFrame( const RHITimestampFrame& frame )
    {
        const vector<float32>& listMicro = frame._listMicro;
        if ( kHighestClockSlot >= listMicro.size() )
            return false;
        const float32 beginMicro = listMicro[GPUTimestampSlotLayout::kFrameBegin];
        const float32 endMicro   = listMicro[GPUTimestampSlotLayout::kFrameEnd];
        // 음수는 그 프레임에 기록되지 않았다는 표시다(백엔드 약속). 한쪽만 음수여도 구간이 서지 않는다.
        if ( beginMicro < 0.0f || endMicro < 0.0f || endMicro < beginMicro )
            return false;
        const float32 gpuMs = ( endMicro - beginMicro ) / static_cast<float32>( constant::time::kMicrosecondsPerMillisecond );
        _packedSample.store( packSample( _nextSerial, gpuMs ), std::memory_order_release );
        // 0 은 "아직 없음" 이라 돌아 넘을 때 건너뛴다.
        _nextSerial = _nextSerial == 0xFFFFFFFFu ? 1u : _nextSerial + 1u;
        setState( GPUFrameClockState::Ready );
        return true;
    }

    void GPUFrameClock::clearDevice()
    {
        _bWritingFrame    = SW_FALSE;
        _closedSlotFrames = 0;
    }

    GPUFrameClockState GPUFrameClock::getState() const
    {
        return static_cast<GPUFrameClockState>( _state.load( std::memory_order_relaxed ) );
    }

    void GPUFrameClock::setState( GPUFrameClockState state )
    {
        _state.store( static_cast<uint8>( state ), std::memory_order_relaxed );
    }

    GPUFrameSample GPUFrameClock::getLatest() const
    {
        const uint64   packed = _packedSample.load( std::memory_order_acquire );
        GPUFrameSample sample;
        sample._serial      = static_cast<uint32>( packed >> 32u );
        const uint32 msBits = static_cast<uint32>( packed & 0xFFFFFFFFu );
        std::memcpy( &sample._gpuMs, &msBits, sizeof( msBits ) );
        return sample;
    }
} // namespace sw
