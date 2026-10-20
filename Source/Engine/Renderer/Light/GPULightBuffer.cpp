#include "pch.h"

#include "Engine/Renderer/Light/GPULightBuffer.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/Component/2D/ShadowCaster2DComponent.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/LightProbeVolumeComponent.h"
#include "Engine/Object/Component/3D/PointLightComponent.h"
#include "Engine/Object/Component/3D/SpotLightComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    SW_LOG_CALLER( "GPULightBuffer" );

    namespace
    {
        struct GPULightBufferInternal
        {
            /** @brief 그림자를 켠 점광과 스폿이면 true 입니다(방향광은 씬이 하나 고릅니다). */
            static bool castsLocalShadow( const LightComponent& light )
            {
                if ( light.getLightType() == shaderslot::kLightTypePoint )
                    return static_cast<const PointLightComponent&>( light ).castsShadow();
                if ( light.getLightType() == shaderslot::kLightTypeSpot )
                    return static_cast<const SpotLightComponent&>( light ).castsShadow();
                return false;
            }

            /**
             * @brief 프로브 볼륨 하나를 라이트 원소(bindingslots.hlsli 8 절의 SW_LIGHT_TYPE_PROBE_VOLUME 배치)로 만들고, 그 프로브들의 SH9 원소를 붙입니다.
             * @details 경계 구는 상자의 외접구다 — 라이트 그리드가 점광과 같은 식으로 클러스터에 넣는다. 수와 번호는 실수로 싣는다(2^24 안이면 정확하다).
             */
            static GPULight makeProbeVolumeElement( const LightProbeGrid& grid, vector<float4>& inoutListProbeElement )
            {
                const LightProbeGridLayout& layout = grid._layout;
                const float3                extent{ layout._spacing._x * static_cast<float32>( layout._countX - 1u ), layout._spacing._y * static_cast<float32>( layout._countY - 1u ),
                                     layout._spacing._z * static_cast<float32>( layout._countZ - 1u ) };
                const float3                center    = layout._origin + extent * 0.5f;
                const uint32                probeBase = static_cast<uint32>( inoutListProbeElement.size() ) / shaderslot::kLightProbeFloat4Count;
                GPULight                    element{};
                element._positionRadius = float4{ center._x, center._y, center._z, extent.getLength() * 0.5f };
                element._colorIntensity = float4{ layout._origin._x, layout._origin._y, layout._origin._z, 0.0f };
                element._directionType  = float4{ layout._spacing._x, layout._spacing._y, layout._spacing._z, static_cast<float32>( shaderslot::kLightTypeProbeVolume ) };
                element._params         = float4{ static_cast<float32>( layout._countX ), static_cast<float32>( layout._countY ), static_cast<float32>( layout._countZ ),
                                          static_cast<float32>( probeBase ) };
                for ( const SH9Color& probe : grid._listProbe )
                {
                    float4 arrElement[SH9Color::kGPUFloat4Count];
                    probe.makeGPUElement( arrElement );
                    inoutListProbeElement.insert( inoutListProbeElement.end(), arrElement, arrElement + SH9Color::kGPUFloat4Count );
                }
                return element;
            }
        };
        static_assert( shaderslot::kLightProbeFloat4Count == SH9Color::kGPUFloat4Count, "셰이더의 프로브 원소 수와 SH9Color 의 GPU 원소 수가 같아야 한다" );
    } // namespace

    void collectSceneLights( const Scene* pScene, vector<GPULight>& outList, vector<uint32>* pOutListShadowLightIndex, vector<float4>* pOutListProbeElement )
    {
        outList.clear();
        if ( pOutListProbeElement != nullptr )
            pOutListProbeElement->clear();
        if ( pOutListShadowLightIndex != nullptr )
            pOutListShadowLightIndex->clear();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;

        const ComponentRegistry& registry = pScene->getObjectManager()->getComponentRegistry();
        // 그림자 플래그는 그림자 행렬을 만드는 쪽(Engine, FrameRenderer)과 **같은 빛**에 붙어야 한다. 고르는 규칙은 씬의 한 함수다.
        const DirectionalLightComponent* pShadowLight = pScene->findShadowCastingDirectionalLight();

        // 종류 순서로 돈다. 방향광이 0 이라 앞에 온다(상한을 넘으면 뒤에서부터 잘린다). 원소의 필드는 빛이 스스로 쓴다
        // (`LightComponent::writeGPULight`). 여기는 종류를 모른다. 그림자 플래그만 씬이 고른 빛에 켠다.
        for ( uint32 lightType = 0; lightType < shaderslot::kLightTypeCount; ++lightType )
        {
            for ( const LightComponent* pLight : registry.getAll<LightComponent>( lightType ) )
            {
                if ( pLight == nullptr || pLight->isActive() == false )
                    continue;

                GPULight light{};
                pLight->writeGPULight( light );
                if ( pLight == pShadowLight )
                    light._params._x = 1.0f;
                // 그림자를 드리우는 점광과 스폿은 번호만 적는다. 아틀라스 슬롯(`_params.x`, `_params.w`)은 슬롯 수를 아는 쪽이 나눠 준다(ShadowSetupUtil).
                if ( pOutListShadowLightIndex != nullptr && GPULightBufferInternal::castsLocalShadow( *pLight ) )
                    pOutListShadowLightIndex->push_back( static_cast<uint32>( outList.size() ) );
                outList.push_back( light );
            }
        }

        // 라이트 프로브 볼륨은 3D 라이트 뒤에 붙는다(IndirectLighting.md P3). 쓸 수 있는 베이크가 없는 볼륨(굽지 않음 · 모양이 다름)은 넣지 않는다 —
        // 그곳은 볼륨이 없는 곳과 같다(이유는 볼륨이 읽을 때 남긴다).
        if ( pOutListProbeElement != nullptr )
        {
            for ( const LightProbeVolumeComponent* pVolume : registry.getAll<LightProbeVolumeComponent>() )
            {
                const LightProbeGrid* pGrid = ( pVolume != nullptr && pVolume->isActive() ) ? pVolume->findProbeData() : nullptr;
                if ( pGrid != nullptr )
                    outList.push_back( GPULightBufferInternal::makeProbeVolumeElement( *pGrid, *pOutListProbeElement ) );
            }
        }

        // 2D 그림자 가림막 세그먼트는 빛 **뒤에** 붙는다. 상한을 넘으면 뒤에서부터 잘리므로 빛보다 가림막이 먼저 빠진다(lighting2d.hlsli).
        for ( const ShadowCaster2DComponent* pCaster : registry.getAll<ShadowCaster2DComponent>() )
        {
            if ( pCaster != nullptr && pCaster->isActive() )
                pCaster->appendGPUShadowSegments( outList );
        }
    }

    void GPULightBuffer::populate( const vector<GPULight>& listLight, const vector<GPULight>& listShadowTileElement, const vector<float4>& listProbeElement )
    {
        uint32 count = static_cast<uint32>( listLight.size() );
        if ( count > shaderslot::kMaxFrameLight )
        {
            if ( _bWarnedOverflow == SW_FALSE )
            {
                _bWarnedOverflow = SW_TRUE;
                SW_LOG_WARNING( "라이트가 상한(%#)을 넘었습니다 — 앞에서부터 잘라 보냅니다.", shaderslot::kMaxFrameLight );
            }
            count = shaderslot::kMaxFrameLight;
        }
        _count = count;
        // 점광과 스폿 그림자 타일 원소는 (잘린) 라이트 뒤에 붙는다. 셰이더는 g_SwLightCount + 라이트의 params.w 로 찾는다. 그리드는 따로 버퍼(t12)다.
        _listUploadScratch.assign( listLight.begin(), listLight.begin() + count );
        _listUploadScratch.insert( _listUploadScratch.end(), listShadowTileElement.begin(), listShadowTileElement.end() );
        _listProbeScratch.assign( listProbeElement.begin(), listProbeElement.end() );
    }

    void GPULightBuffer::update( IRHIDevice* pDevice, const vector<float4x4>& listGridViewProj, LightGridProducer producer )
    {
        _listGridBase.assign( listGridViewProj.size(), invalid_index::kUint32 );
        _gpuGridViewCount  = 0;
        _gridProducer      = LightGridProducer::None;
        _probeElementCount = 0;
        if ( pDevice == nullptr || _count == 0 )
        {
            // 버퍼는 그대로 둔다. 라이트가 잠깐 0 이 되는 프레임마다 만들고 지우면 그것이 비용이다.
            // 셰이더는 g_SwLightCount 가 0 이면 라이트를 돌지 않는다(앰비언트만).
            return;
        }

        const GPULight*          pUpload    = _listUploadScratch.data();
        const uint32             totalCount = static_cast<uint32>( _listUploadScratch.size() );
        constexpr RHIBufferUsage kUsage     = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        const uint32             stride     = static_cast<uint32>( sizeof( GPULight ) );
        if ( _buffer.ensureCapacity( pDevice, stride, totalCount, kUsage, true, false, pUpload ) == false )
        {
            _count = 0;
            return;
        }
        _buffer.upload( pDevice, pUpload, totalCount * stride );

        // 라이트 프로브 SH9(t13). 볼륨 원소가 라이트 목록에 있으면 그 프로브들이 여기 있다 — 못 올리면 볼륨 원소가 빈 버퍼를 가리키므로 오류로 남긴다.
        if ( _listProbeScratch.empty() == false )
        {
            const uint32 probeElementCount = static_cast<uint32>( _listProbeScratch.size() );
            const uint32 probeStride       = static_cast<uint32>( sizeof( float4 ) );
            if ( _probeBuffer.ensureCapacity( pDevice, probeStride, probeElementCount, kUsage, true, false, _listProbeScratch.data() ) )
            {
                _probeBuffer.upload( pDevice, _listProbeScratch.data(), probeElementCount * probeStride );
                _probeElementCount = probeElementCount;
            }
            else
            {
                SW_LOG_ERROR( "Light probe buffer (%# elements) could not be allocated - probe volumes give no indirect light this frame", probeElementCount );
            }
        }

        if ( listGridViewProj.empty() )
            return;
        switch ( producer )
        {
            case LightGridProducer::None:
            {
                break;
            }
            case LightGridProducer::CPU:
            {
                updateCPUGrid( pDevice, listGridViewProj );
                break;
            }
            case LightGridProducer::GPU:
            {
                updateGPUGrid( pDevice, listGridViewProj );
                break;
            }
        }
    }

    void GPULightBuffer::updateCPUGrid( IRHIDevice* pDevice, const vector<float4x4>& listGridViewProj )
    {
        // 뷰마다 그리드 하나 — 셰이더는 PassCB g_SwLightGridBase(지금 뷰의 첫 단어)로 찾는다.
        _listGridWordScratch.clear();
        if ( _listGrid.size() < listGridViewProj.size() )
            _listGrid.resize( listGridViewProj.size() );
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( listGridViewProj.size() ); ++viewIndex )
        {
            LightGrid& grid = _listGrid[viewIndex];
            if ( grid.populate( listGridViewProj[viewIndex], _listUploadScratch.data(), _count ) == false )
            {
                reportGridFailure( "has no usable projection (near/far plane)", viewIndex );
                continue;
            }
            _listGridBase[viewIndex] = static_cast<uint32>( _listGridWordScratch.size() );
            _listGridWordScratch.insert( _listGridWordScratch.end(), grid.getWords().begin(), grid.getWords().end() );
        }
        if ( _listGridWordScratch.empty() )
            return;

        constexpr RHIBufferUsage kUsage    = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        const uint32             wordCount = static_cast<uint32>( _listGridWordScratch.size() );
        if ( _cpuGridBuffer.ensureCapacity( pDevice, static_cast<uint32>( sizeof( uint32 ) ), wordCount, kUsage, true, false, _listGridWordScratch.data() ) == false )
        {
            reportGridFailure( "could not allocate the CPU grid buffer", 0 );
            _listGridBase.assign( listGridViewProj.size(), invalid_index::kUint32 );
            return;
        }
        _cpuGridBuffer.upload( pDevice, _listGridWordScratch.data(), wordCount * static_cast<uint32>( sizeof( uint32 ) ) );
        _gridProducer = LightGridProducer::CPU;
    }

    void GPULightBuffer::updateGPUGrid( IRHIDevice* pDevice, const vector<float4x4>& listGridViewProj )
    {
        namespace grid = shaderslot::lightgrid;
        // 뷰 정보(bindingslots.hlsli 10 절): 행 넷 · (가까운 면 w, 조각 배율, 원근, 0) · asuint(라이트 수, 그리드 첫 단어, 0, 0). 그룹 번호가 곧 이 목록의 번호다.
        // 뷰-투영이 깨진 뷰는 목록에서 뺀다 — 그 뷰는 그리드 없이 그린다.
        _listGridViewScratch.clear();
        uint32 gridViewCount = 0;
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( listGridViewProj.size() ); ++viewIndex )
        {
            LightGridSlicing slicing{};
            const float4x4&  viewProj = listGridViewProj[viewIndex];
            if ( LightGrid::computeSlicing( viewProj, slicing ) == false )
            {
                reportGridFailure( "has no usable projection (near/far plane)", viewIndex );
                continue;
            }
            const uint32 gridBase = gridViewCount * grid::kGPUWordsPerView;
            const uint32 arrCount[4]{ _count, gridBase, 0u, 0u };
            float4       counts{};
            Memory::copy( &counts, arrCount, sizeof( counts ) );
            _listGridViewScratch.push_back( float4{ viewProj._11, viewProj._12, viewProj._13, viewProj._14 } );
            _listGridViewScratch.push_back( float4{ viewProj._21, viewProj._22, viewProj._23, viewProj._24 } );
            _listGridViewScratch.push_back( float4{ viewProj._31, viewProj._32, viewProj._33, viewProj._34 } );
            _listGridViewScratch.push_back( float4{ viewProj._41, viewProj._42, viewProj._43, viewProj._44 } );
            _listGridViewScratch.push_back( float4{ slicing._nearDepth, slicing._sliceScale, slicing._bPerspective == SW_TRUE ? 1.0f : 0.0f, 0.0f } );
            _listGridViewScratch.push_back( counts );
            _listGridBase[viewIndex] = gridBase;
            ++gridViewCount;
        }
        static_assert( grid::kGPUViewFloat4Count == 6, "뷰 정보의 float4 수가 위의 push_back 수와 같아야 한다" );
        if ( gridViewCount == 0 )
            return;

        constexpr RHIBufferUsage kViewUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        constexpr RHIBufferUsage kGridUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
        const uint32             viewCount  = static_cast<uint32>( _listGridViewScratch.size() );
        const bool               bViewReady = _gpuGridViewBuffer.ensureCapacity( pDevice, static_cast<uint32>( sizeof( float4 ) ), viewCount, kViewUsage, true, false, _listGridViewScratch.data() );
        const bool               bGridReady = _gpuGridBuffer.ensureCapacity( pDevice, static_cast<uint32>( sizeof( uint32 ) ), gridViewCount * grid::kGPUWordsPerView, kGridUsage, true, true, nullptr ) &&
                                _gpuGridBuffer._uav != kInvalidDescriptorIndex;
        if ( bViewReady == false || bGridReady == false )
        {
            reportGridFailure( "could not allocate the GPU grid buffers (UAV)", 0 );
            _listGridBase.assign( listGridViewProj.size(), invalid_index::kUint32 );
            return;
        }
        _gpuGridViewBuffer.upload( pDevice, _listGridViewScratch.data(), viewCount * static_cast<uint32>( sizeof( float4 ) ) );
        _gpuGridViewCount = gridViewCount;
        _gridProducer     = LightGridProducer::GPU;
    }

    void GPULightBuffer::reportGridFailure( const utf8* pReason, uint32 viewIndex )
    {
        if ( _bWarnedGrid == SW_TRUE )
            return;
        _bWarnedGrid = SW_TRUE;
        SW_LOG_ERROR( "Light grid for view %# %# - that view walks every light", viewIndex, pReason );
    }

    const RHIStructuredBufferSlot* GPULightBuffer::findProbeBuffer() const
    {
        const bool bReady = _probeElementCount > 0 && _probeBuffer._buffer != 0 && _probeBuffer._srv != kInvalidDescriptorIndex;
        return bReady ? &_probeBuffer : nullptr;
    }

    const RHIStructuredBufferSlot* GPULightBuffer::findGridBuffer() const
    {
        switch ( _gridProducer )
        {
            case LightGridProducer::None:
                return nullptr;
            case LightGridProducer::CPU:
                return &_cpuGridBuffer;
            case LightGridProducer::GPU:
                return &_gpuGridBuffer;
        }
        return nullptr;
    }

    void GPULightBuffer::release( IRHIDevice* pDevice )
    {
        _buffer.release( pDevice );
        _cpuGridBuffer.release( pDevice );
        _gpuGridBuffer.release( pDevice );
        _gpuGridViewBuffer.release( pDevice );
        _probeBuffer.release( pDevice );
        _probeElementCount = 0;
        _count             = 0;
        _gpuGridViewCount  = 0;
        _gridProducer      = LightGridProducer::None;
        _listGridBase.clear();
    }
} // namespace sw
