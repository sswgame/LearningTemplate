/**
 * @file VFXEmitterClock.h
 * @brief 방출기 하나의 시간과 생성 일정(`VFXEmitterClock`)입니다. CPU 시뮬레이션과 GPU 시뮬레이션이 같은 일정으로 입자를 만듭니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/VFX/VFXEffectDef.h"

namespace sw
{
    /** @brief 단계 하나에 만들 입자입니다. 풀이 차서 만들지 못한 입자도 번호는 쓴 것으로 센다(`VFXCPUSimulation` · vfx_simulate.hlsl 가 같은 규칙). */
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
     *          생성률은 방출 시간(0.._duration) 안에서만 쌓고, 반복 방출기는 시간을 되감아 버스트를 다시 켠다. 끝을 넘은 단계의 남는 몫은 다음 바퀴의 시간이다 —
     *          그 몫에 든 버스트(0 초 버스트 포함)와 생성률을 같은 단계에 센다(예전에는 몫을 버려 바퀴마다 생성이 모자라고 0 초 버스트가 한 단계 늦었다).
     */
    class SW_API VFXEmitterClock
    {
    public:
        /** @brief 단계 하나의 시간입니다. `takeDueSteps` 가 프레임 시간을 이 간격으로 나눕니다. */
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
        /**
         * @brief 입자 수명 창(단계 수)입니다 — 지금부터 이만큼 앞선 단계에 태어난 입자는 모두 죽었다(모듈은 수명을 `_lifetimeMax` 보다 늘리지 않는다).
         */
        [[nodiscard]] static uint64 getLifetimeWindowSteps( const VFXEmitterDef& def, float32 stepSeconds );
        /**
         * @brief @p doneStepCount 에서 @p targetStepCount 까지 밀린 단계가 수명 창보다 길면, 창 밖 단계를 입자 없이 지나가고 지나간 단계 수를 돌려줍니다.
         * @details 지나간 단계에 태어난 입자는 목표 단계에 모두 죽었으므로 돌릴 까닭이 없다 — 일정(입자 번호 · 버스트 · 반복)만 `spawnStep` 을 부른 것과 같게 옮긴다.
         *          GPU 방출기가 오래 밀린 재생을 다시 실을 때 처음부터 빨리감지 않게 한다(나이아가라의 warm-up 상한과 같은 자리).
         */
        [[nodiscard]] uint64 skipExpiredSteps( uint64 doneStepCount, uint64 targetStepCount, float32 stepSeconds );
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
        /** @brief 이번 바퀴의 시간(`_emitterSeconds`)까지 온 버스트 가운데 아직 켜지 않은 것을 생성 몫에 더합니다(바퀴마다 한 번). */
        void accumulateDueBurst();
        /** @brief 방출 시간 @p emitSeconds 동안의 생성률을 생성 몫에 더합니다. */
        void accumulateEmission( float64 emitSeconds );

    private:
        shared_ptr<const VFXEmitterDef> _pDef;
        vector<uint8>                   _listBurstFired;
        uint64                          _spawnedCount;
        float64                         _emitterSeconds; ///< 이번 바퀴의 방출기 시간 — 배정밀도라 단계를 오래 더해도 반복 경계 · 버스트 시각이 단계 하나씩 흔들리지 않는다
        float32                         _spawnCarry;
        float32                         _spawnScale;     ///< 생성률 · 버스트 수에 곱한다(이펙트 품질)
        float64                         _pendingSeconds; ///< 아직 단계가 되지 않은 프레임 시간(배정밀도 — 남는 몫이 쌓여도 단계 수가 어긋나지 않는다)
        uint8                           _bEmitting        : 1;
        uint8                           _bWarnedStepLimit : 1;
    };
} // namespace sw
