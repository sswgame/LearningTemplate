#include "pch.h"

#include "Engine/Graphics/Renderer/Light/GpuLightBuffer.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/PointLightComponent.h"
#include "Engine/Object/Component/3D/SpotLightComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/LightRegistry.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    SW_LOG_CALLER( "GpuLightBuffer" );

    namespace
    {
        /** @brief 컴포넌트와 그 소유 오브젝트가 모두 활성인지 확인합니다. */
        bool isLightActive( const SceneComponent* pLight )
        {
            if ( pLight == nullptr || pLight->isActive() == false )
                return false;
            const GameObject* pOwner = pLight->getOwner();
            return pOwner != nullptr && pOwner->isActiveInHierarchy();
        }
    } // namespace

    void collectSceneLights( const Scene* pScene, vector<GpuLight>& outList )
    {
        outList.clear();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;

        const LightRegistry& registry = pScene->getObjectManager()->getLightRegistry();

        // 방향광 먼저. 그림자를 드리우는 **첫** 빛만 그림자 플래그를 받는다(그림자 맵이 하나다).
        bool bShadowTaken = false;
        for ( DirectionalLightComponent* pLight : registry.getAllDirectional() )
        {
            if ( isLightActive( pLight ) == false )
                continue;

            const float3 direction = pLight->getLightDirection();
            const float3 color     = pLight->getColor();

            GpuLight light{};
            light._colorIntensity = float4{ color._x, color._y, color._z, pLight->getIntensity() };
            light._directionType  = float4{ direction._x, direction._y, direction._z,
                                           static_cast<float32>( shaderslot::kLightTypeDirectional ) };
            if ( bShadowTaken == false && pLight->castsShadow() )
            {
                light._params._x = 1.0f;
                bShadowTaken     = true;
            }
            outList.push_back( light );
        }

        for ( PointLightComponent* pLight : registry.getAllPoint() )
        {
            if ( isLightActive( pLight ) == false )
                continue;

            const float3 position = pLight->getLightPosition();
            const float3 color    = pLight->getColor();

            GpuLight light{};
            light._positionRadius   = float4{ position._x, position._y, position._z, pLight->getRadius() };
            light._colorIntensity   = float4{ color._x, color._y, color._z, pLight->getIntensity() };
            light._directionType._w = static_cast<float32>( shaderslot::kLightTypePoint );
            outList.push_back( light );
        }

        for ( SpotLightComponent* pLight : registry.getAllSpot() )
        {
            if ( isLightActive( pLight ) == false )
                continue;

            const float3 position  = pLight->getLightPosition();
            const float3 direction = pLight->getLightDirection();
            const float3 color     = pLight->getColor();

            GpuLight light{};
            light._positionRadius = float4{ position._x, position._y, position._z, pLight->getRadius() };
            light._colorIntensity = float4{ color._x, color._y, color._z, pLight->getIntensity() };
            light._directionType  = float4{ direction._x, direction._y, direction._z,
                                           static_cast<float32>( shaderslot::kLightTypeSpot ) };
            // 원뿔은 **코사인으로** 보낸다. 셰이더가 픽셀마다 acos 를 하지 않게 하려는 것이다.
            light._params._y = MathUtil::cos( pLight->getOuterConeAngle() );
            light._params._z = MathUtil::cos( pLight->getInnerConeAngle() );
            outList.push_back( light );
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
