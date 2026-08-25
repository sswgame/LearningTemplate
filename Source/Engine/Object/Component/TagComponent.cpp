#include "pch.h"

#include "Engine/Object/Component/TagComponent.h"

namespace sw
{
	void TagComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
		{
			if ( pGameObject->getComponent<TagData>() == nullptr )
				pGameObject->addComponent<TagData>();
		}
	}

	TagData* TagComponent::getTagData() const
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			return pGameObject->getComponent<TagData>().get();
		return nullptr;
	}

	TagContainer& TagComponent::getTags()
	{
		static TagContainer s_empty;
		TagData*			pData = getTagData();
		if ( pData != nullptr )
			return pData->tags;
		return s_empty;
	}

	const TagContainer& TagComponent::getTags() const
	{
		static TagContainer s_empty;
		const TagData*		pData = getTagData();
		if ( pData != nullptr )
			return pData->tags;
		return s_empty;
	}

	void TagComponent::addTag( TagID tag )
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			pGameObject->addTag( tag );
	}

	void TagComponent::removeTag( TagID tag )
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			pGameObject->removeTag( tag );
	}

	void TagComponent::clearTags()
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			pGameObject->clearTags();
	}

	bool TagComponent::hasTag( TagID tag, bool bExactMatch ) const
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			return pGameObject->hasTag( tag, bExactMatch );
		return false;
	}

	bool TagComponent::matchTags( const TagContainer& required, const TagContainer& forbidden ) const
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			return pGameObject->matchTags( required, forbidden );
		return required.getTagCount() == 0;
	}

	bool TagComponent::matchesQuery( const TagQuery& query ) const
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			return pGameObject->matchesTagQuery( query );
		static const TagContainer s_emptyTags;
		return query.matches( s_emptyTags );
	}
} // namespace sw
