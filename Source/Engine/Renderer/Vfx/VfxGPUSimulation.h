/**
 * @file VfxGPUSimulation.h
 * @brief GPU 방출기의 입자 시뮬레이션(렌더 스레드, vfxsimulate.hlsl)입니다. `VfxParticleRenderer` 가 소유합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Vfx/VfxEmitterClock.h"

namespace sw
{
    struct GPUParticleBatch;

    class IRHICommandList;
    class IRHIDevice;

    /**
     * @class VfxGPUSimulation
     * @brief 재생 번호(`VfxGPUEmitterRequest::_runID`)마다 GPU 상태(입자 상태 두 반쪽, 렌더 입력, 간접 인자)를 들고, 프레임마다 밀린 단계를 디스패치합니다.
     * @details 기록 전(`prepareFrame`)에 상태를 만들고 매개변수(방출기 상수 · 단계마다의 일정 · 커브 키 · 해시 상수)를 한 버퍼로 올린다. 일정은 게임 스레드와 같은
     *          시계(`VfxEmitterClock`)를 여기서 다시 돌려 얻는다 — 요청이 누적 단계 수라 패킷을 건너뛴 프레임도 다음 프레임에 이어 돈다(디스패치마다 상한
     *          SW_VFX_MAX_STEPS_PER_DISPATCH, 넘친 단계는 다음 프레임). 컴퓨트 프리패스(`recordDispatches`)는 방출기마다 그룹 하나를 디스패치하고,
     *          투명 패스는 렌더 입력을 t11 에 걸고 간접 인자로 그린다(`VfxParticleRenderer::drawBatches`).
     *          이번 프레임 배치에 없는 재생 번호의 상태는 놓는다(멈췄거나 다시 재생했거나 컴포넌트가 사라졌다). 다시 나타나면 처음부터 다시 돌아 같은 결과에 닿는다.
     */
    class SW_API VfxGPUSimulation
    {
    public:
        /** @brief GPU 방출기 하나를 그릴 자원입니다. */
        struct DrawSource
        {
            RHIDescriptorIndex _particleSrv{ kInvalidDescriptorIndex }; ///< 렌더 입력(GPUParticle) 구조 버퍼 — t11
            RHIBufferHandle    _argsBuffer{ 0 };                        ///< 간접 인자(RHIDrawIndirectCommand 가 맨 앞)
        };

        VfxGPUSimulation();
        ~VfxGPUSimulation();

        VfxGPUSimulation( const VfxGPUSimulation& )            = delete;
        VfxGPUSimulation& operator=( const VfxGPUSimulation& ) = delete;

        /**
         * @brief 이번 프레임의 GPU 배치마다 상태를 갖추고 매개변수를 올립니다. **기록 시작 전에** 부릅니다.
         * @param bPipelineReady 컴퓨트 PSO 가 있으면 true. 없으면 한 번 오류를 남기고 GPU 방출기를 그리지 않는다(CPU 로 바꿔 그리지 않는다 — 대상은 데이터가 정했다)
         */
        void prepareFrame( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch, bool bPipelineReady );
        /** @brief 컴퓨트 프리패스에 이번 프레임의 디스패치를 기록합니다(`prepareFrame` 뒤, 투명 패스보다 먼저 큐에 내는 리스트). */
        void recordDispatches( IRHICommandList& cmd, RHIPipelineStateHandle pso ) const;
        /** @brief 배치 @p batchIndex(`prepareFrame` 에 넘긴 목록의 번호)를 그릴 자원입니다. GPU 배치가 아니거나 준비하지 못했으면 nullptr 입니다. */
        const DrawSource* findDrawSource( uint32 batchIndex ) const;
        /** @brief 모든 상태를 놓습니다(디바이스가 없으면 핸들만 잊습니다). */
        void release( IRHIDevice* pDevice );

        /** @brief 지금 든 GPU 방출기 상태의 수입니다(테스트와 진단용). */
        uint32 getEmitterStateCount() const { return static_cast<uint32>( _mapState.size() ); }

    private:
        /** @brief 재생 한 번의 GPU 상태입니다. */
        struct EmitterState
        {
            explicit EmitterState( shared_ptr<const VfxEmitterDef> pDef );

            VfxEmitterClock         _clock;          ///< 게임 스레드와 같은 일정(여기서 다시 돈다)
            uint64                  _doneStepCount;  ///< 디스패치에 실은 단계 수
            RHIStructuredBufferSlot _stateBuffer;    ///< 입자 상태(풀 크기 × 2)
            RHIStructuredBufferSlot _particleBuffer; ///< 렌더 입력(풀 크기)
            RHIStructuredBufferSlot _argsBuffer;     ///< 간접 인자와 살아 있는 수(SW_VFX_ARGS_WORD_COUNT 단어)
            DrawSource              _drawSource;
            bool                    _bNeedsReset; ///< 아직 한 번도 디스패치하지 않았다
            bool                    _bSeen;       ///< 이번 프레임 배치에 있었다
        };

        /** @brief 이번 프레임의 디스패치 하나입니다. */
        struct Dispatch
        {
            const EmitterState* _pState{ nullptr }; ///< 이번 프레임 안에서만 빌린다(`_mapState` 가 소유)
            uint32              _paramRow{ 0 };
        };

        /** @brief @p state 의 이번 프레임 매개변수 구간을 `_listParamWord` 끝에 붙이고 디스패치할 단계 수를 돌려줍니다. */
        uint32 appendParams( EmitterState& state, const GPUParticleBatch& batch, uint64 seed, uint64 targetStepCount );

        unordered_map<uint64, unique_ptr<EmitterState>> _mapState;
        vector<Dispatch>                                _listDispatch;
        vector<const DrawSource*>                       _listBatchDrawSource; ///< 배치마다(GPU 배치가 아니면 nullptr) — 이번 프레임 안에서만 쓴다
        vector<uint32>                                  _listParamWord;       ///< 매개변수 행(uint4)을 단어로 이어 붙인 사본
        vector<uint64>                                  _listScratchStaleRunID;
        RHIStructuredBufferSlot                         _paramBuffer;
        bool                                            _bPipelineMissingLogged;
    };
} // namespace sw
