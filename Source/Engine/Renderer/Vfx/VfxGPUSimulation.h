/**
 * @file VfxGPUSimulation.h
 * @brief GPU 방출기의 입자 시뮬레이션(렌더 스레드, vfxsimulate.hlsl · vfxmeshinstance.hlsl)입니다. `VfxParticleRenderer` 가 소유합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Vfx/VfxEmitterClock.h"
#include "Engine/Vfx/VfxParticle.h"

namespace sw
{
    struct GPUParticleBatch;
    struct GPUVfxMeshEmitter;

    class IRHICommandList;
    class IRHIDevice;

    /**
     * @struct VfxDepthHistoryInput
     * @brief 깊이 충돌이 읽는 주 시점의 깊이 히스토리입니다(`FrameRenderer` 의 뷰마다의 히스토리 중 주 시점 것). `_texture` 가 0 이면 이번 프레임에 없다.
     */
    struct VfxDepthHistoryInput
    {
        RHITextureHandle   _texture{ 0 };                   ///< R32 실수 RT · RW 텍스처
        RHIDescriptorIndex _uav{ kInvalidDescriptorIndex }; ///< RW 텍스처 번호(네이티브는 셰이더가 이 번호로, 에뮬은 슬롯 0 에 건다)
        uint32             _width{ 0 };
        uint32             _height{ 0 };
        float4x4           _viewProj{}; ///< 히스토리를 그린 프레임의 뷰-투영
        float3             _eye{};      ///< 그 프레임의 눈 위치
    };
} // namespace sw

namespace sw
{
    /** @brief `VfxGPUSimulation::prepareFrame` 이 이번 프레임에 받는 렌더러 쪽 입력입니다. */
    struct VfxSimulationFrameInput
    {
        VfxDepthHistoryInput _depth{};
        RHIBufferHandle      _instanceBuffer{ 0 }; ///< GPUScene 인스턴스 버퍼(메시 입자가 쓴다)
        RHIDescriptorIndex   _instanceUav{ kInvalidDescriptorIndex };
        bool                 _bSimulationPipelineReady{ false }; ///< vfxsimulate.hlsl PSO 가 있다
        bool                 _bMeshPipelineReady{ false };       ///< vfxmeshinstance.hlsl PSO 가 있다
        bool                 _bNativeBindless{ false };          ///< DX12 · Vulkan 이면 true(RW 텍스처를 번호로 고른다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class VfxGPUSimulation
     * @brief 재생 번호(`VfxGPUEmitterRequest::_runID`)마다 GPU 상태(입자 상태 두 반쪽, 렌더 입력, 간접 인자)를 들고, 프레임마다 밀린 단계를 디스패치합니다.
     * @details 기록 전(`prepareFrame`)에 상태를 만들고 매개변수(방출기 상수 · 단계마다의 일정 · 커브 키 · 해시 상수 · 방출기 행렬 · 깊이 히스토리)를 한 버퍼로 올린다.
     *          일정은 게임 스레드와 같은 시계(`VfxEmitterClock`)를 여기서 다시 돌려 얻는다 — 요청이 누적 단계 수라 패킷을 건너뛴 프레임도 다음 프레임에 이어 돈다
     *          (디스패치마다 상한 SW_VFX_MAX_STEPS_PER_DISPATCH, 넘친 단계는 다음 프레임). 컴퓨트 프리패스(`recordDispatches`)는 인스턴스 애니메이션 · 컬링 **앞에**
     *          방출기마다 그룹 하나를 디스패치하고, 메시 방출기는 이어서 인스턴스 칸을 쓴다(vfxmeshinstance.hlsl — 매 프레임. 배치 구성이 바뀌어 CPU 자리표가
     *          올라온 프레임도 그리기 전에 다시 쓴다). 빌보드 · 띠는 투명 패스가 렌더 입력을 t11 에 걸고 간접 인자로 그린다(`VfxParticleRenderer::drawBatches`).
     *          이번 프레임에 없는 재생 번호의 상태는 놓는다(멈췄거나 다시 재생했거나 컴포넌트가 사라졌다). 다시 나타나면 처음부터 다시 돌아 같은 결과에 닿는다.
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
         * @brief 이번 프레임의 GPU 배치 · GPU 메시 방출기마다 상태를 갖추고 매개변수를 올립니다. **기록 시작 전에**(GPUScene 을 올린 뒤) 부릅니다.
         * @details PSO 가 없으면 한 번 오류를 남기고 그 방출기를 그리지 않는다 — CPU 로 바꿔 그리지 않는다(대상은 데이터가 정했다).
         */
        void prepareFrame( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch, const vector<GPUVfxMeshEmitter>& listMeshEmitter,
                           const VfxSimulationFrameInput& input );
        /** @brief 컴퓨트 프리패스에 이번 프레임의 디스패치를 기록합니다(`prepareFrame` 뒤, 인스턴스 애니메이션 · 컬링 앞). */
        void recordDispatches( IRHICommandList& cmd, RHIPipelineStateHandle simulationPSO, RHIPipelineStateHandle meshInstancePSO ) const;
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
            EmitterState( shared_ptr<const VfxEmitterDef> pDef, float32 spawnScale );

            VfxEmitterClock         _clock;          ///< 게임 스레드와 같은 일정(여기서 다시 돈다)
            uint64                  _doneStepCount;  ///< 디스패치에 실은 단계 수
            RHIStructuredBufferSlot _stateBuffer;    ///< 입자 상태(풀 크기 × 2)
            RHIStructuredBufferSlot _particleBuffer; ///< 렌더 입력(풀 크기)
            RHIStructuredBufferSlot _argsBuffer;     ///< 간접 인자 · 살아 있는 수 · 버린 수(SW_VFX_ARGS_WORD_COUNT 단어)
            DrawSource              _drawSource;
            bool                    _bNeedsReset; ///< 아직 한 번도 디스패치하지 않았다
            bool                    _bSeen;       ///< 이번 프레임 목록에 있었다
        };

        /** @brief 이번 프레임의 시뮬레이션 디스패치 하나입니다. */
        struct Dispatch
        {
            const EmitterState* _pState{ nullptr }; ///< 이번 프레임 안에서만 빌린다(`_mapState` 가 소유)
            uint32              _paramRow{ 0 };
        };

        /** @brief 이번 프레임의 메시 인스턴스 쓰기 하나입니다. */
        struct MeshDispatch
        {
            const EmitterState* _pState{ nullptr }; ///< 이번 프레임 안에서만 빌린다
            uint32              _paramRow{ 0 };
            uint32              _groupCount{ 0 };
        };

        /** @brief 요청의 상태를 찾거나 만들고 버퍼를 갖춥니다. 버퍼를 만들지 못했으면 오류를 남기고 nullptr 입니다. */
        EmitterState* prepareEmitterState( IRHIDevice& device, const VfxGPUEmitterRequest& request );
        /** @brief @p state 의 시뮬레이션 디스패치를 붙입니다(밀린 단계가 없고 이미 돌았으면 매개변수를 버리고 붙이지 않는다). */
        void appendSimulation( EmitterState& state, const VfxGPUEmitterRequest& request, const float4x4& world, bool bRibbon );
        /** @brief @p state 의 이번 프레임 매개변수 구간을 `_listParamWord` 끝에 붙이고 디스패치할 단계 수를 돌려줍니다. */
        uint32 appendParams( EmitterState& state, uint64 seed, uint64 targetStepCount, const float4x4& world, bool bRibbon );
        /** @brief 메시 방출기 @p emitter 의 인스턴스 쓰기 구간을 붙입니다. */
        void appendMeshInstance( const EmitterState& state, const GPUVfxMeshEmitter& emitter );

        unordered_map<uint64, unique_ptr<EmitterState>> _mapState;
        vector<Dispatch>                                _listDispatch;
        vector<MeshDispatch>                            _listMeshDispatch;
        vector<const DrawSource*>                       _listBatchDrawSource; ///< 배치마다(GPU 배치가 아니면 nullptr) — 이번 프레임 안에서만 쓴다
        vector<uint32>                                  _listParamWord;       ///< 매개변수 행(uint4)을 단어로 이어 붙인 사본
        vector<uint64>                                  _listScratchStaleRunID;
        VfxSimulationFrameInput                         _frameInput; ///< 이번 프레임 입력(기록이 깊이 히스토리 · 인스턴스 버퍼를 건다)
        RHIStructuredBufferSlot                         _paramBuffer;
        bool                                            _bPipelineMissingLogged;
        bool                                            _bMeshPipelineMissingLogged;
    };
} // namespace sw
