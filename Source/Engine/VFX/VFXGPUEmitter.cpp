#include "pch.h"

#include "Engine/VFX/VFXGPUEmitter.h"

#include "Core/Concurrency/atomic.h"

namespace sw
{
    namespace
    {
        /** @brief 프로세스 고유 번호 발급기 — 상태가 아니라 번호 공간이다(상수 초기화라 정적 초기화 순서와 무관하다). 함수 정적에 숨기지 않고 파일 범위에 둔다. */
        atomic<uint64> s_vfxGPUEmitterNextRunID{ 1 };
    } // namespace

    namespace
    {
        /** @brief 다음 재생 번호입니다(0 은 "재생 없음"). 프로세스 안에서 다시 쓰지 않는다 — 렌더 스레드의 GPU 상태 열쇠다. */
        [[nodiscard]] uint64 allocateRunID()
        {
            return s_vfxGPUEmitterNextRunID.fetch_add( 1, std::memory_order_relaxed );
        }
    } // namespace
} // namespace sw

namespace sw
{
    VFXGPUEmitter::VFXGPUEmitter( shared_ptr<const VFXEmitterDef> pDef, uint64 seed, float32 spawnScale )
        : _clock{ std::move( pDef ), spawnScale }
        , _userValues{}
        , _seed{ seed }
        , _runID{ allocateRunID() }
        , _stepCount{ 0 }
        , _lastSpawnStep{ 0 }
    {
        SW_ASSERT( getDef().computeSupportedTargets().has( VFXSimulationTarget::GPU ) );
        // "끝났다" 를 일정에서 구하는 근거다 — 수명을 늘리는 모듈이 생기면 모듈 표가 그렇다고 선언하고 여기서 멈춘다.
        SW_ASSERT( getDef().isLifetimeBoundedByMax() );
    }

    void VFXGPUEmitter::update( float32 deltaSeconds )
    {
        const uint32 stepCount = _clock.takeDueSteps( deltaSeconds );
        for ( uint32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            ++_stepCount;
            if ( _clock.spawnStep( constant::time::kDefaultFixedStepSeconds )._count > 0 )
                _lastSpawnStep = _stepCount;
        }
    }

    void VFXGPUEmitter::restart()
    {
        _clock.restart();
        _runID         = allocateRunID();
        _stepCount     = 0;
        _lastSpawnStep = 0;
    }

    bool VFXGPUEmitter::isFinished() const
    {
        if ( _clock.isEmitting() )
            return false;
        if ( _lastSpawnStep == 0 )
            return true;
        // 마지막으로 만든 입자는 그 단계부터 단계마다 한 간격씩 나이를 먹는다. 가장 긴 수명에 한 단계를 더한 여유 — 나이는 float 덧셈으로 쌓인다.
        const float32 oldestAgeSeconds = static_cast<float32>( _stepCount - _lastSpawnStep + 1 ) * constant::time::kDefaultFixedStepSeconds;
        return oldestAgeSeconds >= getDef()._lifetimeMax + constant::time::kDefaultFixedStepSeconds;
    }

    VFXGPUEmitterRequest VFXGPUEmitter::makeRequest() const
    {
        VFXGPUEmitterRequest request{};
        request._pDef       = _clock.getSharedDef();
        request._runID      = _runID;
        request._seed       = _seed;
        request._stepCount  = _stepCount;
        request._spawnScale = _clock.getSpawnScale();
        request._userValues = _userValues;
        return request;
    }
} // namespace sw
