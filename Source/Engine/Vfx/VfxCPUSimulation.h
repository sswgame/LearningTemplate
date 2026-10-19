/**
 * @file VfxCPUSimulation.h
 * @brief 방출기 하나의 CPU 입자 시뮬레이션(`VfxCPUSimulation`)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Vfx/VfxEffectDef.h"
#include "Engine/Vfx/VfxParticle.h"

namespace sw
{
    /**
     * @class VfxCPUSimulation
     * @brief 방출기 하나(`VfxEmitterDef`)를 CPU 에서 돌리고 렌더 입력(`GPUParticle`)을 냅니다.
     * @details 단계 하나는 생성 → 적분 → 수명 제거(마지막 입자와 바꾸기)입니다. 풀은 고정 크기이고(`_maxParticles`), 차면 새 입자를 만들지 않고
     *          방출기마다 한 번 경고합니다. 프레임 시간(`update`)은 고정 간격으로 나눠 단계를 여러 번 돕니다.
     *
     *          난수는 **방출기 씨앗과 입자 번호**로만 정해집니다(`HashUtil::mix64` · `splitMix64Next`). 풀 안의 자리나 프레임 시간과 무관해서,
     *          같은 씨앗 · 같은 단계 수면 입자가 같습니다(GPU 시뮬레이션이 같은 해시로 같은 초기값을 냅니다). 입자 번호는 만든 순서이고,
     *          한 입자는 수명 → 속도 x → y → z 순으로 난수를 하나씩 씁니다.
     *          좌표는 방출기 로컬입니다. 회전은 지금 0 입니다(회전 모듈은 데이터 형식에 없다).
     */
    class SW_API VfxCPUSimulation
    {
    public:
        /** @brief 단계 하나의 시간입니다. `update` 가 프레임 시간을 이 간격으로 나눕니다. */
        static constexpr float32 kFixedStepSeconds = 1.0f / 60.0f;
        /** @brief `update` 한 번이 돌 수 있는 단계의 상한입니다. 넘는 몫은 버립니다(긴 프레임 뒤 폭주 방지). */
        static constexpr uint32 kMaxStepsPerUpdate = 8;

        /** @brief @p def 의 복사본으로 시뮬레이션을 만듭니다. @p def 는 `VfxEffectDef::validate` 를 통과한 방출기여야 합니다. @p seed 가 방출기의 씨앗입니다. */
        VfxCPUSimulation( const VfxEmitterDef& def, uint64 seed );

        /** @brief 프레임 시간 @p deltaSeconds 만큼 나아갑니다. 고정 간격으로 나눠 `step` 을 부르고, 남는 시간은 다음 호출로 넘깁니다. */
        void update( float32 deltaSeconds );
        /** @brief 단계 하나: 생성, 적분, 수명 제거. @p stepSeconds 는 0 보다 커야 합니다. */
        void step( float32 stepSeconds );
        /** @brief 입자를 모두 지우고 방출기 시간을 0 으로 되돌려 처음부터 다시 방출합니다(같은 씨앗이라 같은 입자가 나옵니다). */
        void restart();

        /** @brief 살아 있는 입자 수입니다. */
        uint32 getAliveCount() const { return static_cast<uint32>( _listParticle.size() ); }
        /** @brief 방출이 끝나고(반복이 아닌 방출기) 입자가 모두 사라졌으면 true 입니다. */
        bool isFinished() const { return _bEmitting == false && _listParticle.empty(); }
        /** @brief 이 시뮬레이션이 쓰는 방출기 데이터입니다. */
        const VfxEmitterDef& getDef() const { return _def; }

        /** @brief 살아 있는 입자의 렌더 입력을 @p outListParticle 에 채웁니다(앞의 내용은 지웁니다). 크기와 색은 수명 커브로 구합니다. */
        void writeParticles( vector<GPUParticle>& outListParticle ) const;

    private:
        /** @brief 살아 있는 입자 하나의 상태입니다. */
        struct Particle
        {
            float3  _position{};
            float3  _velocity{};
            float32 _age{ 0.0f };
            float32 _lifetime{ 1.0f };
        };

        /** @brief 이번 단계에 낼 입자 수를 구해 만듭니다(버스트 → 생성률). 방출 시간이 끝나면 반복하거나 방출을 멈춥니다. */
        void spawnStep( float32 stepSeconds );
        /** @brief 입자 @p count 개를 만듭니다. 풀이 차면 만들지 않고 한 번 경고합니다. */
        void spawnParticles( uint32 count );
        /** @brief 살아 있는 입자를 적분하고 수명이 다한 입자를 마지막 입자와 바꿔 지웁니다. */
        void integrateStep( float32 stepSeconds );

        VfxEmitterDef    _def;
        uint64           _seed;
        vector<Particle> _listParticle;
        vector<uint8>    _listBurstFired;
        uint64           _spawnedCount;
        float32          _emitterSeconds;
        float32          _spawnCarry;
        float32          _pendingSeconds;
        bool             _bEmitting;
        bool             _bWarnedPoolFull;
        bool             _bWarnedStepLimit;
    };
} // namespace sw
