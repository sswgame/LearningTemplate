/**
 * @file VfxParticleRenderer.h
 * @brief 방출기마다의 입자 그리기(`GPUParticleBatch`)를 투명 패스에 빌보드로 그립니다(렌더 스레드). `FrameRenderer` 가 소유합니다.
 * @details 캔버스 렌더러(`CanvasRenderer`)와 같은 모양이다: 기록 전(`prepareFrame`)에 입자 버퍼와 텍스처를 갖추고, 기록 중(`drawBatches`)에는 바인딩과 드로우만 한다.
 *          CPU 방출기의 입자는 프레임마다 모두 이어 붙인 구조 버퍼 하나로 올리고(패스 전용 슬롯 t11 — `shaderslot::kPassBuffer`), 방출기 하나가 `drawInstanced( 6, 입자 수 )` 하나다.
 *          GPU 방출기는 `VfxGPUSimulation` 이 컴퓨트 프리패스에서 만든 자기 렌더 입력을 t11 에 걸고 간접 인자(`drawIndirect`)로 그린다 — 입자 수를 CPU 가 모른다.
 *          PSO 와 PassCB 바인딩은 부르는 쪽(`FrameRenderer::drawParticleSlots`)이 한다. 그리는 순서와 메시 투명 배치 사이의 자리는 스냅샷의 자리 목록이 정한다.
 *          메시 배치 사이에 끼는 자리마다 PSO 를 입자 것으로 바꿨다가 되돌린다(교차 정렬의 비용 — 자리가 이어지는 입자는 한 번에 그린다).
 *
 *          주의: 표본 효과의 텍스처(Kenney 파티클)는 RGB 가 알파와 함께 어두워지는 형태라 곧은 알파로 섞으면 가장자리가 두 번 곱해져 흐리다.
 *          지금은 색 커브(더하기 방출기는 1 보다 큰 HDR 색)로 맞췄다. 텍스처 임포트에 "알파만 마스크" 규칙이 필요해지면 후보로 따로 적는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Vfx/VfxGPUSimulation.h"
#include "Engine/Vfx/VfxParticle.h"

namespace sw
{
    struct GPUParticleBatch;
    struct GPUTransparentParticleSlot;

    class IRHICommandList;
    class IRHIDevice;
    class Texture2D;

    /**
     * @class VfxParticleRenderer
     * @brief 입자 버퍼 하나와 입자 텍스처(경로로 빌린 것)를 가집니다.
     */
    class SW_API VfxParticleRenderer
    {
    public:
        VfxParticleRenderer();
        ~VfxParticleRenderer();

        VfxParticleRenderer( const VfxParticleRenderer& )            = delete;
        VfxParticleRenderer& operator=( const VfxParticleRenderer& ) = delete;

        /**
         * @brief 이번 프레임의 입자를 올리고 텍스처를 빌립니다. **기록 시작 전에** 부릅니다(버퍼 생성과 텍스처 읽기는 기록 중에 할 수 없다).
         * @details 방출기마다 버퍼 안 시작 위치를 기억한다(`drawBatches` 가 같은 목록으로 부른다). 처음 보는 텍스처 경로는 `TextureCache` 에서 빌리고 `release` 까지 든다.
         *          GPU 방출기(빌보드 · 띠 · 메시)의 상태와 매개변수도 여기서 갖춘다(`VfxGPUSimulation::prepareFrame`).
         * @param listMeshEmitter GPU 메시 방출기(인스턴스 칸 목록) — 그리기는 메시 경로가 한다
         * @param input           렌더러 쪽 입력(PSO 유무 · 깊이 히스토리 · 인스턴스 버퍼). PSO 가 없으면 GPU 방출기를 그리지 않고 한 번 오류를 남긴다
         */
        void prepareFrame( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch, const vector<GPUVfxMeshEmitter>& listMeshEmitter,
                           const VfxSimulationFrameInput& input );
        /** @brief GPU 방출기의 디스패치를 컴퓨트 프리패스에 기록합니다(`prepareFrame` 뒤, 인스턴스 애니메이션 · 컬링 앞). */
        void recordSimulation( IRHICommandList& cmd, RHIPipelineStateHandle simulationPSO, RHIPipelineStateHandle meshInstancePSO ) const
        {
            _gpuSimulation.recordDispatches( cmd, simulationPSO, meshInstancePSO );
        }
        /**
         * @brief 지금 열린 렌더 패스에 @p listSlot 의 방출기마다 그리기 하나를 그 순서로 기록합니다. PSO 와 PassCB 는 부르는 쪽이 걸어 두었어야 합니다.
         * @details 투명 패스는 메시 투명 배치 사이의 자리마다 그 자리의 구간으로 부른다(`FrameRenderer::drawParticleSlots`).
         * @param listBatch      `prepareFrame` 에 넘긴 것과 같은 목록(같은 프레임)
         * @param listSlot       그릴 입자 그리기(`GPUTransparentParticleSlot::_particleBatchIndex` 가 @p listBatch 의 번호)
         * @param bNativeBindless DX12 와 Vulkan 이면 텍스처를 bindless 전역 번호로, 아니면 머티리얼 텍스처 슬롯 t16 에 서수 0 으로 겁니다
         * @return 그린 방출기 수입니다(텍스처가 없는 방출기는 그리지 않습니다)
         */
        uint32 drawBatches( IRHICommandList& cmd, const vector<GPUParticleBatch>& listBatch, vector_reference<const GPUTransparentParticleSlot> listSlot,
                            bool bNativeBindless ) const;
        /** @brief 버퍼를 지우고 빌린 텍스처를 돌려줍니다(디바이스가 없으면 핸들만 잊습니다). 다음 `prepareFrame` 이 다시 만듭니다. */
        void release( IRHIDevice* pDevice );

        /** @brief 마지막 `prepareFrame` 이 올린 입자 수입니다(테스트와 진단용). */
        uint32 getUploadedParticleCount() const { return _uploadedParticleCount; }

    private:
        /** @brief 경로로 빌린 입자 텍스처 하나입니다(텍스처는 `TextureCache` 가 소유하며 `release` 가 돌려줍니다). */
        struct PathTexture
        {
            hashed_string _path{};
            Texture2D*    _pTexture{ nullptr }; ///< 읽지 못했으면 nullptr(캐시가 이유를 남겼으므로 다시 묻지 않는다)
        };

        /** @brief 경로의 텍스처 SRV 입니다. 빌리지 못했거나 읽지 못했으면 kInvalidDescriptorIndex 입니다. */
        RHIDescriptorIndex findTextureSrv( const hashed_string& path ) const;

        vector<GPUParticle>     _listUploadScratch; ///< 모든 방출기의 입자를 이어 붙인 사본
        vector<uint32>          _listBatchBase;     ///< 방출기마다 공유 버퍼 안 시작 위치(GPU 방출기는 자리표)
        vector<PathTexture>     _listTexture;       ///< 입자 텍스처(몇 개뿐이라 선형 검색)
        RHIStructuredBufferSlot _particleBuffer;
        VfxGPUSimulation        _gpuSimulation;
        uint32                  _uploadedParticleCount;
    };
} // namespace sw
