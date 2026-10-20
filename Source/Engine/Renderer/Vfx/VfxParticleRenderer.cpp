#include "pch.h"

#include "Engine/Renderer/Vfx/VfxParticleRenderer.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"
#include "Engine/Resource/AssetManager.h"

namespace sw
{
    SW_LOG_CALLER( "VfxParticleRenderer" );

    namespace
    {
        struct VfxParticleRendererInternal
        {
            /** @brief 루트 상수 칸입니다 — particle.hlsl 의 SwRootConstants 순서(시작 · 텍스처 · 더하기 · 크기 배율 · 방출기 행렬의 열 0..2). */
            static constexpr uint32 kRootParticleBase  = 0;
            static constexpr uint32 kRootTexture       = 1;
            static constexpr uint32 kRootAdditive      = 2;
            static constexpr uint32 kRootSizeScale     = 3;
            static constexpr uint32 kRootColumn0       = 4;
            static constexpr uint32 kColumnCount       = 3;
            static constexpr uint32 kFloatsPerColumn   = 4;
            static constexpr uint32 kRootConstantCount = kRootColumn0 + kColumnCount * kFloatsPerColumn;
            static_assert( kRootConstantCount <= shaderslot::kRootConstantDwords, "particle root constants exceed the four-backend budget" );

            /** @brief 입자 하나의 정점 수입니다 — particle.hlsl 이 SV_VertexID 로 모서리 여섯을 만든다. 메시 없는 드로우라 장치 공용 대체 정점 버퍼가 이만큼 담아야 한다. */
            static constexpr uint32 kVerticesPerParticle = 6;
            static_assert( kVerticesPerParticle <= constant::kFallbackVertexCount, "the fallback vertex buffer must cover a particle quad (slot 0 is read past its end)" );

            /** @brief 입자 버퍼가 모자라면 이 배율로 키운다(입자 수가 프레임마다 조금씩 늘 때 다시 만들기를 줄인다). */
            static constexpr float32 kParticleBufferGrowth = 1.5f;

            /** @brief float 의 비트를 uint32 로 옮깁니다(루트 상수는 dword 배열이다). */
            static uint32 toBits( float32 value )
            {
                uint32 bits = 0;
                Memory::copy( &bits, &value, sizeof( bits ) );
                return bits;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    VfxParticleRenderer::VfxParticleRenderer()
        : _listUploadScratch{}
        , _listBatchBase{}
        , _listTexture{}
        , _particleBuffer{}
        , _uploadedParticleCount{ 0 }
    {
    }

    VfxParticleRenderer::~VfxParticleRenderer() = default;

    void VfxParticleRenderer::prepareFrame( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch )
    {
        _listBatchBase.clear();
        _uploadedParticleCount = 0;
        if ( listBatch.empty() )
            return;
        SW_PROFILE_SCOPE( "RT.Vfx.Upload" );

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

        // 2) 입자 — 방출기들을 이어 한 버퍼에 담는다(같은 프레임에 버퍼를 두 번 갱신하지 않는다).
        _listUploadScratch.clear();
        for ( const GPUParticleBatch& batch : listBatch )
        {
            _listBatchBase.push_back( static_cast<uint32>( _listUploadScratch.size() ) );
            _listUploadScratch.insert( _listUploadScratch.end(), batch._pListParticle->begin(), batch._pListParticle->end() );
        }
        const uint32 particleCount = static_cast<uint32>( _listUploadScratch.size() );
        if ( particleCount == 0 )
            return;
        if ( _particleBuffer._capacityElements < particleCount || _particleBuffer.isValid() == false )
        {
            const uint32 capacity =
                MathUtil::max( particleCount, static_cast<uint32>( static_cast<float32>( _particleBuffer._capacityElements ) * VfxParticleRendererInternal::kParticleBufferGrowth ) );
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

    uint32 VfxParticleRenderer::drawBatches( IRHICommandList& cmd, const vector<GPUParticleBatch>& listBatch, vector_reference<const GPUTransparentParticleSlot> listSlot,
                                             bool bNativeBindless ) const
    {
        const bool bNothingToDraw =
            listSlot.empty() || _uploadedParticleCount == 0 || _particleBuffer.isValid() == false || _listBatchBase.size() != listBatch.size();
        if ( bNothingToDraw )
            return 0;
        SW_PROFILE_SCOPE( "RT.Vfx.Draw" );

        // 바인딩은 PSO 뒤에 건다(setPipelineState 가 슬롯 상태를 비운다) — 부르는 쪽이 PSO 를 걸고 PassCB 를 바인딩한 뒤 여기로 온다.
        cmd.bindStructuredBuffer( _particleBuffer._srv, shaderslot::kPassBuffer );
        uint32 drawnBatchCount = 0;
        for ( const GPUTransparentParticleSlot& slot : listSlot )
        {
            const uint32 batchIndex = slot._particleBatchIndex;
            SW_ASSERT( batchIndex < listBatch.size() );
            const GPUParticleBatch&  batch = listBatch[batchIndex];
            const RHIDescriptorIndex srv   = findTextureSrv( batch._texturePath );
            if ( srv == kInvalidDescriptorIndex )
                continue;

            using Root = VfxParticleRendererInternal;
            uint32 arrRoot[Root::kRootConstantCount]{};
            arrRoot[Root::kRootParticleBase] = _listBatchBase[batchIndex];
            arrRoot[Root::kRootAdditive]     = batch._bAdditive == SW_TRUE ? 1u : 0u;
            arrRoot[Root::kRootSizeScale]    = Root::toBits( batch._sizeScale );
            if ( bNativeBindless )
            {
                arrRoot[Root::kRootTexture] = srv; // 전역 번호
            }
            else
            {
                // 에뮬 백엔드(DX11 · GL)는 머티리얼 텍스처 슬롯 t12 에 서수로 건다 — swSampleMaterialTexture 가 서수로 고른다.
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
                    arrRoot[Root::kRootColumn0 + column * Root::kFloatsPerColumn + row] = Root::toBits( arrColumn[column][row] );
                }
            }
            cmd.setGraphicsRootConstants( 0, Root::kRootConstantCount, arrRoot );
            // 시작 인스턴스는 0 — D3D 는 SV_InstanceID 에 시작 인스턴스를 더하지 않는다. 시작 입자는 루트 상수로 간다.
            cmd.drawInstanced( Root::kVerticesPerParticle, static_cast<uint32>( batch._pListParticle->size() ), 0, 0 );
            ++drawnBatchCount;
        }
        return drawnBatchCount;
    }

    void VfxParticleRenderer::release( IRHIDevice* pDevice )
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
        _listBatchBase.clear();
        _uploadedParticleCount = 0;
    }

    RHIDescriptorIndex VfxParticleRenderer::findTextureSrv( const hashed_string& path ) const
    {
        for ( const PathTexture& texture : _listTexture )
        {
            if ( texture._path == path )
                return texture._pTexture != nullptr && texture._pTexture->isRHIValid() ? texture._pTexture->getSrv() : kInvalidDescriptorIndex;
        }
        return kInvalidDescriptorIndex;
    }
} // namespace sw
