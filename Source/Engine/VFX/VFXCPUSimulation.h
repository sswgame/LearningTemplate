/**
 * @file VFXCPUSimulation.h
 * @brief 방출기 하나의 CPU 입자 시뮬레이션(`VFXCPUSimulation`)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/VFX/VFXEffectDef.h"
#include "Engine/VFX/VFXEmitterClock.h"
#include "Engine/VFX/VFXParticle.h"

namespace sw
{
    /** @brief 입자 충돌 질의가 처음 닿은 곳입니다(방출기 로컬). */
    struct VFXCollisionHit
    {
        float3 _point{};  ///< 닿은 점
        float3 _normal{}; ///< 닿은 면의 법선(길이 1, 쏜 쪽을 향한다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class IVFXCollisionScene
     * @brief 입자 시뮬레이션이 충돌을 묻는 창구입니다. 좌표는 **방출기 로컬**이고, 월드 변환과 물리 씬 질의는 구현이 한다(`VFXComponent`).
     * @details 시뮬레이션이 물리 · 씬을 모르게 하는 경계다(테스트는 평면 하나로 구현한다). 물리 씬이 없으면 부르는 쪽이 nullptr 를 넘긴다.
     */
    class IVFXCollisionScene
    {
    public:
        virtual ~IVFXCollisionScene() = default;
        /** @brief 선분 @p from → @p to 가 @p layerMask 의 바디에 처음 닿는 곳을 찾습니다. 닿으면 true 입니다. */
        [[nodiscard]] virtual bool castSegment( const float3& from, const float3& to, uint32 layerMask, VFXCollisionHit& outHit ) const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class VFXCPUSimulation
     * @brief 방출기 하나(`VFXEmitterDef`)를 CPU 에서 돌리고 렌더 입력(`GPUParticle`)을 냅니다.
     * @details 단계 하나는 생성 → 적분 → 수명 제거(마지막 입자와 바꾸기)입니다. 풀은 고정 크기이고(`_maxParticles`), 차면 새 입자를 만들지 않고
     *          방출기마다 한 번 경고합니다. 프레임 시간(`update`)은 고정 간격으로 나눠 단계를 여러 번 돕니다. 생성 일정(버스트 · 생성률 · 반복)은
     *          `VFXEmitterClock` 이 정한다 — GPU 시뮬레이션(vfxsimulate.hlsl)이 같은 시계로 같은 일정을 얻는다.
     *
     *          난수는 **방출기 씨앗과 입자 번호**로만 정해집니다(`HashUtil::mix64` · `splitMix64Next`). 풀 안의 자리나 프레임 시간과 무관해서,
     *          같은 씨앗 · 같은 단계 수면 입자가 같습니다(GPU 시뮬레이션이 같은 해시로 같은 초기값을 냅니다 — `VFXGPUSimulation` 이 해시 상수를 올린다).
     *          입자 번호는 만든 순서이고, 한 입자는 수명 → 속도 x → y → z 순으로 난수를 하나씩 씁니다.
     *          좌표는 방출기 로컬입니다. 회전은 지금 0 입니다(회전 모듈은 데이터 형식에 없다).
     *
     *          충돌(`VFXCollisionDef`)은 적분 뒤 그 단계에 움직인 선분을 `IVFXCollisionScene` 에 묻는다. 튀기는 법선 성분을 `_restitution` 만큼 뒤집고
     *          접선 성분을 `_friction` 만큼 줄이며, 닿은 점에서 법선 쪽으로 `VFXCollisionDef::kSurfaceSkin` 만큼 띄운다(GPU 깊이 충돌과 같은 값).
     *          CPU 는 물리 레이캐스트 방법만 돈다(`VFXCollisionDef::getSupportedTargets` — 깊이 방법의 방출기는 여기 오지 않는다). 띠(`VFXRenderMode::Ribbon`)는 태어난 순서로
     *          이어야 하므로 `writeParticles` 가 입자 번호 순으로 낸다(풀 안의 자리는 지우기마다 바뀐다).
     */
    class SW_API VFXCPUSimulation
    {
    public:
        /** @brief @p pDef 로 시뮬레이션을 만듭니다. @p pDef 는 `VFXEffectDef::validate` 를 통과한 방출기여야 합니다. @p seed 가 방출기의 씨앗입니다. */
        VFXCPUSimulation( shared_ptr<const VFXEmitterDef> pDef, uint64 seed, float32 spawnScale );

        /**
         * @brief 프레임 시간 @p deltaSeconds 만큼 나아갑니다. 고정 간격(`VFXEmitterClock::kFixedStepSeconds`)으로 나눠 `step` 을 부르고, 남는 시간은 다음 호출로 넘깁니다.
         * @param pCollision 충돌 질의(물리 씬이 없으면 nullptr). 방출기의 충돌이 꺼져 있으면 보지 않는다.
         */
        void update( float32 deltaSeconds, const IVFXCollisionScene* pCollision );
        /** @brief 단계 하나: 생성, 적분(충돌), 수명 제거. @p stepSeconds 는 0 보다 커야 합니다. */
        void step( float32 stepSeconds, const IVFXCollisionScene* pCollision );
        /** @brief 입자를 모두 지우고 방출기 시간을 0 으로 되돌려 처음부터 다시 방출합니다(같은 씨앗이라 같은 입자가 나옵니다). */
        void restart();

        /** @brief 살아 있는 입자 수입니다. */
        uint32 getAliveCount() const { return static_cast<uint32>( _listParticle.size() ); }
        /** @brief 방출이 끝나고(반복이 아닌 방출기) 입자가 모두 사라졌으면 true 입니다. */
        bool isFinished() const { return _clock.isEmitting() == false && _listParticle.empty(); }
        /** @brief 지금까지 번호를 쓴 입자 수(풀이 차서 만들지 못한 입자 포함)입니다. */
        uint64 getSpawnedCount() const { return _clock.getSpawnedCount(); }
        /** @brief 이 시뮬레이션이 쓰는 방출기 데이터입니다. */
        const VFXEmitterDef& getDef() const { return _clock.getDef(); }

        /**
         * @brief 살아 있는 입자의 렌더 입력을 @p outListParticle 에 채웁니다(앞의 내용은 지웁니다). 크기와 색은 수명 커브로 구합니다.
         * @details 순서는 띠(`VFXRenderMode::Ribbon`)면 태어난 순서(오래된 것 먼저)이고, 그 밖은 풀 안의 자리 순서다.
         */
        void writeParticles( vector<GPUParticle>& outListParticle ) const;

    private:
        /** @brief 살아 있는 입자 하나의 상태입니다. */
        struct Particle
        {
            float3  _position{};
            float3  _velocity{};
            float32 _age{ 0.0f };
            float32 _lifetime{ 1.0f };
            uint64  _particleIndex{ 0 }; ///< 만든 순서의 번호(난수 수열의 번호와 같다) — 띠가 이 순서로 잇는다
        };

        /** @brief 시계가 정한 이번 단계의 입자를 만듭니다. 풀이 차면 만들지 않고 한 번 경고합니다(번호는 시계가 이미 썼다). */
        void spawnParticles( const VFXSpawnStep& spawn );
        /** @brief 살아 있는 입자를 적분하고(충돌 포함) 수명이 다했거나 닿아 사라질 입자를 마지막 입자와 바꿔 지웁니다. */
        void integrateStep( float32 stepSeconds, const IVFXCollisionScene* pCollision );
        /** @brief 입자 하나가 이번 단계에 @p from 에서 움직인 선분의 충돌을 처리합니다. 사라져야 하면 false 입니다. */
        [[nodiscard]] bool resolveCollision( Particle& particle, const float3& from, const IVFXCollisionScene& collision ) const;

        VFXEmitterClock  _clock;
        uint64           _seed;
        vector<Particle> _listParticle;
        bool             _bWarnedPoolFull;
    };
} // namespace sw
