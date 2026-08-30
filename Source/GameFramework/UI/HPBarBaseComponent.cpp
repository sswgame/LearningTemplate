#include "pch.h"

#include "GameFramework/UI/HPBarBaseComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	HPBarBaseComponent::HPBarBaseComponent()
		: _hpRatio{ 0.0f }
		, _remainRatio{ 0.0f }
		, _targetRatio{ 0.0f }
		, _lerpSpeed{ 0.0f }
		, _offsetPos{ 0.0f, 0.0f }
		, _bVisible{ false }
	{
	}

	void HPBarBaseComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::PostUpdate );

		_remainRatio = _hpRatio;
		_targetRatio = _hpRatio;

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "UI"_tag );
	}

	void HPBarBaseComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	void HPBarBaseComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );

		_remainRatio = MathUtil::lerp( _remainRatio, _targetRatio, MathUtil::saturate( _lerpSpeed * deltaTime ) );
	}

	void HPBarBaseComponent::setTargetRatio( float32 ratio )
	{
		_targetRatio = ratio;
	}
} // namespace sw
