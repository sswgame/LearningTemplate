#include "pch.h"

#include "GameFramework/Base/DontDestroyOnLoadComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	DontDestroyOnLoadComponent::DontDestroyOnLoadComponent()
		: _bPersistent{ true }
		, _persistentTag{ "Persistent" }
	{
	}

	void DontDestroyOnLoadComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::PrePhysics );

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "DontDestroyOnLoad"_tag );
	}

	void DontDestroyOnLoadComponent::onEndPlay()
	{
		Component::onEndPlay();
	}
} // namespace sw
