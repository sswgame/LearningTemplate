#include "pch.h"

#include "Engine/Vfx/VfxCPUSimulation.h"

#include "Core/Common/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    SW_LOG_CALLER( "VfxCPUSimulation" );

    namespace
    {
        /** @brief 난수 한 줄(입자 하나가 쓰는 splitmix64 수열)입니다. GPU 시뮬레이션이 같은 수열을 만듭니다. */
        struct VfxCPUSimulationInternal
        {
            /** @brief 정수 난수에서 float 에 정확히 담기는 비트 수입니다. */
            static constexpr uint32 kFloatRandomBits = 24;

            /** @brief 방출기 씨앗과 입자 번호로 정해지는 수열의 시작 상태입니다. 풀 안의 자리와 무관합니다. */
            [[nodiscard]] static uint64 makeParticleState( uint64 emitterSeed, uint64 particleIndex )
            {
                return HashUtil::combine( HashUtil::mix64( emitterSeed ), particleIndex );
            }

            /** @brief 수열의 다음 값을 [0, 1) 의 float 으로 돌려줍니다. */
            [[nodiscard]] static float32 nextUnitFloat( uint64& inoutState )
            {
                const uint64 bits = HashUtil::splitMix64Next( inoutState ) >> ( 64 - kFloatRandomBits );
                return static_cast<float32>( bits ) / static_cast<float32>( 1u << kFloatRandomBits );
            }

            [[nodiscard]] static float32 lerpFloat( float32 from, float32 to, float32 ratio ) { return from + ( to - from ) * ratio; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    VfxCPUSimulation::VfxCPUSimulation( const VfxEmitterDef& def, uint64 seed )
        : _def{ def }
        , _seed{ seed }
        , _listParticle{}
        , _listBurstFired{}
        , _spawnedCount{ 0 }
        , _emitterSeconds{ 0.0f }
        , _spawnCarry{ 0.0f }
        , _pendingSeconds{ 0.0f }
        , _bEmitting{ true }
        , _bWarnedPoolFull{ false }
        , _bWarnedStepLimit{ false }
    {
        SW_ASSERT( _def._maxParticles >= 1 );
        _listParticle.reserve( _def._maxParticles );
        _listBurstFired.resize( _def._listBurst.size(), 0 );
    }

    void VfxCPUSimulation::restart()
    {
        _listParticle.clear();
        _listBurstFired.assign( _def._listBurst.size(), 0 );
        _spawnedCount   = 0;
        _emitterSeconds = 0.0f;
        _spawnCarry     = 0.0f;
        _pendingSeconds = 0.0f;
        _bEmitting      = true;
        // 이미 낸 경고(풀이 참 · 단계 상한)는 다시 켜지 않는다 — 다시 시작해도 같은 데이터 문제다.
    }

    void VfxCPUSimulation::update( float32 deltaSeconds )
    {
        SW_ASSERT( deltaSeconds >= 0.0f );
        if ( deltaSeconds <= 0.0f )
            return;

        _pendingSeconds += deltaSeconds;
        const uint32 dueStepCount = static_cast<uint32>( _pendingSeconds / kFixedStepSeconds );
        _pendingSeconds -= static_cast<float32>( dueStepCount ) * kFixedStepSeconds;

        uint32 stepCount = dueStepCount;
        if ( dueStepCount > kMaxStepsPerUpdate )
        {
            stepCount = kMaxStepsPerUpdate;
            if ( _bWarnedStepLimit == false )
            {
                SW_LOG_WARNING( "Vfx emitter '%#': frame time %# s needs %# steps, running %# and dropping the rest", _def._name, deltaSeconds, dueStepCount, kMaxStepsPerUpdate );
                _bWarnedStepLimit = true;
            }
        }
        for ( uint32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            step( kFixedStepSeconds );
        }
    }

    void VfxCPUSimulation::step( float32 stepSeconds )
    {
        SW_ASSERT( stepSeconds > 0.0f );
        spawnStep( stepSeconds );
        integrateStep( stepSeconds );
    }

    void VfxCPUSimulation::spawnStep( float32 stepSeconds )
    {
        if ( _bEmitting == false )
            return;

        const float32 previousSeconds = _emitterSeconds;
        _emitterSeconds += stepSeconds;

        uint32 spawnCount = 0;
        for ( size_t burstIndex = 0; burstIndex < _def._listBurst.size(); ++burstIndex )
        {
            const VfxBurstDef& burst = _def._listBurst[burstIndex];
            if ( _listBurstFired[burstIndex] == 0 && burst._time <= _emitterSeconds )
            {
                _listBurstFired[burstIndex] = 1;
                spawnCount += burst._count;
            }
        }

        // 생성률은 방출 시간(0.._duration) 안에서만 쌓는다.
        const float32 emitSeconds = MathUtil::min( _emitterSeconds, _def._duration ) - MathUtil::min( previousSeconds, _def._duration );
        _spawnCarry += _def._spawnRate * emitSeconds;
        const float32 wholeCount = MathUtil::floor( _spawnCarry );
        _spawnCarry -= wholeCount;
        spawnCount += static_cast<uint32>( wholeCount );

        spawnParticles( spawnCount );

        if ( _emitterSeconds >= _def._duration )
        {
            if ( _def._bLooping )
            {
                _emitterSeconds -= _def._duration;
                _listBurstFired.assign( _def._listBurst.size(), 0 );
            }
            else
            {
                _bEmitting = false;
            }
        }
    }

    void VfxCPUSimulation::spawnParticles( uint32 count )
    {
        const uint32 freeCount  = _def._maxParticles - getAliveCount();
        const uint32 spawnCount = MathUtil::min( count, freeCount );
        if ( spawnCount < count && _bWarnedPoolFull == false )
        {
            SW_LOG_WARNING( "Vfx emitter '%#': pool of %# particles is full, dropping new particles", _def._name, _def._maxParticles );
            _bWarnedPoolFull = true;
        }

        for ( uint32 spawnIndex = 0; spawnIndex < spawnCount; ++spawnIndex )
        {
            uint64 state = VfxCPUSimulationInternal::makeParticleState( _seed, _spawnedCount );
            ++_spawnedCount;

            Particle particle{};
            particle._lifetime    = VfxCPUSimulationInternal::lerpFloat( _def._lifetimeMin, _def._lifetimeMax, VfxCPUSimulationInternal::nextUnitFloat( state ) );
            particle._velocity._x = VfxCPUSimulationInternal::lerpFloat( _def._velocityMin._x, _def._velocityMax._x, VfxCPUSimulationInternal::nextUnitFloat( state ) );
            particle._velocity._y = VfxCPUSimulationInternal::lerpFloat( _def._velocityMin._y, _def._velocityMax._y, VfxCPUSimulationInternal::nextUnitFloat( state ) );
            particle._velocity._z = VfxCPUSimulationInternal::lerpFloat( _def._velocityMin._z, _def._velocityMax._z, VfxCPUSimulationInternal::nextUnitFloat( state ) );
            _listParticle.push_back( particle );
        }
        // 풀이 차서 못 만든 입자도 번호는 쓴 것으로 센다 — 풀 상태가 나머지 입자의 난수를 바꾸지 않게 한다.
        _spawnedCount += count - spawnCount;
    }

    void VfxCPUSimulation::integrateStep( float32 stepSeconds )
    {
        size_t particleIndex = 0;
        while ( particleIndex < _listParticle.size() )
        {
            Particle& particle = _listParticle[particleIndex];
            particle._velocity += _def._gravity * stepSeconds;
            particle._position += particle._velocity * stepSeconds;
            particle._age += stepSeconds;

            if ( particle._age >= particle._lifetime )
            {
                // 마지막 입자와 바꿔 지운다. 바뀐 입자는 아직 적분 전이라 같은 자리를 다시 본다.
                particle = _listParticle.back();
                _listParticle.pop_back();
                continue;
            }
            ++particleIndex;
        }
    }

    void VfxCPUSimulation::writeParticles( vector<GPUParticle>& outListParticle ) const
    {
        outListParticle.clear();
        outListParticle.reserve( _listParticle.size() );
        for ( const Particle& particle : _listParticle )
        {
            // 수명이 다한 입자는 `integrateStep` 이 지우므로 비율은 늘 0..1 이고, 데이터 검증이 커브 키를 보장한다.
            const float32 lifeRatio = particle._age / particle._lifetime;
            GPUParticle   output{};
            output._positionSize = float4( particle._position, *_def._sizeOverLife.evaluate( lifeRatio ) );
            output._color        = float4( *_def._colorOverLife._red.evaluate( lifeRatio ), *_def._colorOverLife._green.evaluate( lifeRatio ),
                                           *_def._colorOverLife._blue.evaluate( lifeRatio ), *_def._colorOverLife._alpha.evaluate( lifeRatio ) );
            output._rotationLife = float4( 0.0f, lifeRatio, 0.0f, 0.0f );
            outListParticle.push_back( output );
        }
    }
} // namespace sw
