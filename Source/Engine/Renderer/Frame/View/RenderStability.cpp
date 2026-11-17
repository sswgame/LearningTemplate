#include "pch.h"

#include "Engine/Renderer/Frame/View/RenderStability.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    /**
     * @class RenderStabilityStatusHolder
     * @brief 주 렌더러의 마지막 정착 값(렌더 스레드가 쓰고 게임 스레드가 읽는다)을 엔진 범위에서 듭니다 — 엔진을 다시 세우면 새로 선다(`FrameFogVolumeStatus` 와 같은 모양).
     */
    class RenderStabilityStatusHolder final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "RenderStabilityStatus";

        RenderStabilityStatusHolder()
            : _stillFrameCount{ 0 }
            , _exposureErrorBits{ 0 }
        {
        }

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        void store( const RenderStabilitySample& sample )
        {
            uint32 bits = 0;
            Memory::copy( &bits, &sample._exposureErrorEv, sizeof( bits ) );
            _stillFrameCount.store( sample._stillFrameCount, std::memory_order_relaxed );
            _exposureErrorBits.store( bits, std::memory_order_relaxed );
        }

        RenderStabilitySample load() const
        {
            RenderStabilitySample sample;
            sample._stillFrameCount = _stillFrameCount.load( std::memory_order_relaxed );
            const uint32 bits       = _exposureErrorBits.load( std::memory_order_relaxed );
            Memory::copy( &sample._exposureErrorEv, &bits, sizeof( bits ) );
            return sample;
        }

    private:
        atomic<uint32> _stillFrameCount;
        atomic<uint32> _exposureErrorBits; ///< `float32` 의 비트(원자 실수 대신)

        SW_SERVICE_SLOT( RenderStabilityStatusHolder, Required );
    };

    uint32 RenderStabilityUtil::advanceStillFrames( uint32 previous, bool bTAAHistoryReset, bool bCameraStill, bool bObjectMotion )
    {
        const bool bStill = bTAAHistoryReset == false && bCameraStill && bObjectMotion == false;
        return bStill ? previous + 1u : 0u;
    }

    float32 RenderStabilityUtil::advanceExposureError( float32 previous, bool bReset, bool bMoved, float32 rangeEv, float32 slowerAdaptRate )
    {
        const float32 range = MathUtil::max( 0.0f, rangeEv );
        if ( bReset || range <= 0.0f )
            return 0.0f;
        const float32 start = bMoved ? range : MathUtil::min( previous, range );
        return start * ( 1.0f - MathUtil::clamp( slowerAdaptRate, 0.0f, 1.0f ) );
    }

    bool RenderStabilityUtil::isStable( const RenderStabilitySample& sample, uint32 pendingJobCount, const RenderStabilityThreshold& threshold )
    {
        return pendingJobCount == 0 && sample._stillFrameCount >= threshold._minStillFrames && sample._exposureErrorEv <= threshold._maxExposureErrorEv;
    }

    void RenderStabilitySample::publish( const RenderStabilitySample& sample )
    {
        engine::getSubsystem<RenderStabilityStatusHolder>().store( sample );
    }

    RenderStabilitySample RenderStabilitySample::readPublished()
    {
        return engine::getSubsystem<RenderStabilityStatusHolder>().load();
    }

    SW_ENGINE_SERVICE_SUBSYSTEM( RenderStabilityStatusHolder, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( RenderStabilityStatusHolder ) );
} // namespace sw
