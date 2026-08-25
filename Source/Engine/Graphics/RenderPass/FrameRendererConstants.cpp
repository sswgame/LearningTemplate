#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RenderPass/FrameRenderer.h"
#include "Engine/Object/Component/CameraComponent.h"

namespace sw
{

	namespace
	{
		void mulMat4( const float32 a[16], const float32 b[16], float32 out[16] )
		{
			for ( int32 rowIndex = 0; rowIndex < 4; ++rowIndex )
			{
				for ( int32 colIndex = 0; colIndex < 4; ++colIndex )
				{
					out[colIndex * 4 + rowIndex] = a[0 * 4 + rowIndex] * b[colIndex * 4 + 0] + a[1 * 4 + rowIndex] * b[colIndex * 4 + 1] + a[2 * 4 + rowIndex] * b[colIndex * 4 + 2] + a[3 * 4 + rowIndex] * b[colIndex * 4 + 3];
				}
			}
		}

	} // namespace

	void FrameRenderer::updatePassConstants()
	{
		buildLightViewProj( _passConstants._lightViewProj );
		buildCascadeShadowMatrices( _passConstants._cascadeViewProj, _passConstants._cascadeSplits );

		CameraComponent* pCam{ nullptr };
		if ( _pScene != nullptr )
		{
			// Prefer game camera during Scene::render; App packet path sets matrices explicitly.
			pCam = _pScene->getActiveRenderCamera( false );
		}
		if ( pCam != nullptr )
			applyViewFromCamera( pCam );
		else
			buildViewProj( _passConstants._viewProj );

		_passConstants._outlineParams[1] = _transientWidth > 0 ? ( 1.0f / static_cast<float32>( _transientWidth ) ) : 0.001f;
		_passConstants._outlineParams[2] = _transientHeight > 0 ? ( 1.0f / static_cast<float32>( _transientHeight ) ) : 0.001f;
		_passConstants._flags			 = ( _pDevice != nullptr && _pDevice->supportsNativeBindlessSampling() ) ? 1u : 0u;
		if ( _pDevice != nullptr && _passCb != 0 )
			_pDevice->getResource()->updateConstantBuffer( _passCb, &_passConstants, sizeof( PassConstants ) );
	}

	void FrameRenderer::applyViewFromCamera( CameraComponent* pCamera )
	{
		if ( pCamera == nullptr )
			return;
		const float32  aspect = ( _transientHeight > 0 )
								  ? ( static_cast<float32>( _transientWidth ) / static_cast<float32>( _transientHeight ) )
								  : ( 16.0f / 9.0f );
		const float4x4 vp	  = pCamera->getViewProjectionMatrix( aspect );
		Memory::copy( _passConstants._viewProj, &vp._11, sizeof( _passConstants._viewProj ) );
	}

	void FrameRenderer::buildCascadeShadowMatrices( float32 outCascadeMats[4][16], float32 outSplits[4] ) const
	{
		constexpr float32 kLambda	= 0.75f;
		constexpr float32 kNear		= 0.1f;
		constexpr float32 kFar		= 150.0f;
		constexpr int32	  kCascades = 4;

		// Practical split scheme: Ci = lambda * (n * (f/n)^(i/m)) + (1-lambda) * (n + (i/m)*(f-n))
		for ( int32 cascadeIndex = 0; cascadeIndex < kCascades; ++cascadeIndex )
		{
			const float32 p			= static_cast<float32>( cascadeIndex + 1 ) / static_cast<float32>( kCascades );
			const float32 logSplit	= kNear * MathUtil::pow( kFar / kNear, p );
			const float32 uniSplit	= kNear + ( kFar - kNear ) * p;
			outSplits[cascadeIndex] = kLambda * logSplit + ( 1.0f - kLambda ) * uniSplit;
		}

		float32		  lx  = _passConstants._keyLightDirIntensity[0];
		float32		  ly  = _passConstants._keyLightDirIntensity[1];
		float32		  lz  = _passConstants._keyLightDirIntensity[2];
		const float32 len = MathUtil::sqrt( lx * lx + ly * ly + lz * lz );
		if ( len > 1e-4f )
		{
			lx /= len;
			ly /= len;
			lz /= len;
		}

		float32 upX = 0.0f, upY = 1.0f, upZ = 0.0f;
		if ( MathUtil::abs( ly ) > 0.95f )
		{
			upX = 0.0f;
			upY = 0.0f;
			upZ = 1.0f;
		}
		float32 sx = upY * lz - upZ * ly;
		float32 sy = upZ * lx - upX * lz;
		float32 sz = upX * ly - upY * lx;
		float32 sl = MathUtil::sqrt( sx * sx + sy * sy + sz * sz );
		if ( sl > 1e-4f )
		{
			sx /= sl;
			sy /= sl;
			sz /= sl;
		}
		const float32 ux = ly * sz - lz * sy;
		const float32 uy = lz * sx - lx * sz;
		const float32 uz = lx * sy - ly * sx;

		const float32 arrLightView[16] = {
			sx, ux, -lx, 0,
			sy, uy, -ly, 0,
			sz, uz, -lz, 0,
			0, 0, 2.0f, 1 };

		for ( int32 cascadeIndex = 0; cascadeIndex < kCascades; ++cascadeIndex )
		{
			const float32 extent = outSplits[cascadeIndex] * 0.6f + 2.0f;
			const float32 invExt = 1.0f / extent;

			const float32 arrOrthoCascade[16] = {
				invExt, 0, 0, 0,
				0, invExt, 0, 0,
				0, 0, 0.1f * invExt, 0,
				0, 0, 0.5f, 1.0f };

			mulMat4( arrLightView, arrOrthoCascade, outCascadeMats[cascadeIndex] );
		}
	}

	void FrameRenderer::buildLightViewProj( float32 outMat[16] ) const
	{
		// Orthographic projection looking along key light (column-major).
		constexpr float32 arrOrtho[16] = {
			0.9f, 0, 0, 0,
			0, 0.9f, 0, 0,
			0, 0, 0.25f, 0,
			0, 0, 0.5f, 1.0f };

		float32		  lx  = _passConstants._keyLightDirIntensity[0];
		float32		  ly  = _passConstants._keyLightDirIntensity[1];
		float32		  lz  = _passConstants._keyLightDirIntensity[2];
		const float32 len = MathUtil::sqrt( lx * lx + ly * ly + lz * lz );
		if ( len > 1e-4f )
		{
			lx /= len;
			ly /= len;
			lz /= len;
		}

		// Simple view: align -Z with light direction.
		float32 upX = 0.0f, upY = 1.0f, upZ = 0.0f;
		if ( MathUtil::abs( ly ) > 0.99f )
		{
			upY = 0.0f;
			upZ = 1.0f;
		}
		float32		  sx = upY * lz - upZ * ly;
		float32		  sy = upZ * lx - upX * lz;
		float32		  sz = upX * ly - upY * lx;
		const float32 sl = MathUtil::sqrt( sx * sx + sy * sy + sz * sz );
		if ( sl > 1e-4f )
		{
			sx /= sl;
			sy /= sl;
			sz /= sl;
		}
		const float32 ux = ly * sz - lz * sy;
		const float32 uy = lz * sx - lx * sz;
		const float32 uz = lx * sy - ly * sx;

		const float32 arrView[16] = {
			sx, ux, -lx, 0,
			sy, uy, -ly, 0,
			sz, uz, -lz, 0,
			0, 0, 2.0f, 1 };

		mulMat4( arrView, arrOrtho, outMat );
	}

	void FrameRenderer::buildViewProj( float32 outMat[16] ) const
	{
		// Fallback orbit camera (used when no CameraComponent is active).
		constexpr float32 eyeX = 2.15f;
		constexpr float32 eyeY = 1.55f;
		constexpr float32 eyeZ = 2.65f;

		float32 zx = eyeX;
		float32 zy = eyeY;
		float32 zz = eyeZ;
		float32 zl = MathUtil::sqrt( zx * zx + zy * zy + zz * zz );
		if ( zl > 1e-4f )
		{
			zx /= zl;
			zy /= zl;
			zz /= zl;
		}

		float32 upX = 0.0f, upY = 1.0f, upZ = 0.0f;
		float32 xx = upY * zz - upZ * zy;
		float32 xy = upZ * zx - upX * zz;
		float32 xz = upX * zy - upY * zx;
		float32 xl = MathUtil::sqrt( xx * xx + xy * xy + xz * xz );
		if ( xl > 1e-4f )
		{
			xx /= xl;
			xy /= xl;
			xz /= xl;
		}
		const float32 yx = zy * xz - zz * xy;
		const float32 yy = zz * xx - zx * xz;
		const float32 yz = zx * xy - zy * xx;

		const float32 arrView[16] = {
			xx, yx, zx, 0.0f,
			xy, yy, zy, 0.0f,
			xz, yz, zz, 0.0f,
			-( xx * eyeX + xy * eyeY + xz * eyeZ ),
			-( yx * eyeX + yy * eyeY + yz * eyeZ ),
			-( zx * eyeX + zy * eyeY + zz * eyeZ ),
			1.0f };

		const float32	  aspect = ( _transientHeight > 0 )
									 ? ( static_cast<float32>( _transientWidth ) / static_cast<float32>( _transientHeight ) )
									 : ( 16.0f / 9.0f );
		constexpr float32 fovY	 = 0.70f; // ~40 deg
		constexpr float32 nearZ	 = 0.1f;
		constexpr float32 farZ	 = 100.0f;
		const float32	  yScale = 1.0f / MathUtil::tan( fovY * 0.5f );
		const float32	  xScale = yScale / aspect;
		constexpr float32 q		 = farZ / ( farZ - nearZ );

		const float32 arrProj[16] = {
			xScale, 0.0f, 0.0f, 0.0f,
			0.0f, yScale, 0.0f, 0.0f,
			0.0f, 0.0f, q, 1.0f,
			0.0f, 0.0f, -nearZ * q, 0.0f };

		mulMat4( arrView, arrProj, outMat );
	}

	void FrameRenderer::setIdentityWorld()
	{
		Memory::copy( _passConstants._world, &float4x4::Identity._11, sizeof( _passConstants._world ) );
	}
} // namespace sw
