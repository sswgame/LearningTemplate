#include "pch.h"

#include "GameFramework/Kits/Overworld/CameraControllerComponent.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
	CameraControllerComponent::CameraControllerComponent()
		: _targetPos{ 0.0f, 0.0f }
		, _currentPos{ 0.0f, 0.0f }
		, _followSpeed{ 0.0f }
		, _shakeIntensity{ 0.0f }
		, _shakeDuration{ 0.0f }
		, _shakeFrequency{ 0.0f }
	{
	}

	void CameraControllerComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::PrePhysics );
	}

	void CameraControllerComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	void CameraControllerComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );

		_currentPos = float2::lerp( _currentPos, _targetPos, MathUtil::saturate( _followSpeed * deltaTime ) );

		float2 shakeOffset{ 0.0f, 0.0f };
		if ( _shakeDuration > 0.0f )
		{
			_shakeDuration	   = MathUtil::max( _shakeDuration - deltaTime, 0.0f );
			const float32 freq = _shakeFrequency;
			shakeOffset._x	   = MathUtil::sin( _shakeDuration * freq ) * _shakeIntensity;
			shakeOffset._y	   = MathUtil::cos( _shakeDuration * ( freq * 1.3f ) ) * ( _shakeIntensity * 0.75f );
		}

		GameObject* pOwner = getOwner();
		if ( pOwner == nullptr )
			return;

		SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
		if ( pSceneComp == nullptr )
			return;

		const float3 pos = pSceneComp->getLocalPosition();
		pSceneComp->setLocalPosition( float3{ _currentPos + shakeOffset, pos._z } );
	}

	void CameraControllerComponent::shake( float32 intensity, float32 duration )
	{
		_shakeIntensity = intensity;
		_shakeDuration	= duration;
	}
} // namespace sw
