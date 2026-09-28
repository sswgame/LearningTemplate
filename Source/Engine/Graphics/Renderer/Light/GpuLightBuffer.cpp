#include "pch.h"

#include "Engine/Graphics/Renderer/Light/GpuLightBuffer.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/PointLightComponent.h"
#include "Engine/Object/Component/3D/SpotLightComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/LightRegistry.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    SW_LOG_CALLER( "GpuLightBuffer" );

    void collectSceneLights( const Scene* pScene, vector<GpuLight>& outList )
    {
        outList.clear();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;

        const LightRegistry& registry = pScene->getObjectManager()->getLightRegistry();
        // 그림자 플래그는 그림자 행렬을 만드는 쪽(EngineLoop · FrameRenderer)과 **같은 빛**에 붙어야 한다. 고르는 규칙은 씬의 한 함수다.
        const DirectionalLightComponent* pShadowLight = pScene->findShadowCastingDirectionalLight();

        // 종류 순서로 돈다. 방향광이 0 이라 앞에 온다(상한을 넘으면 뒤에서부터 잘린다). 공통 필드(색 · 세기 · 종류)는 한 번 채우고,
        // 종류마다 다른 것만 분기한다 — 새 종류는 여기 분기 하나다.
        for ( uint32 lightType = 0; lightType < shaderslot::kLightTypeCount; ++lightType )
        {
            for ( const LightComponent* pLight : registry.getAll( lightType ) )
            {
                if ( pLight == nullptr || pLight->isActive() == false )
                    continue;

                const float3 color = pLight->getColor();
                GpuLight     light{};
                light._colorIntensity   = float4{ color._x, color._y, color._z, pLight->getIntensity() };
                light._directionType._w = static_cast<float32>( lightType );

                if ( lightType == shaderslot::kLightTypeDirectional )
                {
                    const DirectionalLightComponent* pDirectional = static_cast<const DirectionalLightComponent*>( pLight );
                    const float3                     direction    = pDirectional->getLightDirection();
                    light._directionType                          = float4{ direction._x, direction._y, direction._z, light._directionType._w };
                    if ( pDirectional == pShadowLight )
                        light._params._x = 1.0f;
                }
                else if ( lightType == shaderslot::kLightTypePoint )
                {
                    const PointLightComponent* pPoint   = static_cast<const PointLightComponent*>( pLight );
                    const float3               position = pPoint->getLightPosition();
                    light._positionRadius               = float4{ position._x, position._y, position._z, pPoint->getRadius() };
                }
                else if ( lightType == shaderslot::kLightTypeSpot )
                {
                    const SpotLightComponent* pSpot     = static_cast<const SpotLightComponent*>( pLight );
                    const float3              position  = pSpot->getLightPosition();
                    const float3              direction = pSpot->getLightDirection();
                    light._positionRadius               = float4{ position._x, position._y, position._z, pSpot->getRadius() };
                    light._directionType                = float4{ direction._x, direction._y, direction._z, light._directionType._w };
                    // 원뿔은 **코사인으로** 보낸다. 셰이더가 픽셀마다 acos 를 하지 않게 하려는 것이다.
                    light._params._y = MathUtil::cos( pSpot->getOuterConeAngle() );
                    light._params._z = MathUtil::cos( pSpot->getInnerConeAngle() );
                }
                outList.push_back( light );
            }
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
