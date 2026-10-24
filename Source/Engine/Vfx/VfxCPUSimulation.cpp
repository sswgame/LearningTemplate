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
    VfxCPUSimulation::VfxCPUSimulation( shared_ptr<const VfxEmitterDef> pDef, uint64 seed, float32 spawnScale )
        : _clock{ std::move( pDef ), spawnScale }
        , _seed{ seed }
        , _listParticle{}
        , _bWarnedPoolFull{ false }
    {
        SW_ASSERT( getDef()._maxParticles >= 1 );
        SW_ASSERT( getDef().computeSupportedTargets().has( VfxSimulationTarget::CPU ) ); // 깊이 충돌 같은 GPU 전용 모듈은 여기 오지 않는다
        _listParticle.reserve( getDef()._maxParticles );
    }

    void VfxCPUSimulation::restart()
    {
        _listParticle.clear();
        _clock.restart();
        // 이미 낸 경고(풀이 참)는 다시 켜지 않는다 — 다시 시작해도 같은 데이터 문제다.
    }

    void VfxCPUSimulation::update( float32 deltaSeconds, const IVfxCollisionScene* pCollision )
    {
        const uint32 stepCount = _clock.takeDueSteps( deltaSeconds );
        for ( uint32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            step( VfxEmitterClock::kFixedStepSeconds, pCollision );
        }
    }

    void VfxCPUSimulation::step( float32 stepSeconds, const IVfxCollisionScene* pCollision )
    {
        SW_ASSERT( stepSeconds > 0.0f );
        spawnParticles( _clock.spawnStep( stepSeconds ) );
        integrateStep( stepSeconds, pCollision );
    }

    void VfxCPUSimulation::spawnParticles( const VfxSpawnStep& spawn )
    {
        const VfxEmitterDef& def        = getDef();
        const uint32         freeCount  = def._maxParticles - getAliveCount();
        const uint32         spawnCount = MathUtil::min( spawn._count, freeCount );
        if ( spawnCount < spawn._count && _bWarnedPoolFull == false )
        {
            SW_LOG_WARNING_ID( "Vfx.PoolFull", "Vfx emitter '%#': pool of %# particles is full, dropping new particles", def._name, def._maxParticles );
            _bWarnedPoolFull = true;
        }

        // 풀이 차서 못 만든 입자(뒤쪽 번호)도 시계가 번호를 이미 썼다 — 풀 상태가 나머지 입자의 난수를 바꾸지 않게 한다.
        for ( uint32 spawnIndex = 0; spawnIndex < spawnCount; ++spawnIndex )
        {
            const uint64 particleIndex = spawn._firstParticleIndex + spawnIndex;
            uint64       state         = VfxCPUSimulationInternal::makeParticleState( _seed, particleIndex );

            Particle particle{};
            particle._particleIndex = particleIndex;
            particle._lifetime      = VfxCPUSimulationInternal::lerpFloat( def._lifetimeMin, def._lifetimeMax, VfxCPUSimulationInternal::nextUnitFloat( state ) );
            particle._velocity._x   = VfxCPUSimulationInternal::lerpFloat( def._velocityMin._x, def._velocityMax._x, VfxCPUSimulationInternal::nextUnitFloat( state ) );
            particle._velocity._y   = VfxCPUSimulationInternal::lerpFloat( def._velocityMin._y, def._velocityMax._y, VfxCPUSimulationInternal::nextUnitFloat( state ) );
            particle._velocity._z   = VfxCPUSimulationInternal::lerpFloat( def._velocityMin._z, def._velocityMax._z, VfxCPUSimulationInternal::nextUnitFloat( state ) );
            _listParticle.push_back( particle );
        }
    }

    void VfxCPUSimulation::integrateStep( float32 stepSeconds, const IVfxCollisionScene* pCollision )
    {
        const VfxEmitterDef&      def              = getDef();
        const IVfxCollisionScene* pActiveCollision = def._collision._mode != VfxCollisionMode::None ? pCollision : nullptr;
        size_t                    particleIndex    = 0;
        while ( particleIndex < _listParticle.size() )
        {
            Particle&    particle = _listParticle[particleIndex];
            const float3 from     = particle._position;
            particle._velocity += def._gravity * stepSeconds;
            particle._position += particle._velocity * stepSeconds;
            particle._age += stepSeconds;
            const bool bSurvivesCollision = pActiveCollision == nullptr || resolveCollision( particle, from, *pActiveCollision );

            if ( particle._age >= particle._lifetime || bSurvivesCollision == false )
            {
                // 마지막 입자와 바꿔 지운다. 바뀐 입자는 아직 적분 전이라 같은 자리를 다시 본다.
                particle = _listParticle.back();
                _listParticle.pop_back();
                continue;
            }
            ++particleIndex;
        }
    }

    bool VfxCPUSimulation::resolveCollision( Particle& particle, const float3& from, const IVfxCollisionScene& collision ) const
    {
        const VfxEmitterDef& def = getDef();
        VfxCollisionHit      hit{};
        if ( collision.castSegment( from, particle._position, def._collision._layerMask, hit ) == false )
            return true;
        if ( def._collision._mode == VfxCollisionMode::Kill )
            return false;
        // 튀기: 법선 성분은 뒤집어 반발 계수만큼, 접선 성분은 마찰만큼 줄인다. 위치는 닿은 점에서 살짝 띄운다(남은 이동은 버린다 — 한 단계에 한 번만 튄다).
        const float32 normalSpeed = particle._velocity.dot( hit._normal );
        const float3  normalPart  = hit._normal * normalSpeed;
        const float3  tangentPart = particle._velocity - normalPart;
        particle._velocity        = tangentPart * ( 1.0f - def._collision._friction ) - normalPart * def._collision._restitution;
        particle._position        = hit._point + hit._normal * VfxCollisionDef::kSurfaceSkin;
        return true;
    }

    void VfxCPUSimulation::writeParticles( vector<GPUParticle>& outListParticle ) const
    {
        const VfxEmitterDef& def = getDef();
        outListParticle.clear();
        outListParticle.reserve( _listParticle.size() );
        // 띠는 태어난 순서로 잇는다. 풀 안의 자리는 지우기(마지막과 바꾸기)마다 섞이므로 번호로 다시 줄 세운다.
        vector<uint32> listOrder( _listParticle.size() );
        for ( uint32 slot = 0; slot < static_cast<uint32>( _listParticle.size() ); ++slot )
        {
            listOrder[slot] = slot;
        }
        if ( def._renderMode == VfxRenderMode::Ribbon )
        {
            std::sort( listOrder.begin(), listOrder.end(),
                       [this]( uint32 slotA, uint32 slotB )
            { return _listParticle[slotA]._particleIndex < _listParticle[slotB]._particleIndex; } );
        }
        for ( const uint32 slot : listOrder )
        {
            const Particle& particle = _listParticle[slot];
            // 수명이 다한 입자는 `integrateStep` 이 지우므로 비율은 늘 0..1 이고, 데이터 검증이 커브 키를 보장한다.
            const float32 lifeRatio = particle._age / particle._lifetime;
            GPUParticle   output{};
            output._positionSize = float4( particle._position, *def._sizeOverLife.evaluate( lifeRatio ) );
            output._color        = float4( *def._colorOverLife._red.evaluate( lifeRatio ), *def._colorOverLife._green.evaluate( lifeRatio ),
                                           *def._colorOverLife._blue.evaluate( lifeRatio ), *def._colorOverLife._alpha.evaluate( lifeRatio ) );
            output._rotationLife = float4( 0.0f, lifeRatio, 0.0f, 0.0f );
            outListParticle.push_back( output );
        }
    }
} // namespace sw
