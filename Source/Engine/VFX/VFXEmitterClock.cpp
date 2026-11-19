#include "pch.h"

#include "Engine/VFX/VFXEmitterClock.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    VFXEmitterClock::VFXEmitterClock( shared_ptr<const VFXEmitterDef> pDef, float32 spawnScale )
        : _pDef{ std::move( pDef ) }
        , _listBurstFired{}
        , _spawnedCount{ 0 }
        , _emitterSeconds{ 0.0 }
        , _spawnCarry{ 0.0f }
        , _spawnScale{ spawnScale }
        , _pendingSeconds{ 0.0 }
        , _bEmitting{ SW_TRUE }
        , _bWarnedStepLimit{ SW_FALSE }
    {
        if ( _pDef == nullptr )
            ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
        SW_ASSERT( _spawnScale >= 0.0f );
        _listBurstFired.resize( _pDef->_listBurst.size(), 0 );
    }

    uint32 VFXEmitterClock::takeDueSteps( float32 deltaSeconds )
    {
        SW_ASSERT( deltaSeconds >= 0.0f );
        if ( deltaSeconds <= 0.0f )
            return 0;

        const float64 stepSeconds = static_cast<float64>( constant::time::kDefaultFixedStepSeconds );
        _pendingSeconds += static_cast<float64>( deltaSeconds );
        const uint32 dueStepCount = static_cast<uint32>( _pendingSeconds / stepSeconds );
        _pendingSeconds -= static_cast<float64>( dueStepCount ) * stepSeconds;
        if ( dueStepCount <= kMaxStepsPerUpdate )
            return dueStepCount;
        if ( _bWarnedStepLimit == false )
        {
            SW_LOG_WARNING_ID( "VFX.StepsDropped", "VFX emitter '%#': frame time %# s needs %# steps, running %# and dropping the rest", _pDef->_name, deltaSeconds, dueStepCount, kMaxStepsPerUpdate );
            _bWarnedStepLimit = SW_TRUE;
        }
        return kMaxStepsPerUpdate;
    }

    VFXSpawnStep VFXEmitterClock::spawnStep( float32 stepSeconds )
    {
        SW_ASSERT( stepSeconds > 0.0f );
        VFXSpawnStep spawn{};
        spawn._firstParticleIndex = _spawnedCount;
        if ( _bEmitting == false )
            return spawn;

        const VFXEmitterDef& def             = *_pDef;
        const float64        duration        = static_cast<float64>( def._duration );
        const float64        previousSeconds = _emitterSeconds;
        _emitterSeconds += static_cast<float64>( stepSeconds );

        // 버스트와 생성률은 생성 배율(이펙트 품질)을 곱해 같은 나머지에 쌓는다 — 배율이 1 이 아니어도 작은 버스트가 반올림으로 사라지지 않고 다음 몫에 이어진다.
        accumulateDueBurst();
        // 생성률은 방출 시간(0.._duration) 안에서만 쌓는다.
        accumulateEmission( MathUtil::min( _emitterSeconds, duration ) - MathUtil::min( previousSeconds, duration ) );

        if ( _emitterSeconds >= duration )
        {
            if ( def._bLooping )
            {
                // 끝을 넘은 몫은 다음 바퀴의 시간이다 — 그 몫의 버스트 · 생성률을 이 단계에 센다(바퀴 경계에서 0 번 · 한 단계 늦음이 없다).
                // shortcut: 바퀴가 단계보다 짧으면 몫이 바퀴를 넘는다 — 한 단계에 한 바퀴만 세고 넘는 몫은 버린다. 단계보다 짧은 반복이 데이터에 나오면 바퀴 수만큼 센다.
                _emitterSeconds = MathUtil::min( _emitterSeconds - duration, duration );
                _listBurstFired.assign( def._listBurst.size(), 0 );
                accumulateDueBurst();
                accumulateEmission( _emitterSeconds );
            }
            else
            {
                _bEmitting = SW_FALSE;
            }
        }

        const float32 wholeCount = MathUtil::floor( _spawnCarry );
        _spawnCarry -= wholeCount;
        spawn._count += static_cast<uint32>( wholeCount );
        // 만들지 못한 입자도 번호를 쓴다 — 풀 상태가 나머지 입자의 난수를 바꾸지 않게 한다.
        _spawnedCount += spawn._count;
        return spawn;
    }

    void VFXEmitterClock::accumulateDueBurst()
    {
        const VFXEmitterDef& def = *_pDef;
        for ( size_t burstIndex = 0; burstIndex < def._listBurst.size(); ++burstIndex )
        {
            const VFXBurstDef& burst = def._listBurst[burstIndex];
            if ( _listBurstFired[burstIndex] == 0 && static_cast<float64>( burst._time ) <= _emitterSeconds )
            {
                _listBurstFired[burstIndex] = 1;
                _spawnCarry += static_cast<float32>( burst._count ) * _spawnScale;
            }
        }
    }

    void VFXEmitterClock::accumulateEmission( float64 emitSeconds )
    {
        _spawnCarry += _pDef->_spawnRate * _spawnScale * static_cast<float32>( emitSeconds );
    }

    uint64 VFXEmitterClock::getLifetimeWindowSteps( const VFXEmitterDef& def, float32 stepSeconds )
    {
        SW_ASSERT( stepSeconds > 0.0f );
        // 올림에 한 단계를 더 둔다 — 실수 나눗셈이 경계에서 한 단계 모자라도 산 입자를 건너뛰지 않게.
        return static_cast<uint64>( MathUtil::ceil( def._lifetimeMax / stepSeconds ) ) + 1u;
    }

    uint64 VFXEmitterClock::skipExpiredSteps( uint64 doneStepCount, uint64 targetStepCount, float32 stepSeconds )
    {
        const uint64 window = getLifetimeWindowSteps( *_pDef, stepSeconds );
        if ( targetStepCount <= doneStepCount + window )
            return 0u;
        const uint64 skipCount = targetStepCount - window - doneStepCount;
        for ( uint64 stepIndex = 0; stepIndex < skipCount; ++stepIndex )
        {
            const VFXSpawnStep skipped = spawnStep( stepSeconds );
            static_cast<void>( skipped ); // 일정만 옮긴다 — 이 입자들은 목표 단계에 모두 죽었다
        }
        return skipCount;
    }

    void VFXEmitterClock::restart()
    {
        _listBurstFired.assign( _pDef->_listBurst.size(), 0 );
        _spawnedCount   = 0;
        _emitterSeconds = 0.0;
        _spawnCarry     = 0.0f;
        _pendingSeconds = 0.0;
        _bEmitting      = SW_TRUE;
        // 이미 낸 경고(단계 상한)는 다시 켜지 않는다 — 다시 시작해도 같은 데이터 문제다.
    }
} // namespace sw
