#include "pch.h"

#include "Games/Demo/UI/UIManagerComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	void UIManagerComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::PostUpdate );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "UI"_tag );
	}

	void UIManagerComponent::onEndPlay()
	{
	}

	void UIManagerComponent::onTick( float32 deltaTime )
	{
		(void)deltaTime;
	}

	void UIManagerComponent::setHudVisible( bool bVisible )
	{
		bHudVisible = bVisible;
	}
} // namespace sw
