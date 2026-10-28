/**
 * @file VFXEmitterClock.h
 * @brief 방출기 하나의 시간과 생성 일정(`VFXEmitterClock`)입니다. CPU 시뮬레이션과 GPU 시뮬레이션이 같은 일정으로 입자를 만듭니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/VFX/VFXEffectDef.h"

namespace sw
{
    /** @brief 단계 하나에 만들 입자입니다. 풀이 차서 만들지 못한 입자도 번호는 쓴 것으로 센다(`VFXCPUSimulation` · vfxsimulate.hlsl 가 같은 규칙). */
    struct VFXSpawnStep
    {
        uint64 _firstParticleIndex{ 0 }; ///< 이 단계의 첫 입자 번호(난수 수열의 번호 — 만든 순서)
        uint32 _count{ 0 };              ///< 이 단계에 만들려는 입자 수(풀 한도 전)
    };
} // namespace sw

namespace sw
{
    /**
     * @class VFXEmitterClock
     * @brief 프레임 시간을 고정 단계로 나누고, 단계마다 버스트와 생성률로 만들 입자 수와 첫 번호를 냅니다.
     * @details 일정은 방출기 데이터와 단계 수로만 정해진다(입자 상태와 무관하다). 그래서 GPU 방출기는 게임 스레드(끝났는지)와 렌더 스레드(디스패치마다의 일정)가
     *          같은 시계를 따로 돌려도 같은 일정을 얻는다 — 렌더 스레드가 프레임을 건너뛰어도 단계 수만 맞추면 된다(`VFXGPUSimulation`).
     *          생성률은 방출 시간(0.._duration) 안에서만 쌓고, 반복 방출기는 시간을 되감아 버스트를 다시 켠다.
     */
    class SW_API VFXEmitterClock
    {
    public:
        /** @brief 단계 하나의 시간입니다. `takeDueSteps` 가 프레임 시간을 이 간격으로 나눕니다. */
        static constexpr float32 kFixedStepSeconds = 1.0f / 60.0f;
        /** @brief `takeDueSteps` 한 번이 돌려줄 수 있는 단계의 상한입니다. 넘는 몫은 버립니다(긴 프레임 뒤 폭주 방지). */
        static constexpr uint32 kMaxStepsPerUpdate = 8;

        /**
         * @brief @p pDef 의 일정으로 시계를 만듭니다. @p pDef 는 `VFXEffectDef::validate` 를 통과한 방출기여야 합니다.
         * @param spawnScale 생성률과 버스트 수에 곱하는 배율(0 이상)입니다. 이펙트 품질 사용자 설정(`gv_effectsSpawnScale`)을 `VFXComponent` 가 넘기고,
         *        GPU 방출기는 요청(`VFXGPUEmitterRequest::_spawnScale`)에 실어 렌더 스레드의 시계가 같은 일정을 얻게 한다.
         */
        VFXEmitterClock( shared_ptr<const VFXEmitterDef> pDef, float32 spawnScale );

        /**
         * @brief 프레임 시간 @p deltaSeconds 를 쌓고 이번에 돌 단계 수를 돌려줍니다. 남는 시간은 다음 호출로 넘깁니다.
         * @details 상한(`kMaxStepsPerUpdate`)을 넘는 단계는 버리고 방출기마다 한 번 경고합니다.
         */
        [[nodiscard]] uint32 takeDueSteps( float32 deltaSeconds );
        /** @brief 단계 하나(@p stepSeconds)만큼 방출기 시간을 옮기고 그 단계에 만들 입자를 돌려줍니다. 방출이 끝났으면 0 개입니다. */
        [[nodiscard]] VFXSpawnStep spawnStep( float32 stepSeconds );
        /** @brief 방출기 시간을 0 으로 되돌립니다(같은 데이터면 같은 일정이 다시 나온다). */
        void restart();

        /** @brief 아직 방출 중이면 true 입니다(반복 방출기는 늘 true). */
        bool isEmitting() const { return _bEmitting; }
        /** @brief 지금까지 번호를 쓴 입자 수(만들지 못한 입자 포함)입니다. */
        uint64 getSpawnedCount() const { return _spawnedCount; }
        /** @brief 이 시계의 방출기 데이터입니다. */
        const VFXEmitterDef& getDef() const { return *_pDef; }
        /** @brief 방출기 데이터의 공유 소유입니다(렌더 스레드가 GPU 일정을 다시 만들 때 싣는다). */
        const shared_ptr<const VFXEmitterDef>& getSharedDef() const { return _pDef; }
        /** @brief 생성 배율입니다(생성자 인자). */
        float32 getSpawnScale() const { return _spawnScale; }

    private:
        shared_ptr<const VFXEmitterDef> _pDef;
        vector<uint8>                   _listBurstFired;
        uint64                          _spawnedCount;
        float32                         _emitterSeconds;
        float32                         _spawnCarry;
        float32                         _spawnScale; ///< 생성률 · 버스트 수에 곱한다(이펙트 품질)
        float32                         _pendingSeconds;
        bool                            _bEmitting;
        bool                            _bWarnedStepLimit;
    };
} // namespace sw
