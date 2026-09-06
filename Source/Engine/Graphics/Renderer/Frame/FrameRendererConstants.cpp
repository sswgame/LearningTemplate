#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Object/Component/CameraComponent.h"

namespace sw
{
    namespace
    {
        constexpr float4 kDefaultKeyLightDirIntensity{ -0.35f, -0.85f, -0.25f, 1.35f };
        constexpr float4 kDefaultKeyLightColor{ 1.0f, 0.82f, 0.62f, 0.28f };
        constexpr float4 kDefaultShadowParams{ 0.02f, 0.45f, 0.0f, 0.0f };
        constexpr float4 kDefaultBloomParams{ 0.55f, 0.65f, 0.25f, 0.0f };
        constexpr float4 kDefaultOutlineColor{ 0.08f, 0.05f, 0.12f, 0.85f };

        /// @brief 그림자용 라이트 카메라를 원점에서 얼마나 떨어뜨릴지. 직교 깊이 범위도 이 값을 쓴다.
        constexpr float32 kLightDistance = 2.0f;
        /// @brief 라이트 직교 투영이 담는 가로/세로 범위 (기존 스케일 0.9 = 2/2.222 와 같다).
        constexpr float32 kLightOrthoExtent = 2.0f / 0.9f;

        /// @brief 폴백 궤도 카메라 파라미터 — 약 40도 수직 화각.
        constexpr float32 kFallbackFovY  = 0.70f;
        constexpr float32 kFallbackNearZ = 0.1f;
        constexpr float32 kFallbackFarZ  = 100.0f;
    } // namespace

    void FrameRenderer::updatePassConstants( FramePassContext& ctx )
    {
        // **프레임당 한 번** 프레임 시드(_frameCtx)에만 채운다. 패스 컨텍스트는 이 시드를 복사해
        // 가므로(onGraphPassExecute) 패스마다 다시 계산할 필요가 없고, 드로우마다는 더더욱 없다.
        // 예전엔 commitBindlessTextureBindings 가 드로우마다 이걸 불렀다.
        //
        // 뷰/라이트 행렬은 씬이 없으면(렌더 스레드 패킷 경로) 폴백으로 세운다 — 패킷이 자기
        // 뷰 행렬을 갖고 있으면 executePacket 이 그 위에 덮어쓴다.
        float4x4 lightViewProj{};
        buildLightViewProj( ctx, lightViewProj );
        ctx._passValues.setMatrix( passConstantNames()._lightViewProj, lightViewProj );

        CameraComponent* pCam = ( _pScene != nullptr ) ? _pScene->getActiveGameCamera() : nullptr;
        if ( pCam != nullptr )
        {
            applyViewFromCamera( ctx, pCam );
        }
        else
        {
            float4x4 viewProj = float4x4::Identity;
            buildViewProj( viewProj );
            ctx._passValues.setMatrix( passConstantNames()._viewProj, viewProj );
        }
        ctx._passValues.setMatrix( passConstantNames()._world, ctx._world );

        const float32 outlineY = _transientWidth > 0 ? ( 1.0f / static_cast<float32>( _transientWidth ) ) : 0.001f;
        const float32 outlineZ = _transientHeight > 0 ? ( 1.0f / static_cast<float32>( _transientHeight ) ) : 0.001f;

        ctx._passValues.setFloat4( passConstantNames()._keyLightDirIntensity, kDefaultKeyLightDirIntensity );
        ctx._passValues.setFloat4( passConstantNames()._keyLightColor, kDefaultKeyLightColor );
        ctx._passValues.setFloat4( passConstantNames()._shadowParams, kDefaultShadowParams );
        ctx._passValues.setFloat4( passConstantNames()._bloomParams, kDefaultBloomParams );
        ctx._passValues.setFloat4( passConstantNames()._outlineColor, kDefaultOutlineColor );
        ctx._passValues.setFloat4( passConstantNames()._outlineParams, float4{ 0.02f, outlineY, outlineZ, 0.0f } );
        ctx._passValues.setUint( passConstantNames()._flags,
                                 ( _pDevice != nullptr && _pDevice->supportsNativeBindlessSampling() ) ? 1u : 0u );
    }

    void FrameRenderer::applyViewFromCamera( FramePassContext& ctx, CameraComponent* pCamera )
    {
        if ( pCamera == nullptr )
            return;
        const float32 aspect = ( _transientHeight > 0 )
                                 ? ( static_cast<float32>( _transientWidth ) / static_cast<float32>( _transientHeight ) )
                                 : ( 16.0f / 9.0f );
        ctx._passValues.setMatrix( passConstantNames()._viewProj, pCamera->getViewProjectionMatrix( aspect ) );
    }

    void FrameRenderer::buildLightViewProj( const FramePassContext& ctx, float4x4& outMat ) const
    {
        (void)ctx;
        float3 lightDir = float3{ kDefaultKeyLightDirIntensity._x, kDefaultKeyLightDirIntensity._y, kDefaultKeyLightDirIntensity._z }.normalize();
        if ( lightDir.getLengthSquared() < MathUtil::Epsilon )
            lightDir = float3{ 0.57735f, -0.57735f, 0.57735f };

        // 라이트를 원점 위(빛이 오는 쪽)에 두고 빛 방향을 따라 원점을 내려다본다. up 이 라이트와
        // 거의 나란하면 side 축이 사라지므로 다른 축으로 갈아탄다.
        const float3 up  = MathUtil::abs( lightDir._y ) > 0.99f ? float3::Forward : float3::Up;
        const float3 eye = lightDir * -kLightDistance;

        // 예전엔 view/ortho 성분을 직접 써 넣었다. createLookAt/createOrthographic 과 같은 행렬이지만
        // 손으로 쓰면 어떤 규약(좌수, 행벡터)인지 읽어서 알아내야 하고, CameraComponent 가 쓰는 규약과
        // 어긋나도 드러나지 않는다.
        outMat = float4x4::createLookAt( eye, float3::Zero, up ) * float4x4::createOrthographic( kLightOrthoExtent, kLightOrthoExtent, -kLightDistance, kLightDistance );
    }

    void FrameRenderer::buildViewProj( float4x4& outMat ) const
    {
        // CameraComponent 가 없을 때만 쓰는 폴백 궤도 카메라 — 원점을 바라본다.
        // 예전엔 view 행렬을 직접 채웠는데 z축을 eye.normalize() 로 잡고 있었다. 그건 원점에서
        // eye 로 향하는 방향이라 원점을 등지고 보는 셈이고, 이 엔진이 쓰는 좌수 투영
        // (createPerspectiveFieldOfView, w' = z_view)에서는 원점이 뷰 z = -|eye| 로 카메라 뒤에
        // 떨어져 아무것도 그려지지 않는다. CameraComponent::getViewMatrix 와 같은 createLookAt 으로
        // 맞춘다.
        constexpr float3 eye{ 2.15f, 1.55f, 2.65f };

        const float32 aspect = ( _transientHeight > 0 )
                                 ? ( static_cast<float32>( _transientWidth ) / static_cast<float32>( _transientHeight ) )
                                 : ( 16.0f / 9.0f );

        outMat = float4x4::createLookAt( eye, float3::Zero, float3::Up ) * float4x4::createPerspectiveFieldOfView( kFallbackFovY, aspect, kFallbackNearZ, kFallbackFarZ );
    }

    void FrameRenderer::setIdentityWorld( FramePassContext& ctx )
    {
        ctx._world = float4x4::Identity;
    }
} // namespace sw
