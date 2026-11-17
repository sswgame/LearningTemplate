#include "pch.h"

#include "Engine/Graphics/View/SimulationOnlyFrameQueue.h"

#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    bool SimulationOnlyFrameQueue::request( uint32 frameCount, float32 fixedFrameDelta )
    {
        if ( fixedFrameDelta <= 0.0f )
        {
            SW_LOG_ERROR_ID( "SimulationOnlyFrameQueue.NeedsFixedDelta", "Advancing %# frames without drawing needs a fixed frame delta (scenario fixedDelta) - the wall-clock frame time would change the result", frameCount );
            return false;
        }
        _remainingCount += frameCount;
        return true;
    }

    FrameDrawMode SimulationOnlyFrameQueue::beginFrame( float32 fixedFrameDelta )
    {
        if ( _remainingCount > 0 && fixedFrameDelta <= 0.0f )
        {
            SW_LOG_ERROR_ID( "SimulationOnlyFrameQueue.FixedDeltaReleased", "The fixed frame delta was released with %# frames left to advance without drawing - drawing them", _remainingCount );
            _remainingCount = 0;
        }
        if ( _remainingCount == 0 )
        {
            _bLastSimulationOnly = 0;
            return FrameDrawMode::Draw;
        }
        --_remainingCount;
        _bLastSimulationOnly = 1;
        (void)_skippedSerial.fetch_add( 1, std::memory_order_acq_rel );
        return FrameDrawMode::SimulationOnly;
    }

    void SimulationOnlyFrameQueue::reportDrawnFrame( uint64 packetSkippedSerial, uint64 pendingCatchUpSteps )
    {
        if ( pendingCatchUpSteps != 0 )
            return;
        // 패킷은 지은 차례로 실행된다 — 번호는 늘기만 한다. 더 앞선 번호로 되돌리지 않는다.
        uint64 completed = _completedSerial.load( std::memory_order_acquire );
        while ( completed < packetSkippedSerial && _completedSerial.compare_exchange_weak( completed, packetSkippedSerial, std::memory_order_acq_rel, std::memory_order_acquire ) == false )
        {
        }
    }
} // namespace sw

namespace sw
{
    // 그리지 않고 진행하는 고정 걸음 — 클라이언트 전용(렌더러가 있는 호스트).
    SW_ENGINE_SERVICE_SUBSYSTEM( SimulationOnlyFrameQueue, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( SimulationOnlyFrameQueue ) );
} // namespace sw
