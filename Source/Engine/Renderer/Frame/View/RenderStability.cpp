#include "pch.h"

#include "Engine/Renderer/Frame/View/RenderStability.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Subsystem/SubsystemSlot.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/EngineSubsystems.h"

namespace sw
{
    /**
     * @class RenderStabilityStatusHolder
     * @brief 주 렌더러의 마지막 정착 값(렌더 스레드가 쓰고 게임 스레드가 읽는다)을 엔진 범위에서 듭니다 — 엔진을 다시 세우면 새로 선다(`FrameFogVolumeStatus` 와 같은 모양).
     * @details 렌더 정착 조건의 문턱(`ScenarioRun.json` 의 `_mapConditionThreshold`)도 여기 든다 — 게임 스레드가 시나리오 시작 때 쓰고(`configure*`) 조건이 읽는다. 읽기 전이면 없다(기본값이 없다).
     */
    class RenderStabilityStatusHolder final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "RenderStabilityStatus";

        RenderStabilityStatusHolder()
            : _stillFrameCount{ 0 }
            , _exposureErrorBits{ 0 }
            , _presentedFrameCount{ 0 }
            , _exposureAdaptationBits{ 0 }
            , _giAccumulatedFrames{ 0 }
            , _giErrorBits{ 0 }
            , _cameraStillFrameCount{ 0 }
            , _flags{ 0 }
            , _minStillFrames{}
            , _maxExposureErrorEv{}
            , _maxGIErrorBits{ 0 }
        {
        }

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        void store( const RenderStabilitySample& sample )
        {
            uint32 bits           = 0;
            uint32 adaptationBits = 0;
            uint32 giErrorBits    = 0;
            Memory::copy( &bits, &sample._exposureErrorEv, sizeof( bits ) );
            Memory::copy( &adaptationBits, &sample._exposureAdaptationEv, sizeof( adaptationBits ) );
            Memory::copy( &giErrorBits, &sample._giError, sizeof( giErrorBits ) );
            _stillFrameCount.store( sample._stillFrameCount, std::memory_order_relaxed );
            _exposureErrorBits.store( bits, std::memory_order_relaxed );
            _exposureAdaptationBits.store( adaptationBits, std::memory_order_relaxed );
            _giAccumulatedFrames.store( sample._giAccumulatedFrames, std::memory_order_relaxed );
            _giErrorBits.store( giErrorBits, std::memory_order_relaxed );
            _cameraStillFrameCount.store( sample._cameraStillFrameCount, std::memory_order_relaxed );
            _flags.store( ( sample._bTemporalAccumulation != SW_FALSE ? kTemporalAccumulationFlag : 0u ) | ( sample._bAutoExposure != SW_FALSE ? kAutoExposureFlag : 0u ) |
                              ( sample._bRealtimeGI != SW_FALSE ? kRealtimeGIFlag : 0u ),
                          std::memory_order_relaxed );
        }

        RenderStabilitySample load() const
        {
            RenderStabilitySample sample;
            sample._stillFrameCount = _stillFrameCount.load( std::memory_order_relaxed );
            const uint32 bits       = _exposureErrorBits.load( std::memory_order_relaxed );
            Memory::copy( &sample._exposureErrorEv, &bits, sizeof( bits ) );
            const uint32 adaptationBits = _exposureAdaptationBits.load( std::memory_order_relaxed );
            Memory::copy( &sample._exposureAdaptationEv, &adaptationBits, sizeof( adaptationBits ) );
            const uint32 giErrorBits = _giErrorBits.load( std::memory_order_relaxed );
            Memory::copy( &sample._giError, &giErrorBits, sizeof( giErrorBits ) );
            sample._giAccumulatedFrames   = _giAccumulatedFrames.load( std::memory_order_relaxed );
            sample._cameraStillFrameCount = _cameraStillFrameCount.load( std::memory_order_relaxed );
            const uint32 flags            = _flags.load( std::memory_order_relaxed );
            sample._bTemporalAccumulation = ( flags & kTemporalAccumulationFlag ) != 0 ? SW_TRUE : SW_FALSE;
            sample._bAutoExposure         = ( flags & kAutoExposureFlag ) != 0 ? SW_TRUE : SW_FALSE;
            sample._bRealtimeGI           = ( flags & kRealtimeGIFlag ) != 0 ? SW_TRUE : SW_FALSE;
            return sample;
        }

        void   addPresentedFrame() { _presentedFrameCount.fetch_add( 1, std::memory_order_relaxed ); }
        uint64 loadPresentedFrames() const { return _presentedFrameCount.load( std::memory_order_relaxed ); }

        // 게임 스레드만 쓰고 읽는다(시나리오 시작 때 `ScenarioRun.json` 에서).
        void                     setMinStillFrames( uint32 minStillFrames ) { _minStillFrames = minStillFrames; }
        void                     setMaxExposureErrorEv( float32 maxErrorEv ) { _maxExposureErrorEv = maxErrorEv; }
        void                     setMaxGIError( float32 maxError ) { _maxGIErrorBits.store( BitUtil::floatToBits( maxError ), std::memory_order_relaxed ); }
        const optional<uint32>&  getMinStillFrames() const { return _minStillFrames; }
        const optional<float32>& getMaxExposureErrorEv() const { return _maxExposureErrorEv; }
        /** @brief 렌더 스레드도 읽는다(갱신기의 정착 판정). 싣지 않았으면 0 이다 — 실을 수 있는 값은 0 보다 크다(`configureRealtimeGI`). */
        float32 getMaxGIError() const { return BitUtil::bitsToFloat( _maxGIErrorBits.load( std::memory_order_relaxed ) ); }

    private:
        static constexpr uint32 kTemporalAccumulationFlag = 1u << 0;
        static constexpr uint32 kAutoExposureFlag         = 1u << 1;
        static constexpr uint32 kRealtimeGIFlag           = 1u << 2;

        atomic<uint32>    _stillFrameCount;
        atomic<uint32>    _exposureErrorBits;      ///< `float32` 의 비트(원자 실수 대신)
        atomic<uint64>    _presentedFrameCount;    ///< 장치의 프레임 끝에 넘긴 프레임 수(`RenderPresentCounter`)
        atomic<uint32>    _exposureAdaptationBits; ///< `float32` 의 비트
        atomic<uint32>    _giAccumulatedFrames;
        atomic<uint32>    _giErrorBits; ///< `float32` 의 비트
        atomic<uint32>    _cameraStillFrameCount;
        atomic<uint32>    _flags;              ///< `kTemporalAccumulationFlag` · `kAutoExposureFlag` · `kRealtimeGIFlag`
        optional<uint32>  _minStillFrames;     ///< TAA 조건의 문턱(`_mapConditionThreshold.RenderTemporalAccumulation`)
        optional<float32> _maxExposureErrorEv; ///< 노출 조건의 문턱(`_mapConditionThreshold.RenderAutoExposure`)
        atomic<uint32>    _maxGIErrorBits;     ///< 실시간 GI 조건의 문턱(`_mapConditionThreshold.RenderRealtimeGI`)의 `float32` 비트. 싣지 않았으면 0

        SW_SUBSYSTEM_SLOT( RenderStabilityStatusHolder, Required );
    };

    uint32 RenderStabilityUtil::advanceStillFrames( uint32 previous, bool bTAAHistoryReset, bool bCameraStill, bool bObjectMotion )
    {
        const bool bStill = bTAAHistoryReset == false && bCameraStill && bObjectMotion == false;
        return bStill ? previous + 1u : 0u;
    }

    bool RenderStabilityUtil::isAccumulationReset( bool bHistoryReset, bool bTAAThisFrame, bool bTAAHistoryReset )
    {
        return bHistoryReset || ( bTAAThisFrame && bTAAHistoryReset );
    }

    float32 RenderStabilityUtil::advanceExposureError( float32 previous, bool bReset, bool bMoved, float32 rangeEv, float32 slowerAdaptRate )
    {
        const float32 range = MathUtil::max( 0.0f, rangeEv );
        if ( bReset || range <= 0.0f )
            return 0.0f;
        const float32 start = bMoved ? range : MathUtil::min( previous, range );
        return start * ( 1.0f - MathUtil::clamp( slowerAdaptRate, 0.0f, 1.0f ) );
    }

    AutomationReadiness RenderStabilitySample::evaluateTemporalAccumulation( uint32 minStillFrames ) const
    {
        if ( _bTemporalAccumulation == SW_FALSE )
            return AutomationReadiness::notApplicable();
        if ( _cameraStillFrameCount >= minStillFrames )
            return AutomationReadiness::ready();
        return AutomationReadiness::waiting( _cameraStillFrameCount, "the camera has not held still long enough for the TAA history" );
    }

    AutomationReadiness RenderStabilitySample::evaluateAutoExposure( float32 maxErrorEv ) const
    {
        if ( _bAutoExposure == SW_FALSE )
            return AutomationReadiness::notApplicable();
        if ( _exposureErrorEv <= maxErrorEv )
            return AutomationReadiness::ready();
        uint32 errorBits = 0; // 오차 상한은 정지 프레임마다 준다 — 그 비트가 진척이다(카메라가 계속 움직이면 범위 끝에 머문다)
        Memory::copy( &errorBits, &_exposureErrorEv, sizeof( errorBits ) );
        return AutomationReadiness::waiting( errorBits, "auto exposure is still adapting" );
    }

    AutomationReadiness RenderStabilitySample::evaluateRealtimeGI( float32 maxError ) const
    {
        if ( _bRealtimeGI == SW_FALSE )
            return AutomationReadiness::notApplicable();
        if ( _giError <= maxError )
            return AutomationReadiness::ready();
        return AutomationReadiness::waiting( _giAccumulatedFrames, "realtime GI is still accumulating" );
    }

    void RenderStabilitySample::publish( const RenderStabilitySample& sample )
    {
        engine::getSubsystem<RenderStabilityStatusHolder>().store( sample );
    }

    RenderStabilitySample RenderStabilitySample::readPublished()
    {
        return engine::getSubsystem<RenderStabilityStatusHolder>().load();
    }

    float32 RenderStabilitySample::readMaxGIError()
    {
        return engine::getSubsystem<RenderStabilityStatusHolder>().getMaxGIError();
    }

    void RenderStabilitySample::setMinStillFramesThreshold( uint32 minStillFrames )
    {
        engine::getSubsystem<RenderStabilityStatusHolder>().setMinStillFrames( minStillFrames );
    }

    void RenderStabilitySample::setMaxExposureErrorThreshold( float32 maxErrorEv )
    {
        engine::getSubsystem<RenderStabilityStatusHolder>().setMaxExposureErrorEv( maxErrorEv );
    }

    void RenderStabilitySample::setMaxGIErrorThreshold( float32 maxError )
    {
        engine::getSubsystem<RenderStabilityStatusHolder>().setMaxGIError( maxError );
    }

    optional<uint32> RenderStabilitySample::getMinStillFramesThreshold()
    {
        return engine::getSubsystem<RenderStabilityStatusHolder>().getMinStillFrames();
    }

    optional<float32> RenderStabilitySample::getMaxExposureErrorThreshold()
    {
        return engine::getSubsystem<RenderStabilityStatusHolder>().getMaxExposureErrorEv();
    }

    void RenderPresentCounter::addOne()
    {
        engine::getSubsystem<RenderStabilityStatusHolder>().addPresentedFrame();
    }

    uint64 RenderPresentCounter::read()
    {
        return engine::getSubsystem<RenderStabilityStatusHolder>().loadPresentedFrames();
    }

    SW_ENGINE_SUBSYSTEM_WITH_SLOT( RenderStabilityStatusHolder, HostOnly, Process, Shared, (), SW_ENGINE_DEFAULT_FACTORY( RenderStabilityStatusHolder ) );
} // namespace sw
