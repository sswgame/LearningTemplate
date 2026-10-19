/**
 * @file VfxParticleRenderer.h
 * @brief 방출기마다의 입자 그리기(`GPUParticleBatch`)를 투명 패스에 빌보드로 그립니다(렌더 스레드). `FrameRenderer` 가 소유합니다.
 * @details 캔버스 렌더러(`CanvasRenderer`)와 같은 모양이다: 기록 전(`prepareFrame`)에 입자 버퍼와 텍스처를 갖추고, 기록 중(`drawBatches`)에는 바인딩과 드로우만 한다.
 *          입자는 프레임마다 모든 방출기를 이어 붙인 구조 버퍼 하나로 올리고(패스 전용 슬롯 t11 — `shaderslot::kPassBuffer`), 방출기 하나가 `drawInstanced( 6, 입자 수 )` 하나다.
 *          PSO 와 PassCB 바인딩은 부르는 쪽(`FrameRenderer::drawParticles`)이 한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Vfx/VfxParticle.h"

namespace sw
{
    struct GPUParticleBatch;

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
         */
        void prepareFrame( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch );
        /**
         * @brief 지금 열린 렌더 패스에 방출기마다 그리기 하나를 기록합니다. PSO 와 PassCB 는 부르는 쪽이 걸어 두었어야 합니다.
         * @param listBatch      `prepareFrame` 에 넘긴 것과 같은 목록(같은 프레임)
         * @param bNativeBindless DX12 와 Vulkan 이면 텍스처를 bindless 전역 번호로, 아니면 t12 에 서수 0 으로 겁니다
         * @return 그린 방출기 수입니다(텍스처가 없는 방출기는 그리지 않습니다)
         */
        uint32 drawBatches( IRHICommandList& cmd, const vector<GPUParticleBatch>& listBatch, bool bNativeBindless ) const;
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
        vector<uint32>          _listBatchBase;     ///< 방출기마다 버퍼 안 시작 위치
        vector<PathTexture>     _listTexture;       ///< 입자 텍스처(몇 개뿐이라 선형 검색)
        RHIStructuredBufferSlot _particleBuffer;
        uint32                  _uploadedParticleCount;
    };
} // namespace sw
