#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigamePortalSpawnerComponent.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabCache.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameTriggerComponent.h"

namespace sw
{
    MinigamePortalSpawnerComponent::MinigamePortalSpawnerComponent()
        : _portalPrefab{}
        , _columnCount{ 8 }
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

    void MinigamePortalSpawnerComponent::spawnPortals( GameObjectManager& manager, const MinigameFlow& flow )
    {
        AssetManager* const pAssetManager = game::findSubsystem<AssetManager>();
        if ( _portalPrefab.empty() || pAssetManager == nullptr || _columnCount < 1 )
        {
            SW_LOG_ERROR_ID( "MinigamePortal.CannotSpawn", "Minigame portal spawner '%#' cannot spawn - it needs a portal prefab, the asset manager and at least one column (columns %#)",
                             getOwner()->getName().c_str(), _columnCount );
            return;
        }
        vector<MinigameDefinition> listPortal;
        flow.collectPortals( listPortal );
        PrefabCache& prefabs = pAssetManager->getAssetCache<PrefabCache>();
        for ( const MinigameDefinition& definition : listPortal )
        {
            StringBuilder<constant::buffer::kMax256> nameBuilder;
            nameBuilder.appendFormat( "%#%#", kPortalNamePrefix, definition._name.c_str() );
            GameObject* const               pPortal  = prefabs.spawn( &manager, _portalPrefab.path(), nameBuilder.c_str() );
            MinigameTriggerComponent* const pTrigger = pPortal != nullptr ? pPortal->getComponent<MinigameTriggerComponent>() : nullptr;
            SceneComponent* const           pRoot    = pPortal != nullptr ? pPortal->getPrimarySceneComponent() : nullptr;
            if ( pTrigger == nullptr || pRoot == nullptr )
            {
                SW_LOG_ERROR_ID( "MinigamePortal.PrefabRefused", "Minigame portal '%#': the prefab '%#' could not be spawned or has no scene component with a MinigameTriggerComponent",
                                 definition._name.c_str(), _portalPrefab.path() );
                if ( pPortal != nullptr )
                    manager.destroyGameObject( pPortal );
                return; // 같은 프리팹이라 나머지도 진다
            }
            pPortal->setTransient( true ); // 정의에서 다시 세우는 파생 — 스냅샷에 실으면 복원 뒤 다시 세운 것과 겹친다
            pTrigger->setMinigame( definition._name );
            pRoot->setWorldPosition( computeSlotPosition( definition._portalSlot ) );
            _listPortal.push_back( pPortal->getHandle() );
        }
    }
} // namespace sw
