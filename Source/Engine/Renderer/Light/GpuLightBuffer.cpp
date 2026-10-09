#include "pch.h"

#include "Engine/Renderer/Light/GpuLightBuffer.h"

#include "Core/Log/Logger.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/Component/2D/ShadowCaster2DComponent.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    SW_LOG_CALLER( "GpuLightBuffer" );

    void collectSceneLights( const Scene* pScene, vector<GpuLight>& outList )
    {
        outList.clear();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;

        const ComponentRegistry& registry = pScene->getObjectManager()->getComponentRegistry();
        // 그림자 플래그는 그림자 행렬을 만드는 쪽(EngineLoop · FrameRenderer)과 **같은 빛**에 붙어야 한다. 고르는 규칙은 씬의 한 함수다.
        const DirectionalLightComponent* pShadowLight = pScene->findShadowCastingDirectionalLight();

        // 종류 순서로 돈다. 방향광이 0 이라 앞에 온다(상한을 넘으면 뒤에서부터 잘린다). 원소의 칸은 빛이 스스로 쓴다
        // (`LightComponent::writeGpuLight`) — 여기는 종류를 모른다. 그림자 플래그만 씬이 고른 빛에 켠다.
        for ( uint32 lightType = 0; lightType < shaderslot::kLightTypeCount; ++lightType )
        {
            for ( const LightComponent* pLight : registry.getAll<LightComponent>( lightType ) )
            {
                if ( pLight == nullptr || pLight->isActive() == false )
                    continue;

                GpuLight light{};
                pLight->writeGpuLight( light );
                if ( pLight == pShadowLight )
                    light._params._x = 1.0f;
                outList.push_back( light );
            }
        }

        // 2D 그림자 가림막 토막은 빛 **뒤에** 붙는다 — 상한을 넘으면 뒤에서부터 잘리므로 빛보다 가림막이 먼저 빠진다(lighting2d.hlsli).
        for ( const ShadowCaster2DComponent* pCaster : registry.getAll<ShadowCaster2DComponent>() )
        {
            if ( pCaster != nullptr && pCaster->isActive() )
                pCaster->appendGpuShadowSegments( outList );
        }
    }

    void GpuLightBuffer::update( IRHIDevice* pDevice, const vector<GpuLight>& listLight )
    {
        if ( pDevice == nullptr )
            return;

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
        if ( count == 0 )
        {
            // 버퍼는 그대로 둔다. 라이트가 잠깐 0 이 되는 프레임마다 만들고 지우면 그것이 비용이다.
            // 셰이더는 g_SwLightCount 가 0 이면 PassCB 키라이트로 폴백한다.
            return;
        }

        constexpr RHIBufferUsage kUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        const uint32             stride = static_cast<uint32>( sizeof( GpuLight ) );
        if ( _buffer.ensureCapacity( pDevice, stride, count, kUsage, true, false, listLight.data() ) == false )
        {
            _count = 0;
            return;
        }
        _buffer.upload( pDevice, listLight.data(), count * stride );
    }

    void GpuLightBuffer::release( IRHIDevice* pDevice )
    {
        _buffer.release( pDevice );
        _count = 0;
    }
} // namespace sw
