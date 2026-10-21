#include "pch.h"

#include "Engine/Vfx/VfxEmitterClock.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    SW_LOG_CALLER( "VfxEmitterClock" );

    VfxEmitterClock::VfxEmitterClock( shared_ptr<const VfxEmitterDef> pDef, float32 spawnScale )
        : _pDef{ std::move( pDef ) }
        , _listBurstFired{}
        , _spawnedCount{ 0 }
        , _emitterSeconds{ 0.0f }
        , _spawnCarry{ 0.0f }
        , _spawnScale{ spawnScale }
        , _pendingSeconds{ 0.0f }
        , _bEmitting{ true }
        , _bWarnedStepLimit{ false }
    {
        SW_ASSERT( _pDef != nullptr );
        SW_ASSERT( _spawnScale >= 0.0f );
        _listBurstFired.resize( _pDef->_listBurst.size(), 0 );
    }

    uint32 VfxEmitterClock::takeDueSteps( float32 deltaSeconds )
    {
        SW_ASSERT( deltaSeconds >= 0.0f );
        if ( deltaSeconds <= 0.0f )
            return 0;

        _pendingSeconds += deltaSeconds;
        const uint32 dueStepCount = static_cast<uint32>( _pendingSeconds / kFixedStepSeconds );
        _pendingSeconds -= static_cast<float32>( dueStepCount ) * kFixedStepSeconds;
        if ( dueStepCount <= kMaxStepsPerUpdate )
            return dueStepCount;
        if ( _bWarnedStepLimit == false )
        {
            SW_LOG_WARNING( "Vfx emitter '%#': frame time %# s needs %# steps, running %# and dropping the rest", _pDef->_name, deltaSeconds, dueStepCount, kMaxStepsPerUpdate );
            _bWarnedStepLimit = true;
        }
        return kMaxStepsPerUpdate;
    }

    VfxSpawnStep VfxEmitterClock::spawnStep( float32 stepSeconds )
    {
        SW_ASSERT( stepSeconds > 0.0f );
        VfxSpawnStep spawn{};
        spawn._firstParticleIndex = _spawnedCount;
        if ( _bEmitting == false )
            return spawn;

        const VfxEmitterDef& def             = *_pDef;
        const float32        previousSeconds = _emitterSeconds;
        _emitterSeconds += stepSeconds;

        // 버스트와 생성률은 생성 배율(이펙트 품질)을 곱해 같은 나머지에 쌓는다 — 배율이 1 이 아니어도 작은 버스트가 반올림으로 사라지지 않고 다음 몫에 이어진다.
        for ( size_t burstIndex = 0; burstIndex < def._listBurst.size(); ++burstIndex )
        {
            const VfxBurstDef& burst = def._listBurst[burstIndex];
            if ( _listBurstFired[burstIndex] == 0 && burst._time <= _emitterSeconds )
            {
                _listBurstFired[burstIndex] = 1;
                _spawnCarry += static_cast<float32>( burst._count ) * _spawnScale;
            }
        }

        // 생성률은 방출 시간(0.._duration) 안에서만 쌓는다.
        const float32 emitSeconds = MathUtil::min( _emitterSeconds, def._duration ) - MathUtil::min( previousSeconds, def._duration );
        _spawnCarry += def._spawnRate * _spawnScale * emitSeconds;
        const float32 wholeCount = MathUtil::floor( _spawnCarry );
        _spawnCarry -= wholeCount;
        spawn._count += static_cast<uint32>( wholeCount );
        // 만들지 못한 입자도 번호를 쓴다 — 풀 상태가 나머지 입자의 난수를 바꾸지 않게 한다.
        _spawnedCount += spawn._count;

        if ( _emitterSeconds >= def._duration )
        {
            if ( def._bLooping )
            {
                _emitterSeconds -= def._duration;
                _listBurstFired.assign( def._listBurst.size(), 0 );
            }
            else
            {
                _bEmitting = false;
            }
        }
        return spawn;
    }

    void VfxEmitterClock::restart()
    {
        _listBurstFired.assign( _pDef->_listBurst.size(), 0 );
        _spawnedCount   = 0;
        _emitterSeconds = 0.0f;
        _spawnCarry     = 0.0f;
        _pendingSeconds = 0.0f;
        _bEmitting      = true;
        // 이미 낸 경고(단계 상한)는 다시 켜지 않는다 — 다시 시작해도 같은 데이터 문제다.
    }
} // namespace sw
