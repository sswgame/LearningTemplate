#include "pch.h"

#include "Engine/Object/Component/TagComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    TagComponent::TagComponent()
        : _tags{}
    {
    }

    void TagComponent::onBeginPlay()
    {
        Component::onBeginPlay();
    }

    const TagContainer& TagComponent::getTags() const
    {
        return _tags;
    }

    void TagComponent::addTag( TagID tag )
    {
        if ( isStructureFrozen() )
        {
            getOwner()->addTag( tag ); // 오브젝트의 길이 미룬다 — 틱 뒤에 이 함수로 다시 온다
            return;
        }
        _tags.addTag( tag );
    }

    void TagComponent::removeTag( TagID tag )
    {
        if ( isStructureFrozen() )
        {
            getOwner()->removeTag( tag );
            return;
        }
        _tags.removeTag( tag );
    }

    void TagComponent::clearTags()
    {
        if ( isStructureFrozen() )
        {
            getOwner()->clearTags();
            return;
        }
        _tags.clear();
    }

    bool TagComponent::isStructureFrozen() const
    {
        const GameObject* pOwner = getOwner();
        return pOwner != nullptr && pOwner->getManager() != nullptr && pOwner->getManager()->isStructuralMutationFrozen();
    }

    bool TagComponent::hasTag( TagID tag, bool bExactMatch ) const
    {
        return _tags.hasTag( tag, bExactMatch );
    }

    bool TagComponent::matchesTags( const TagContainer& required, const TagContainer& forbidden ) const
    {
        return _tags.matchesTags( required, forbidden );
    }

    bool TagComponent::matchesQuery( const TagQuery& query ) const
    {
        return query.matches( _tags );
    }
} // namespace sw
