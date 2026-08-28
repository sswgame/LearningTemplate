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

		_currentPos._x = MathUtil::lerp( _currentPos._x, _targetPos._x, MathUtil::clamp( _followSpeed * deltaTime, 0.0f, 1.0f ) );
		_currentPos._y = MathUtil::lerp( _currentPos._y, _targetPos._y, MathUtil::clamp( _followSpeed * deltaTime, 0.0f, 1.0f ) );

		float2 shakeOffset{ 0.0f, 0.0f };
		if ( _shakeDuration > 0.0f )
		{
			_shakeDuration -= deltaTime;
			if ( _shakeDuration < 0.0f )
				_shakeDuration = 0.0f;
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

		float3 pos = pSceneComp->getLocalPosition();
		pos._x	   = _currentPos._x + shakeOffset._x;
		pos._y	   = _currentPos._y + shakeOffset._y;
		pSceneComp->setLocalPosition( pos );
	}

	void CameraControllerComponent::shake( float32 intensity, float32 duration )
	{
		_shakeIntensity = intensity;
		_shakeDuration	= duration;
	}
} // namespace sw
