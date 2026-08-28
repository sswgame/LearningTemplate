#include "pch.h"

#include "GameFramework/Base/EffectBaseComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	EffectBaseComponent::EffectBaseComponent()
		: _duration{ 0.0f }
		, _currentTimer{ 0.0f }
		, _currentAlpha{ 0.0f }
	{
	}

	void EffectBaseComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::PostPhysics );

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "VFX"_tag );

		_currentTimer = 0.0f;
		_currentAlpha = 1.0f;
	}

	void EffectBaseComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	void EffectBaseComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );

		_currentTimer += deltaTime;
		if ( _duration > 0.0f )
		{
			_currentAlpha = 1.0f - ( _currentTimer / _duration );
			if ( _currentAlpha < 0.0f )
			{
				_currentAlpha	   = 0.0f;
				GameObject* pOwner = getOwner();
				if ( pOwner != nullptr )
					pOwner->markPendingKill();
			}
		}
	}

	float32 EffectBaseComponent::getDuration() const
	{
		return _duration;
	}

	float32 EffectBaseComponent::getCurrentTimer() const
	{
		return _currentTimer;
	}

	float32 EffectBaseComponent::getCurrentAlpha() const
	{
		return _currentAlpha;
	}

	void EffectBaseComponent::setCurrentAlpha( float32 alpha )
	{
		_currentAlpha = alpha;
	}
} // namespace sw
