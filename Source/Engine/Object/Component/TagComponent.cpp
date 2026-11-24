#include "pch.h"

#include "Engine/Object/Component/TagComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/TagIndex.h"

namespace sw
{
    TagComponent::TagComponent()
        : _tags{}
        , _spawnTags{}
        , _pTagIndex{ nullptr }
    {
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
        captureSpawnTags();
        _tags.addTag( tag );
        syncTagIndex();
    }

    void TagComponent::removeTag( TagID tag )
    {
        if ( isStructureFrozen() )
        {
            getOwner()->removeTag( tag );
            return;
        }
        captureSpawnTags();
        _tags.removeTag( tag );
        syncTagIndex();
    }

    void TagComponent::clearTags()
    {
        if ( isStructureFrozen() )
        {
            getOwner()->clearTags();
            return;
        }
        captureSpawnTags();
        _tags.clear();
        syncTagIndex();
    }

    void TagComponent::captureSpawnTags()
    {
        if ( _spawnTags.has_value() == false )
            _spawnTags = _tags;
    }

    SpawnStateReset TagComponent::resetToSpawnState()
    {
        if ( _spawnTags.has_value() )
        {
            _tags = std::move( *_spawnTags );
            _spawnTags.reset();
            syncTagIndex();
        }
        return SpawnStateReset::Reset;
    }

    void TagComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _pTagIndex = &manager.getTagIndex();
        syncTagIndex();
    }

    void TagComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getTagIndex().remove( *this );
        _pTagIndex = nullptr;
        Component::onUnregister( manager );
    }

    void TagComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        syncTagIndex();
    }

    void TagComponent::onPostLoad()
    {
        Component::onPostLoad();
        _spawnTags.reset(); // 저작 값이 새로 실렸다
        syncTagIndex();
    }

    void TagComponent::syncTagIndex()
    {
        if ( _pTagIndex == nullptr )
            return;
        _pTagIndex->update( *this, getOwner(), _tags );
        GameObjectEvents::broadcast( *getOwner(), &GameObjectEvents::_tagsChanged, _tags );
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
