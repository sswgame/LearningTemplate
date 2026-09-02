#include "pch.h"

#include "Engine/Object/Component/TagComponent.h"

namespace sw
{
    TagComponent::TagComponent()
        : _tags{}
    {
        _bCanEverTick = SW_FALSE;
    }

    void TagComponent::onBeginPlay()
    {
        Component::onBeginPlay();
    }

    TagContainer& TagComponent::getTags()
    {
        return _tags;
    }

    const TagContainer& TagComponent::getTags() const
    {
        return _tags;
    }

    void TagComponent::addTag( TagID tag )
    {
        _tags.addTag( tag );
    }

    void TagComponent::removeTag( TagID tag )
    {
        _tags.removeTag( tag );
    }

    void TagComponent::clearTags()
    {
        _tags.clear();
    }

    bool TagComponent::hasTag( TagID tag, bool bExactMatch ) const
    {
        return _tags.hasTag( tag, bExactMatch );
    }

    bool TagComponent::matchTags( const TagContainer& required, const TagContainer& forbidden ) const
    {
        return _tags.matchTags( required, forbidden );
    }

    bool TagComponent::matchesQuery( const TagQuery& query ) const
    {
        return query.matches( _tags );
    }
} // namespace sw
