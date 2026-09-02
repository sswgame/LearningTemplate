#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RenderPass/FrameRenderer.h"
#include "Engine/Object/Component/CameraComponent.h"

namespace sw
{
    void FrameRenderer::updatePassConstants( FramePassContext& ctx )
    {
        buildLightViewProj( ctx, ctx._passConstants._lightViewProj );
        buildCascadeShadowMatrices( ctx, ctx._passConstants._arrCascadeViewProj, ctx._passConstants._cascadeSplits );

        if ( _pScene != nullptr )
        {
            CameraComponent* pCam = _pScene->getActiveGameCamera();
            if ( pCam != nullptr )
                applyViewFromCamera( ctx, pCam );
            else
                buildViewProj( ctx._passConstants._viewProj );
        }

        ctx._passConstants._outlineParams._y = _transientWidth > 0 ? ( 1.0f / static_cast<float32>( _transientWidth ) ) : 0.001f;
        ctx._passConstants._outlineParams._z = _transientHeight > 0 ? ( 1.0f / static_cast<float32>( _transientHeight ) ) : 0.001f;
        ctx._passConstants._flags            = ( _pDevice != nullptr && _pDevice->supportsNativeBindlessSampling() ) ? 1u : 0u;
        if ( _pDevice != nullptr && ctx._passCb != 0 )
            _pDevice->getResource()->updateConstantBuffer( ctx._passCb, &ctx._passConstants, sizeof( PassConstants ) );
    }

    void FrameRenderer::applyViewFromCamera( FramePassContext& ctx, CameraComponent* pCamera )
    {
        if ( pCamera == nullptr )
            return;
        const float32 aspect         = ( _transientHeight > 0 )
                                         ? ( static_cast<float32>( _transientWidth ) / static_cast<float32>( _transientHeight ) )
                                         : ( 16.0f / 9.0f );
        ctx._passConstants._viewProj = pCamera->getViewProjectionMatrix( aspect );
    }

    void FrameRenderer::buildCascadeShadowMatrices( const FramePassContext& ctx, float4x4 outArrCascadeMat[4], float4& outSplit ) const
    {
        constexpr float32 kLambda   = 0.75f;
        constexpr float32 kNear     = 0.1f;
        constexpr float32 kFar      = 150.0f;
        constexpr int32   kCascades = 4;

        // Practical split scheme: Ci = lambda * (n * (f/n)^(i/m)) + (1-lambda) * (n + (i/m)*(f-n))
        float32 arrSplit[4]{};
        for ( int32 cascadeIndex = 0; cascadeIndex < kCascades; ++cascadeIndex )
        {
            const float32 p        = static_cast<float32>( cascadeIndex + 1 ) / static_cast<float32>( kCascades );
            const float32 logSplit = kNear * MathUtil::pow( kFar / kNear, p );
            const float32 uniSplit = kNear + ( kFar - kNear ) * p;
            arrSplit[cascadeIndex] = kLambda * logSplit + ( 1.0f - kLambda ) * uniSplit;
        }
        outSplit = float4{ arrSplit[0], arrSplit[1], arrSplit[2], arrSplit[3] };

        float3 lightDir = float3{ ctx._passConstants._keyLightDirIntensity._x, ctx._passConstants._keyLightDirIntensity._y, ctx._passConstants._keyLightDirIntensity._z }.normalize();
        if ( lightDir.getLengthSquared() < MathUtil::Epsilon )
            lightDir = float3{ 0.57735f, -0.57735f, 0.57735f };

        const float3 up     = MathUtil::abs( lightDir._y ) > 0.95f ? float3{ 0.0f, 0.0f, 1.0f } : float3{ 0.0f, 1.0f, 0.0f };
        const float3 side   = up.cross( lightDir ).normalize();
        const float3 realUp = lightDir.cross( side );

        const float4x4 lightView{
            side._x, realUp._x, -lightDir._x, 0.0f,
            side._y, realUp._y, -lightDir._y, 0.0f,
            side._z, realUp._z, -lightDir._z, 0.0f,
            0.0f, 0.0f, 2.0f, 1.0f };

        for ( int32 cascadeIndex = 0; cascadeIndex < kCascades; ++cascadeIndex )
        {
            const float32 extent = arrSplit[cascadeIndex] * 0.6f + 2.0f;
            const float32 invExt = 1.0f / extent;

            const float4x4 orthoCascade{
                invExt, 0.0f, 0.0f, 0.0f,
                0.0f, invExt, 0.0f, 0.0f,
                0.0f, 0.0f, 0.1f * invExt, 0.0f,
                0.0f, 0.0f, 0.5f, 1.0f };

            outArrCascadeMat[cascadeIndex] = lightView * orthoCascade;
        }
    }

    void FrameRenderer::buildLightViewProj( const FramePassContext& ctx, float4x4& outMat ) const
    {
        // Orthographic projection looking along key light (column-major).
        constexpr float4x4 ortho{
            0.9f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.9f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.25f, 0.0f,
            0.0f, 0.0f, 0.5f, 1.0f };

        float3 lightDir = float3{ ctx._passConstants._keyLightDirIntensity._x, ctx._passConstants._keyLightDirIntensity._y, ctx._passConstants._keyLightDirIntensity._z }.normalize();
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
        ctx._passConstants._world = float4x4::Identity;
    }
} // namespace sw
