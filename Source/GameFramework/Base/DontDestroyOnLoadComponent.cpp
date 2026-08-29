#include "pch.h"

#include "GameFramework/Base/DontDestroyOnLoadComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	DontDestroyOnLoadComponent::DontDestroyOnLoadComponent()
		: _persistentTag{ "Persistent" }
		, _bPersistent{ true }
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
