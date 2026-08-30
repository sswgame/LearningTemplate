#include "pch.h"

#include "Engine/Object/Component/CameraComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

namespace sw
{
	CameraComponent::CameraComponent()
		: _fovY{ 0.70f }
		, _nearZ{ 0.1f }
		, _farZ{ 100.0f }
		, _orthoHeight{ 10.0f }
		, _priority{ 0 }
		, _role{ CameraRole::Game }
		, _bOrthographic{ false }
	{
	}

	void CameraComponent::onBeginPlay()
	{
		SceneComponent::onBeginPlay();
	}

	void CameraComponent::lookAt( const float3& target, const float3& up )
	{
		const float3  eye	  = getWorldPosition();
		float3		  forward = ( target - eye );
		const float32 lenSq	  = forward.getLengthSquared();
		if ( lenSq <= MathUtil::Epsilon )
			return;
		forward.normalize();

		const float32 yaw	= MathUtil::atan2( forward._x, forward._z );
		const float32 pitch = -MathUtil::asin( MathUtil::clamp( forward._y, -1.0f, 1.0f ) );
		(void)up;
		setLocalRotation( float3( pitch, yaw, 0.0f ) );
	}

	float4x4 CameraComponent::getViewMatrix() const
	{
		const float3 eye = getWorldPosition();
		const float3 rot = getLocalRotation();

		const float4x4 rotMat  = float4x4::createFromYawPitchRoll( rot._y, rot._x, rot._z );
		const float3   forward = float3::transformNormal( float3( 0.0f, 0.0f, 1.0f ), rotMat );
		const float3   target  = eye + forward;
		return float4x4::createLookAt( eye, target, float3( 0.0f, 1.0f, 0.0f ) );
	}

	float4x4 CameraComponent::getProjectionMatrix( float32 aspectRatio ) const
	{
		const float32 aspect = aspectRatio > 1e-4f ? aspectRatio : ( 16.0f / 9.0f );
		if ( _bOrthographic )
		{
			const float32 height = _orthoHeight > 1e-4f ? _orthoHeight : 10.0f;
			const float32 width	 = height * aspect;
			return float4x4::createOrthographic( width, height, _nearZ, _farZ );
		}
		return float4x4::createPerspectiveFieldOfView( _fovY, aspect, _nearZ, _farZ );
	}

	float4x4 CameraComponent::getViewProjectionMatrix( float32 aspectRatio ) const
	{
		return getViewMatrix() * getProjectionMatrix( aspectRatio );
	}

} // namespace sw
