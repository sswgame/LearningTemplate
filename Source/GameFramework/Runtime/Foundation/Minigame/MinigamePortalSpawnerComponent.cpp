#include "pch.h"

#include "GameFramework/Runtime/Foundation/Minigame/MinigamePortalSpawnerComponent.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Reflection/TypeInfo.h"

#include "GameFramework/Runtime/Foundation/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigamePreloadTriggerComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameTriggerComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    MinigamePortalSpawnerComponent::MinigamePortalSpawnerComponent()
        : _portalPrefab{}
        , _columnCount{ 8 }
        , _preloadPrefab{}
        , _slotSpacing{ 7.0f, 7.0f }
        , _listPortal{}
    {
    }

    void MinigamePortalSpawnerComponent::onBeginPlay()
    {
        SceneComponent::onBeginPlay();
        GameObject* const         pOwner   = getOwner();
        GameObjectManager* const  pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        const MinigameFlow* const pFlow    = game::findSubsystem<MinigameFlow>();
        if ( pManager != nullptr && pFlow != nullptr )
            spawnPortals( *pManager, *pFlow );
    }

    void MinigamePortalSpawnerComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        for ( const GameObjectHandle& handle : _listPortal )
        {
            GameObject* const pPortal = pManager != nullptr ? pManager->resolveGameObject( handle ) : nullptr;
            if ( pPortal != nullptr )
                pManager->destroyGameObject( pPortal );
        }
        _listPortal.clear();
        SceneComponent::onEndPlay();
    }

    float3 MinigamePortalSpawnerComponent::computeSlotPosition( int32 slot ) const
    {
        const int32  column = slot % _columnCount;
        const int32  row    = slot / _columnCount;
        const float3 offset{ static_cast<float32>( column ) * _slotSpacing._x, 0.0f, static_cast<float32>( row ) * _slotSpacing._y };
        return getWorldPosition() + offset;
    }

    template <typename TTrigger>
    TTrigger* MinigamePortalSpawnerComponent::spawnVolume( GameObjectManager& manager, const AssetRef<PrefabAsset>& prefab, const utf8* pName, const MinigameDefinition& definition )
    {
        GameObject* const     pVolume  = game::getSubsystem<AssetManager>().getAssetCache<PrefabCache>().spawn( &manager, prefab.path(), SpawnLifetime::Derived, pName );
        TTrigger* const       pTrigger = pVolume != nullptr ? pVolume->getComponent<TTrigger>() : nullptr;
        SceneComponent* const pRoot    = pVolume != nullptr ? pVolume->getPrimarySceneComponent() : nullptr;
        if ( pTrigger == nullptr || pRoot == nullptr )
        {
            SW_LOG_ERROR_ID( "MinigamePortal.PrefabRefused", "Minigame portal '%#': the prefab '%#' could not be spawned or has no scene component with a %#", definition._name.c_str(), prefab.path(),
                             TTrigger::StaticType()->_name.c_str() );
            if ( pVolume != nullptr )
                manager.destroyGameObject( pVolume );
            return nullptr;
        }
        pTrigger->setMinigame( definition._name );
        pRoot->setWorldPosition( computeSlotPosition( definition._portalSlot ) );
        _listPortal.push_back( pVolume->getHandle() );
        return pTrigger;
    }

    void MinigamePortalSpawnerComponent::spawnPortals( GameObjectManager& manager, const MinigameFlow& flow )
    {
        if ( _portalPrefab.empty() || game::findSubsystem<AssetManager>() == nullptr || _columnCount < 1 )
        {
            SW_LOG_ERROR_ID( "MinigamePortal.CannotSpawn", "Minigame portal spawner '%#' cannot spawn - it needs a portal prefab, the asset manager and at least one column (columns %#)",
                             getOwner()->getName().c_str(), _columnCount );
            return;
        }
        vector<MinigameDefinition> listPortal;
        flow.collectPortals( listPortal );
        for ( const MinigameDefinition& definition : listPortal )
        {
            StringBuilder<constant::buffer::kMax256> nameBuilder;
            nameBuilder.appendFormat( "%#%#", kPortalNamePrefix, definition._name.c_str() );
            MinigameTriggerComponent* const pTrigger = spawnVolume<MinigameTriggerComponent>( manager, _portalPrefab, nameBuilder.c_str(), definition );
            if ( pTrigger == nullptr )
                return; // 같은 프리팹이라 나머지도 진다
            pTrigger->setSubLevelOffset( definition._portalOffset );
            if ( _preloadPrefab.empty() )
                continue;
            nameBuilder.clear();
            nameBuilder.appendFormat( "%#%#", kPreloadNamePrefix, definition._name.c_str() );
            if ( spawnVolume<MinigamePreloadTriggerComponent>( manager, _preloadPrefab, nameBuilder.c_str(), definition ) == nullptr )
                return;
        }
    }
} // namespace sw
