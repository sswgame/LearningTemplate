/**
 * @file VfxGPUEmitter.h
 * @brief GPU 로 시뮬레이션하는 방출기 하나의 게임 스레드 쪽(`VfxGPUEmitter`)입니다. 시간과 생성 일정만 들고 입자는 렌더 스레드의 컴퓨트가 가집니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Vfx/VfxEffectDef.h"
#include "Engine/Vfx/VfxEmitterClock.h"
#include "Engine/Vfx/VfxParticle.h"

namespace sw
{
    /**
     * @class VfxGPUEmitter
     * @brief 틱마다 고정 단계를 세어 누적 단계 수를 요청(`VfxGPUEmitterRequest`)으로 냅니다. 렌더 스레드(`VfxGPUSimulation`)가 그만큼 디스패치합니다.
     * @details 게임 스레드는 입자 수를 모른다(GPU 버퍼에 있다). 그래서 "끝났다" 는 일정에서 구한 상한이다 — 방출이 끝난 뒤 마지막으로 입자를 만든 단계부터
     *          가장 긴 수명(`_lifetimeMax`)과 한 단계를 더 지나면 모든 입자가 사라졌다(GPU 방출기에는 수명을 줄이는 모듈 — 충돌 — 이 없다).
     *          풀이 차서 버린 입자는 GPU 가 알리지 못한다(CPU 시뮬레이션은 경고한다).
     */
    class SW_API VfxGPUEmitter
    {
    public:
        /** @brief @p pDef 의 방출기를 @p seed 로 만듭니다. @p pDef 는 CPU 전용 모듈이 없는 방출기여야 합니다(`VfxEmitterDef::findCPUOnlyModuleName`). */
        VfxGPUEmitter( shared_ptr<const VfxEmitterDef> pDef, uint64 seed );

        /** @brief 프레임 시간 @p deltaSeconds 만큼 단계를 셉니다(`VfxEmitterClock::takeDueSteps`). */
        void update( float32 deltaSeconds );
        /** @brief 처음부터 다시 시작합니다. 새 재생 번호를 받아 렌더 스레드가 GPU 상태를 새로 만든다. */
        void restart();

        /** @brief 방출이 끝나고 모든 입자의 수명이 지났으면 true 입니다(일정에서 구한 상한 — 클래스 설명). */
        bool isFinished() const;
        /** @brief 지금까지 번호를 쓴 입자 수(풀이 차서 버린 입자 포함)입니다. */
        uint64 getSpawnedCount() const { return _clock.getSpawnedCount(); }
        /** @brief 이 방출기의 데이터입니다. */
        const VfxEmitterDef& getDef() const { return _clock.getDef(); }
        /** @brief 이번 프레임의 요청입니다(누적 단계 수와 재생 번호). */
        VfxGPUEmitterRequest makeRequest() const;

    private:
        VfxEmitterClock _clock;
        uint64          _seed;
        uint64          _runID;
        uint64          _stepCount;
        uint64          _lastSpawnStep; ///< 마지막으로 입자를 만든 단계의 번호(1 부터). 0 이면 아직 만든 적이 없다
    };
} // namespace sw
