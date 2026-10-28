#include "pch.h"

#include "Engine/Renderer/VFX/VFXParticleRenderer.h"

#include "Core/Common/BitUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

namespace sw
{
    SW_LOG_CALLER( "VFXParticleRenderer" );

    namespace
    {
        struct VFXParticleRendererInternal
        {
            /** @brief 루트 상수 칸입니다 — particle.hlsl 의 SwRootConstants 순서(시작 · 텍스처 · 플래그 · 크기 배율 · 방출기 행렬의 열 0..2). */
            static constexpr uint32 kRootParticleBase  = 0;
            static constexpr uint32 kRootTexture       = 1;
            static constexpr uint32 kRootFlags         = 2;
            static constexpr uint32 kRootSizeScale     = 3;
            static constexpr uint32 kRootColumn0       = 4;
            static constexpr uint32 kColumnCount       = 3;
            static constexpr uint32 kFloatsPerColumn   = 4;
            static constexpr uint32 kRootConstantCount = kRootColumn0 + kColumnCount * kFloatsPerColumn;
            static_assert( kRootConstantCount <= shaderslot::kRootConstantDwords, "particle root constants exceed the four-backend budget" );
            /** @brief 플래그 칸의 비트입니다 — particle.hlsl 의 kParticleFlag* 와 같다. */
            static constexpr uint32 kFlagAdditive = 1u << 0;
            static constexpr uint32 kFlagRibbon   = 1u << 1;

            // 메시 없는 드로우라 장치 공용 대체 정점 버퍼가 입자 하나의 정점 수만큼 담아야 한다.
            static_assert( GPUParticle::kVertexCount <= constant::kFallbackVertexCount, "the fallback vertex buffer must cover a particle quad (slot 0 is read past its end)" );
            /** @brief GPU 방출기의 버퍼 안 시작 위치 자리표입니다 — 자기 렌더 입력 버퍼를 0 부터 읽는다. */
            static constexpr uint32 kNoSharedBase = invalid_index::kUint32;

            /** @brief 입자 버퍼가 모자라면 이 배율로 키운다(입자 수가 프레임마다 조금씩 늘 때 다시 만들기를 줄인다). */
            static constexpr float32 kParticleBufferGrowth = 1.5f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    VFXParticleRenderer::VFXParticleRenderer()
        : _listUploadScratch{}
        , _listBatchBase{}
        , _listTexture{}
        , _particleBuffer{}
        , _gpuSimulation{}
        , _uploadedParticleCount{ 0 }
    {
    }

    VFXParticleRenderer::~VFXParticleRenderer() = default;

    void VFXParticleRenderer::prepareFrame( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch, const vector<GPUVFXMeshEmitter>& listMeshEmitter,
                                            const VFXSimulationFrameInput& input )
    {
        _listBatchBase.clear();
        _uploadedParticleCount = 0;
        // GPU 방출기의 상태는 배치가 없는 프레임에도 정리해야 한다(멈춘 효과의 버퍼를 놓는다) — 빈 목록이어도 부른다.
        _gpuSimulation.prepareFrame( device, listBatch, listMeshEmitter, input );
        if ( listBatch.empty() )
            return;
        SW_PROFILE_SCOPE( "RT.VFX.Upload" );

        // 1) 텍스처 — 게임 스레드는 디바이스가 없어 경로만 실었다. 머티리얼 텍스처와 같은 캐시에서 빌린다(기록 전이라 만들 수 있다).
        const bool bServices = engine::areEngineServicesBound();
        for ( const GPUParticleBatch& batch : listBatch )
        {
            bool bKnown = false;
            for ( const PathTexture& texture : _listTexture )
            {
                bKnown = bKnown || texture._path == batch._texturePath;
            }
            if ( bKnown || bServices == false )
                continue;
            PathTexture& added = _listTexture.emplace_back();
            added._path        = batch._texturePath;
            added._pTexture    = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().acquire( batch._texturePath.view(), &device );
        }

        // 2) 입자 — CPU 방출기들을 이어 한 버퍼에 담는다(같은 프레임에 버퍼를 두 번 갱신하지 않는다). GPU 방출기는 자기 렌더 입력 버퍼를 읽는다.
        _listUploadScratch.clear();
        for ( const GPUParticleBatch& batch : listBatch )
        {
            const vector<GPUParticle>* pListParticle = batch.findCPUParticles();
            if ( pListParticle == nullptr )
            {
                _listBatchBase.push_back( VFXParticleRendererInternal::kNoSharedBase );
                continue;
            }
            _listBatchBase.push_back( static_cast<uint32>( _listUploadScratch.size() ) );
            _listUploadScratch.insert( _listUploadScratch.end(), pListParticle->begin(), pListParticle->end() );
        }
        const uint32 particleCount = static_cast<uint32>( _listUploadScratch.size() );
        if ( particleCount == 0 )
            return;
        if ( _particleBuffer._capacityElements < particleCount || _particleBuffer.isValid() == false )
        {
            const uint32 capacity =
                MathUtil::max( particleCount, static_cast<uint32>( static_cast<float32>( _particleBuffer._capacityElements ) * VFXParticleRendererInternal::kParticleBufferGrowth ) );
            _particleBuffer.release( &device );
            if ( _particleBuffer.ensureCapacity( &device, sizeof( GPUParticle ), capacity, RHIBufferUsage::ShaderResource, true, false, nullptr ) == false )
            {
                SW_LOG_ERROR( "Failed to create the particle buffer (%# particles) - particles are not drawn", capacity );
                return;
            }
        }
        _particleBuffer.upload( &device, _listUploadScratch.data(), particleCount * static_cast<uint32>( sizeof( GPUParticle ) ) );
        _uploadedParticleCount = particleCount;
    }

    uint32 VFXParticleRenderer::drawBatches( IRHICommandList& cmd, const vector<GPUParticleBatch>& listBatch, vector_reference<const GPUTransparentParticleSlot> listSlot,
                                             bool bNativeBindless ) const
    {
        const bool bNothingToDraw = listSlot.empty() || _listBatchBase.size() != listBatch.size();
        if ( bNothingToDraw )
            return 0;
        SW_PROFILE_SCOPE( "RT.VFX.Draw" );

        // 바인딩은 PSO 뒤에 건다(setPipelineState 가 슬롯 상태를 비운다) — 부르는 쪽이 PSO 를 걸고 PassCB 를 바인딩한 뒤 여기로 온다.
        // t11 은 CPU 방출기의 공유 버퍼이거나 GPU 방출기 자기의 렌더 입력이다 — 바뀔 때만 다시 건다.
        const bool         bSharedReady    = _uploadedParticleCount > 0 && _particleBuffer.isValid();
        RHIDescriptorIndex boundParticles  = kInvalidDescriptorIndex;
        uint32             drawnBatchCount = 0;
        for ( const GPUTransparentParticleSlot& slot : listSlot )
        {
            using Root              = VFXParticleRendererInternal;
            const uint32 batchIndex = slot._particleBatchIndex;
            SW_ASSERT( batchIndex < listBatch.size() );
            const GPUParticleBatch&             batch       = listBatch[batchIndex];
            const VFXGPUSimulation::DrawSource* pGPUSource  = _gpuSimulation.findDrawSource( batchIndex );
            const bool                          bGPUBatch   = batch.findGPURequest() != nullptr;
            const bool                          bSourceGone = bGPUBatch ? pGPUSource == nullptr : bSharedReady == false;
            const RHIDescriptorIndex            srv         = findTextureSRV( batch._texturePath );
            if ( srv == kInvalidDescriptorIndex || bSourceGone )
                continue;
            const RHIDescriptorIndex particleSRV = bGPUBatch ? pGPUSource->_particleSRV : _particleBuffer._srv;
            if ( particleSRV != boundParticles )
            {
                cmd.bindStructuredBuffer( particleSRV, shaderslot::kPassBuffer );
                boundParticles = particleSRV;
            }

            uint32 arrRoot[Root::kRootConstantCount]{};
            arrRoot[Root::kRootParticleBase] = bGPUBatch ? 0u : _listBatchBase[batchIndex];
            arrRoot[Root::kRootFlags]        = ( batch._bAdditive == SW_TRUE ? Root::kFlagAdditive : 0u ) | ( batch._bRibbon == SW_TRUE ? Root::kFlagRibbon : 0u );
            arrRoot[Root::kRootSizeScale]    = BitUtil::floatToBits( batch._sizeScale );
            if ( bNativeBindless )
            {
                arrRoot[Root::kRootTexture] = srv; // 전역 번호
            }
            else
            {
                // 에뮬 백엔드(DX11 · GL)는 머티리얼 텍스처 슬롯 t16(kMaterialTexture0) 에 서수로 건다 — MaterialTexture::sampleIndex 가 서수로 고른다.
                cmd.bindShaderResource( srv, shaderslot::kMaterialTexture0 );
                arrRoot[Root::kRootTexture] = 0;
            }
            // 행벡터 규약의 열 c = (_1c, _2c, _3c, _4c) — 셰이더가 dot( float4( 로컬, 1 ), 열 ) 로 월드 좌표 c 를 얻는다.
            const float4x4& world                                                 = batch._world;
            const float32   arrColumn[Root::kColumnCount][Root::kFloatsPerColumn] = {
                {world._11, world._21, world._31, world._41},
                {world._12, world._22, world._32, world._42},
                {world._13, world._23, world._33, world._43}
            };
            for ( uint32 column = 0; column < Root::kColumnCount; ++column )
            {
                for ( uint32 row = 0; row < Root::kFloatsPerColumn; ++row )
                {
                    arrRoot[Root::kRootColumn0 + column * Root::kFloatsPerColumn + row] = BitUtil::floatToBits( arrColumn[column][row] );
                }
            }
            cmd.setGraphicsRootConstants( 0, Root::kRootConstantCount, arrRoot );
            ++drawnBatchCount;
            if ( bGPUBatch )
            {
                // 정점 수 · 인스턴스 수(띠는 마디 수)는 컴퓨트가 썼다(vfxsimulate.hlsl). 시작 인스턴스는 0 이다.
                cmd.drawIndirect( pGPUSource->_argsBuffer, 0, 1 );
                continue;
            }
            // 시작 인스턴스는 0 — D3D 는 SV_InstanceID 에 시작 인스턴스를 더하지 않는다. 시작 입자는 루트 상수로 간다.
            // 띠는 이웃 입자 둘이 한 마디(사각형 하나)라 인스턴스가 입자 수보다 하나 적다(빌더가 입자 둘 이상만 싣는다).
            const uint32 particleCount = static_cast<uint32>( batch.findCPUParticles()->size() );
            const uint32 instanceCount = batch._bRibbon == SW_TRUE ? particleCount - 1u : particleCount;
            cmd.drawInstanced( GPUParticle::kVertexCount, instanceCount, 0, 0 );
        }
        return drawnBatchCount;
    }

    void VFXParticleRenderer::release( IRHIDevice* pDevice )
    {
        if ( _listTexture.empty() == false && engine::areEngineServicesBound() )
        {
            TextureCache& textures = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
            for ( const PathTexture& texture : _listTexture )
            {
                if ( texture._pTexture != nullptr )
                    textures.release( texture._path.view(), pDevice );
            }
        }
        _listTexture.clear();
        _particleBuffer.release( pDevice );
        _gpuSimulation.release( pDevice );
        _listBatchBase.clear();
        _uploadedParticleCount = 0;
    }

    RHIDescriptorIndex VFXParticleRenderer::findTextureSRV( const hashed_string& path ) const
    {
        for ( const PathTexture& texture : _listTexture )
        {
            if ( texture._path == path )
                return texture._pTexture != nullptr && texture._pTexture->isRHIValid() ? texture._pTexture->getSRV() : kInvalidDescriptorIndex;
        }
        return kInvalidDescriptorIndex;
    }
} // namespace sw
