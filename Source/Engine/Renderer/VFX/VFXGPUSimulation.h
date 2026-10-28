/**
 * @file VFXGPUSimulation.h
 * @brief GPU 방출기의 입자 시뮬레이션(렌더 스레드, vfxsimulate.hlsl · vfxmeshinstance.hlsl)입니다. `VFXParticleRenderer` 가 소유합니다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/VFX/VFXEmitterClock.h"
#include "Engine/VFX/VFXParticle.h"

namespace sw
{
    struct GPUParticleBatch;
    struct GPUVFXMeshEmitter;

    class IRHICommandList;
    class IRHIDevice;

    /**
     * @struct VFXDepthHistoryInput
     * @brief 깊이 충돌이 읽는 주 시점의 깊이 히스토리입니다(`FrameRenderer` 의 뷰마다의 히스토리 중 주 시점 것). `_texture` 가 0 이면 이번 프레임에 없다.
     */
    struct VFXDepthHistoryInput
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
    /** @brief `VFXGPUSimulation::prepareFrame` 이 이번 프레임에 받는 렌더러 쪽 입력입니다. */
    struct VFXSimulationFrameInput
    {
        VFXDepthHistoryInput _depth{};
        RHIBufferHandle      _instanceBuffer{ 0 }; ///< GPUScene 인스턴스 버퍼(메시 입자가 쓴다)
        RHIDescriptorIndex   _instanceUAV{ kInvalidDescriptorIndex };
        RHIBufferHandle      _lightBuffer{ 0 };                    ///< 라이트 버퍼(`GPULightBuffer`) — 파티클 라이트가 GPU 구간에 쓴다
        RHIDescriptorIndex   _lightUAV{ kInvalidDescriptorIndex }; ///< 라이트 버퍼의 UAV. GPU 구간이 없는 프레임은 invalid 다
        uint32               _lightBase{ 0 };                      ///< GPU 구간의 첫 라이트 번호(`GPULightBuffer::getGPULightBase`)
        uint32               _lightSlotCount{ 0 };                 ///< 받은 GPU 구간의 슬롯 수 — 0 이면 이번 프레임에 파티클 라이트를 쓰지 않는다
        bool                 _bSimulationPipelineReady{ false };   ///< vfxsimulate.hlsl PSO 가 있다
        bool                 _bMeshPipelineReady{ false };         ///< vfxmeshinstance.hlsl PSO 가 있다
        bool                 _bNativeBindless{ false };            ///< DX12 · Vulkan 이면 true(RW 텍스처를 번호로 고른다)
    };
} // namespace sw

namespace sw
{
    /** @brief 방출기 하나가 파티클 라이트에 받은 GPU 구간의 슬롯 범위입니다(구간 안의 상대 번호). `_count` 가 0 이면 라이트를 쓰지 않는다. */
    struct VFXGPULightRange
    {
        uint32 _first{ 0 };
        uint32 _count{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief `VFXGPUSimulation::assignLightRanges` 의 결과 요약입니다. */
    struct VFXGPULightAssignment
    {
        uint32 _slotCount{ 0 };            ///< 나눈 슬롯의 합 — GPU 구간이 받아야 할 크기
        uint32 _overflowEmitterCount{ 0 }; ///< 구간 상한에 들지 못해 라이트를 받지 못한 방출기 수
    };
} // namespace sw

namespace sw
{
    /**
     * @class VFXGPUSimulation
     * @brief 재생 번호(`VFXGPUEmitterRequest::_runID`)마다 GPU 상태(입자 상태 두 반쪽, 렌더 입력, 간접 인자)를 들고, 프레임마다 밀린 단계를 디스패치합니다.
     * @details 기록 전(`prepareFrame`)에 상태를 만들고 매개변수(방출기 상수 · 단계마다의 일정 · 커브 키 · 해시 상수 · 방출기 행렬 · 깊이 히스토리)를 한 버퍼로 올린다.
     *          일정은 게임 스레드와 같은 시계(`VFXEmitterClock`)를 여기서 다시 돌려 얻는다 — 요청이 누적 단계 수라 패킷을 건너뛴 프레임도 다음 프레임에 이어 돈다
     *          (디스패치마다 상한 SW_VFX_MAX_STEPS_PER_DISPATCH, 넘친 단계는 다음 프레임). 컴퓨트 프리패스(`recordDispatches`)는 인스턴스 애니메이션 · 컬링 **앞에**
     *          방출기마다 그룹 하나(풀이 `SW_VFX_MULTI_GROUP_MIN_POOL` 이상이면 단계마다 적분 · 접두사 합 · 압축 디스패치 셋 — 같은 결과, 언리얼 Niagara GPU 압축 · 유니티 VFX Graph dead list 와 같은 선)를 디스패치하고, 메시 방출기는 이어서 인스턴스 칸을 쓴다(vfxmeshinstance.hlsl — 매 프레임. 배치 구성이 바뀌어 CPU 자리표가
     *          올라온 프레임도 그리기 전에 다시 쓴다). 빌보드 · 띠는 투명 패스가 렌더 입력을 t11 에 걸고 간접 인자로 그린다(`VFXParticleRenderer::drawBatches`).
     *          이번 프레임에 없는 재생 번호의 상태는 놓는다(멈췄거나 다시 재생했거나 컴포넌트가 사라졌다). 다시 나타나면 처음부터 다시 돌아 같은 결과에 닿는다.
     */
    class SW_API VFXGPUSimulation
    {
    public:
        /** @brief GPU 방출기 하나를 그릴 자원입니다. */
        struct DrawSource
        {
            RHIDescriptorIndex _particleSRV{ kInvalidDescriptorIndex }; ///< 렌더 입력(GPUParticle) 구조 버퍼 — t11
            RHIBufferHandle    _argsBuffer{ 0 };                        ///< 간접 인자(RHIDrawIndirectCommand 가 맨 앞)
        };

        VFXGPUSimulation();
        ~VFXGPUSimulation();

        VFXGPUSimulation( const VFXGPUSimulation& )            = delete;
        VFXGPUSimulation& operator=( const VFXGPUSimulation& ) = delete;

        /**
         * @brief GPU 방출기마다 파티클 라이트 슬롯(`VFXLightDef::_maxLights`)을 라이트 버퍼의 GPU 구간에 차례로 나눕니다. 렌더러가 라이트 버퍼를 올리기 전에 슬롯 합을 알고(`FrameRenderer::updateLightBuffer`),
         *        시뮬레이션이 같은 목록으로 자기 범위를 얻습니다(`prepareFrame`) — 두 곳이 같은 입력에 이 함수 하나를 부르므로 범위가 어긋나지 않습니다.
         * @details 순서는 GPU 배치(목록 순서), 그 뒤 GPU 메시 방출기입니다. 상한(`GPULightBuffer::kMaxGPULight`)에 들지 않는 방출기는 라이트가 없습니다(`_overflowEmitterCount`, 부르는 쪽이 알린다).
         *          그릴 수 없는 방출기(요청이 비었다 · 메시 인스턴스 칸이 없다)는 슬롯을 받지 않는다 — 받으면 디스패치되지 않아 죽은 슬롯으로 남는다.
         * @param outListRange 지우고 채웁니다. 크기는 배치 수 + 메시 방출기 수이고, 번호는 배치가 `batchIndex`, 메시 방출기가 `listBatch.size() + meshIndex` 입니다.
         */
        static VFXGPULightAssignment assignLightRanges( const vector<GPUParticleBatch>& listBatch, const vector<GPUVFXMeshEmitter>& listMeshEmitter,
                                                        vector<VFXGPULightRange>& outListRange );

        /**
         * @brief 이번 프레임의 GPU 배치 · GPU 메시 방출기마다 상태를 갖추고 매개변수를 올립니다. **기록 시작 전에**(GPUScene 을 올린 뒤) 부릅니다.
         * @details PSO 가 없으면 한 번 오류를 남기고 그 방출기를 그리지 않는다 — CPU 로 바꿔 그리지 않는다(대상은 데이터가 정했다).
         *          파티클 라이트가 있는 방출기는 밀린 단계가 없어도 매 프레임 디스패치한다 — 라이트 구간의 위치(CPU 라이트 수)와 월드 변환이 프레임마다 바뀌므로 다시 쓴다.
         */
        void prepareFrame( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch, const vector<GPUVFXMeshEmitter>& listMeshEmitter,
                           const VFXSimulationFrameInput& input );
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
            EmitterState( shared_ptr<const VFXEmitterDef> pDef, float32 spawnScale );

            VFXEmitterClock         _clock;          ///< 게임 스레드와 같은 일정(여기서 다시 돈다)
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
            bool                _bLights{ false }; ///< 파티클 라이트를 GPU 구간에 쓴다(u3 에 라이트 버퍼를 건다)
            bool                _bReset{ false };  ///< 재생 시작(다중 그룹 경로가 시작 패스를 돈다)
            uint32              _stepCount{ 0 };   ///< 이번 디스패치가 도는 단계 수(다중 그룹 경로가 단계마다 패스 셋을 기록한다)
        };

        /** @brief 이번 프레임의 메시 인스턴스 쓰기 하나입니다. */
        struct MeshDispatch
        {
            const EmitterState* _pState{ nullptr }; ///< 이번 프레임 안에서만 빌린다
            uint32              _paramRow{ 0 };
            uint32              _groupCount{ 0 };
        };

        /** @brief 풀이 커서 단계마다 디스패치 셋으로 도는 방출기입니다(`SW_VFX_MULTI_GROUP_MIN_POOL` — 두 경로는 같은 결과다). */
        static bool usesMultiGroup( const VFXEmitterDef& def );
        /** @brief 다중 그룹 경로가 한 패스에 거는 그룹 수입니다(풀 / 스레드 수 올림). */
        static uint32 getGroupCount( const VFXEmitterDef& def );
        /** @brief 방출기 하나의 시뮬레이션 패스들을 기록합니다(그룹 하나 경로는 디스패치 하나, 다중 그룹은 패스마다 하나와 UAV 장벽). */
        void recordEmitterPasses( IRHICommandList& cmd, const Dispatch& dispatch ) const;

        /** @brief GPU 로 돌릴 방출기가 있는데 시뮬레이션 컴퓨트 PSO 가 없으면 한 번만 오류를 남깁니다(`prepareFrame` 의 한 단계). */
        void reportMissingSimulationPipeline( bool bAnyGPUBatch );
        /** @brief 파티클 라이트 범위를 렌더러가 구간 크기를 정할 때와 같은 함수로 구해 `_listLightRange` 에 담습니다(어긋나면 비웁니다). */
        void assignParticleLights( const vector<GPUParticleBatch>& listBatch, const vector<GPUVFXMeshEmitter>& listMeshEmitter );
        /** @brief 파티클 배치마다 방출기 상태를 갖추고 시뮬레이션을 더합니다. */
        void prepareParticleEmitters( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch );
        /** @brief 메시 방출기마다 방출기 상태를 갖추고 시뮬레이션과 메시 인스턴스 쓰기를 더합니다. */
        void prepareMeshEmitters( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch, const vector<GPUVFXMeshEmitter>& listMeshEmitter );
        /** @brief 이번 프레임에 없는 재생(멈춤 · 다시 재생 · 컴포넌트 제거)의 GPU 버퍼를 놓습니다. */
        void releaseStaleEmitters( IRHIDevice& device );
        /** @brief 모은 매개변수 단어를 버퍼에 올립니다. 버퍼를 잡지 못하면 이번 프레임의 디스패치를 모두 비웁니다. */
        void uploadParamBuffer( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch );
        /** @brief 요청의 상태를 찾거나 만들고 버퍼를 갖춥니다. 버퍼를 만들지 못했으면 오류를 남기고 nullptr 입니다. */
        EmitterState* prepareEmitterState( IRHIDevice& device, const VFXGPUEmitterRequest& request );
        /** @brief @p state 의 시뮬레이션 디스패치를 붙입니다(밀린 단계가 없고 이미 돌았으면 매개변수를 버리고 붙이지 않는다). */
        void appendSimulation( EmitterState& state, const VFXGPUEmitterRequest& request, const float4x4& world, bool bRibbon, const VFXGPULightRange& lightRange );
        /** @brief @p state 의 이번 프레임 매개변수 구간을 `_listParamWord` 끝에 붙이고 디스패치할 단계 수를 돌려줍니다. */
        uint32 appendParams( EmitterState& state, uint64 seed, uint64 targetStepCount, const float4x4& world, bool bRibbon, const VFXGPULightRange& lightRange );
        /** @brief 메시 방출기 @p emitter 의 인스턴스 쓰기 구간을 붙입니다. */
        void appendMeshInstance( const EmitterState& state, const GPUVFXMeshEmitter& emitter );

        unordered_map<uint64, unique_ptr<EmitterState>> _mapState;
        vector<Dispatch>                                _listDispatch;
        vector<MeshDispatch>                            _listMeshDispatch;
        vector<const DrawSource*>                       _listBatchDrawSource; ///< 배치마다(GPU 배치가 아니면 nullptr) — 이번 프레임 안에서만 쓴다
        vector<uint32>                                  _listParamWord;       ///< 매개변수 행(uint4)을 단어로 이어 붙인 사본
        vector<uint64>                                  _listScratchStaleRunID;
        vector<VFXGPULightRange>                        _listLightRange; ///< 이번 프레임의 방출기별 파티클 라이트 슬롯 범위(`assignLightRanges`)
        VFXSimulationFrameInput                         _frameInput;     ///< 이번 프레임 입력(기록이 깊이 히스토리 · 인스턴스 버퍼 · 라이트 버퍼를 건다)
        RHIStructuredBufferSlot                         _paramBuffer;
        bool                                            _bPipelineMissingLogged;
        bool                                            _bMeshPipelineMissingLogged;
        bool                                            _bLightOverflowLogged;
    };
} // namespace sw
