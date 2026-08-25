#include "pch.h"

#include "GameFramework/Base/DontDestroyOnLoadComponent.h"

#include "Engine/Object/Component/EcsDataUtil.h"
#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	void DontDestroyOnLoadComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::PrePhysics );

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "DontDestroyOnLoad"_tag );
		ensurePersistData();
	}

	void DontDestroyOnLoadComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	Component::EcsDataView DontDestroyOnLoadComponent::ensureEcsData()
	{
		DontDestroyOnLoadData* pData = ensurePersistData();
		return { pData, DontDestroyOnLoadData::StaticType() };
	}

	Component::EcsDataView DontDestroyOnLoadComponent::getEcsData() const
	{
		return { ( getPersistData() ), DontDestroyOnLoadData::StaticType() };
	}

	DontDestroyOnLoadData* DontDestroyOnLoadComponent::getPersistData() const
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			return pOwner->getComponent<DontDestroyOnLoadData>().get();
		return nullptr;
	}

	DontDestroyOnLoadData* DontDestroyOnLoadComponent::ensurePersistData()
	{
		return sw::ensureEcsData<DontDestroyOnLoadData>( getOwner(), getTypeInfo() );
	}
} // namespace sw
