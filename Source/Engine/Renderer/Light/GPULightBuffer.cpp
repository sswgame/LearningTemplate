#include "pch.h"

#include "Engine/Renderer/Light/GPULightBuffer.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/Component/2D/ShadowCaster2DComponent.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
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
        /** @brief 그림자를 켠 점광과 스폿이면 true 입니다(방향광은 씬이 하나 고릅니다). */
        bool castsLocalShadow( const LightComponent& light )
        {
            if ( light.getLightType() == shaderslot::kLightTypePoint )
                return static_cast<const PointLightComponent&>( light ).castsShadow();
            if ( light.getLightType() == shaderslot::kLightTypeSpot )
                return static_cast<const SpotLightComponent&>( light ).castsShadow();
            return false;
        }
    } // namespace

    void collectSceneLights( const Scene* pScene, vector<GPULight>& outList, vector<uint32>* pOutListShadowLightIndex )
    {
        outList.clear();
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
                if ( pOutListShadowLightIndex != nullptr && castsLocalShadow( *pLight ) )
                    pOutListShadowLightIndex->push_back( static_cast<uint32>( outList.size() ) );
                outList.push_back( light );
            }
        }

        // 2D 그림자 가림막 세그먼트는 빛 **뒤에** 붙는다. 상한을 넘으면 뒤에서부터 잘리므로 빛보다 가림막이 먼저 빠진다(lighting2d.hlsli).
        for ( const ShadowCaster2DComponent* pCaster : registry.getAll<ShadowCaster2DComponent>() )
        {
            if ( pCaster != nullptr && pCaster->isActive() )
                pCaster->appendGPUShadowSegments( outList );
        }
    }

    void GPULightBuffer::populate( const vector<GPULight>& listLight, const vector<GPULight>& listShadowTileElement )
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
        // 점광과 스폿 그림자 타일 원소는 (잘린) 라이트 뒤에 붙는다. 셰이더는 g_SwLightCount + 라이트의 params.w 로 찾는다. 그리드는 그 뒤에 붙는다(update).
        _listUploadScratch.assign( listLight.begin(), listLight.begin() + count );
        _listUploadScratch.insert( _listUploadScratch.end(), listShadowTileElement.begin(), listShadowTileElement.end() );
        _populatedCount = static_cast<uint32>( _listUploadScratch.size() );
    }

    void GPULightBuffer::update( IRHIDevice* pDevice, const vector<float4x4>& listGridViewProj )
    {
        _listGridBase.assign( listGridViewProj.size(), invalid_index::kUint32 );
        if ( pDevice == nullptr || _count == 0 )
        {
            // 버퍼는 그대로 둔다. 라이트가 잠깐 0 이 되는 프레임마다 만들고 지우면 그것이 비용이다.
            // 셰이더는 g_SwLightCount 가 0 이면 라이트를 돌지 않는다(앰비언트만).
            return;
        }

        // 뷰마다 그리드 하나 — 셰이더는 PassCB g_SwLightGridBase(지금 뷰의 첫 원소)로 찾는다.
        _listUploadScratch.resize( _populatedCount );
        if ( _listGrid.size() < listGridViewProj.size() )
            _listGrid.resize( listGridViewProj.size() );
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( listGridViewProj.size() ); ++viewIndex )
        {
            LightGrid& grid = _listGrid[viewIndex];
            if ( grid.populate( listGridViewProj[viewIndex], _listUploadScratch.data(), _count ) == false )
            {
                if ( _bWarnedGrid == SW_FALSE )
                {
                    _bWarnedGrid = SW_TRUE;
                    SW_LOG_ERROR( "View %# has no usable projection for the light grid (near/far plane) - it walks every light", viewIndex );
                }
                continue;
            }
            const uint32 first = static_cast<uint32>( _listUploadScratch.size() );
            _listUploadScratch.resize( first + grid.getElementCount() );
            Memory::copy( _listUploadScratch.data() + first, grid.getWords().data(), grid.getWords().size() * sizeof( uint32 ) );
            _listGridBase[viewIndex] = first;
        }

        const GPULight*          pUpload    = _listUploadScratch.data();
        const uint32             totalCount = static_cast<uint32>( _listUploadScratch.size() );
        constexpr RHIBufferUsage kUsage     = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        const uint32             stride     = static_cast<uint32>( sizeof( GPULight ) );
        if ( _buffer.ensureCapacity( pDevice, stride, totalCount, kUsage, true, false, pUpload ) == false )
        {
            _count = 0;
            _listGridBase.assign( listGridViewProj.size(), invalid_index::kUint32 );
            return;
        }
        _buffer.upload( pDevice, pUpload, totalCount * stride );
    }

    void GPULightBuffer::release( IRHIDevice* pDevice )
    {
        _buffer.release( pDevice );
        _count = 0;
    }
} // namespace sw
