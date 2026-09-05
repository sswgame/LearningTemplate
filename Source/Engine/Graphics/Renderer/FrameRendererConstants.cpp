#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/FrameRenderer.h"
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
    } // namespace

    void FrameRenderer::updatePassConstants( FramePassContext& ctx )
    {
        // 뷰/라이트 행렬은 씬이 있을 때만 재계산한다. 렌더 스레드 패킷 경로(_pScene == null)는
        // executePacket 에서 이미 시드했다.
        if ( _pScene != nullptr )
        {
            float4x4 lightViewProj{};
            buildLightViewProj( ctx, lightViewProj );
            ctx._passValues.setMatrix( hashed_string( "g_LightViewProj" ), lightViewProj );

            CameraComponent* pCam = _pScene->getActiveGameCamera();
            if ( pCam != nullptr )
            {
                applyViewFromCamera( ctx, pCam );
            }
            else
            {
                float4x4 viewProj = float4x4::Identity;
                buildViewProj( viewProj );
                ctx._passValues.setMatrix( hashed_string( "g_ViewProj" ), viewProj );
            }
        }
        ctx._passValues.setMatrix( hashed_string( "g_World" ), ctx._world );

        const float32 outlineY = _transientWidth > 0 ? ( 1.0f / static_cast<float32>( _transientWidth ) ) : 0.001f;
        const float32 outlineZ = _transientHeight > 0 ? ( 1.0f / static_cast<float32>( _transientHeight ) ) : 0.001f;

        ctx._passValues.setFloat4( hashed_string( "g_KeyLightDirIntensity" ), kDefaultKeyLightDirIntensity );
        ctx._passValues.setFloat4( hashed_string( "g_KeyLightColor" ), kDefaultKeyLightColor );
        ctx._passValues.setFloat4( hashed_string( "g_ShadowParams" ), kDefaultShadowParams );
        ctx._passValues.setFloat4( hashed_string( "g_BloomParams" ), kDefaultBloomParams );
        ctx._passValues.setFloat4( hashed_string( "g_OutlineColor" ), kDefaultOutlineColor );
        ctx._passValues.setFloat4( hashed_string( "g_OutlineParams" ), float4{ 0.02f, outlineY, outlineZ, 0.0f } );
        ctx._passValues.setUint( hashed_string( "g_Flags" ),
                                 ( _pDevice != nullptr && _pDevice->supportsNativeBindlessSampling() ) ? 1u : 0u );
    }

    void FrameRenderer::applyViewFromCamera( FramePassContext& ctx, CameraComponent* pCamera )
    {
        if ( pCamera == nullptr )
            return;
        const float32 aspect = ( _transientHeight > 0 )
                                 ? ( static_cast<float32>( _transientWidth ) / static_cast<float32>( _transientHeight ) )
                                 : ( 16.0f / 9.0f );
        ctx._passValues.setMatrix( hashed_string( "g_ViewProj" ), pCamera->getViewProjectionMatrix( aspect ) );
    }

    void FrameRenderer::buildLightViewProj( const FramePassContext& ctx, float4x4& outMat ) const
    {
        // Orthographic projection looking along key light (column-major).
        constexpr float4x4 ortho{
            0.9f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.9f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.25f, 0.0f,
            0.0f, 0.0f, 0.5f, 1.0f };

        (void)ctx;
        float3 lightDir = float3{ kDefaultKeyLightDirIntensity._x, kDefaultKeyLightDirIntensity._y, kDefaultKeyLightDirIntensity._z }.normalize();
        if ( lightDir.getLengthSquared() < MathUtil::Epsilon )
            lightDir = float3{ 0.57735f, -0.57735f, 0.57735f };

        // Simple view: align -Z with light direction.
        const float3 up     = MathUtil::abs( lightDir._y ) > 0.99f ? float3{ 0.0f, 0.0f, 1.0f } : float3{ 0.0f, 1.0f, 0.0f };
        const float3 side   = up.cross( lightDir ).normalize();
        const float3 realUp = lightDir.cross( side );

        const float4x4 view{
            side._x, realUp._x, -lightDir._x, 0.0f,
            side._y, realUp._y, -lightDir._y, 0.0f,
            side._z, realUp._z, -lightDir._z, 0.0f,
            0.0f, 0.0f, 2.0f, 1.0f };

        outMat = view * ortho;
    }

    void FrameRenderer::buildViewProj( float4x4& outMat ) const
    {
        // Fallback orbit camera (used when no CameraComponent is active).
        constexpr float3 eye{ 2.15f, 1.55f, 2.65f };
        const float3     zAxis = eye.normalize();
        constexpr float3 up{ 0.0f, 1.0f, 0.0f };
        const float3     xAxis = up.cross( zAxis ).normalize();
        const float3     yAxis = zAxis.cross( xAxis );

        const float4x4 view{
            xAxis._x, yAxis._x, zAxis._x, 0.0f,
            xAxis._y, yAxis._y, zAxis._y, 0.0f,
            xAxis._z, yAxis._z, zAxis._z, 0.0f,
            -xAxis.dot( eye ),
            -yAxis.dot( eye ),
            -zAxis.dot( eye ),
            1.0f };

        const float32     aspect = ( _transientHeight > 0 )
                                     ? ( static_cast<float32>( _transientWidth ) / static_cast<float32>( _transientHeight ) )
                                     : ( 16.0f / 9.0f );
        constexpr float32 fovY   = 0.70f; // ~40 deg
        constexpr float32 nearZ  = 0.1f;
        constexpr float32 farZ   = 100.0f;

        const float4x4 proj = float4x4::createPerspectiveFieldOfView( fovY, aspect, nearZ, farZ );

        outMat = view * proj;
    }

    void FrameRenderer::setIdentityWorld( FramePassContext& ctx )
    {
        ctx._world = float4x4::Identity;
    }
} // namespace sw
