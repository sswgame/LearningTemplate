#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorNavigationCommands.h"

#include "Engine/Navigation/Component/SceneNavigation.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorNavigationCommands" );

    uint32 EditorNavigationCommands::bakeAll( GameObjectManager& manager )
    {
        SceneNavigation&      navigation = manager.getSubsystem<SceneNavigation>();
        const NavMeshSettings settings   = navigation.getSettings();
        navigation.setSettings( settings ); // 같은 테이블을 다시 걸어 마련해 둔 내비메시를 버린다
        uint32 bakedCount{ 0 };
        for ( const NavAgentTypeDef& agentType : settings._listAgentType )
        {
            const INavMesh* pNavMesh = navigation.ensureNavMesh( agentType._name );
            if ( pNavMesh != nullptr && pNavMesh->getPolygonCount() > 0 ) // 기하가 없는 씬도 ±1 상자로 초기화돼 빈 메시가 서지만 그것은 베이크가 아니다
                ++bakedCount;
        }
        navigation.flushTileBakes();
        SW_LOG_INFO( "Navigation baked: %# of %# agent type(s) have a navmesh", bakedCount, static_cast<uint32>( settings._listAgentType.size() ) );
        return bakedCount;
    }

    uint32 EditorNavigationCommands::countBakedMeshes( GameObjectManager& manager )
    {
        SceneNavigation& navigation = manager.getSubsystem<SceneNavigation>();
        uint32           count{ 0 };
        for ( const NavAgentTypeDef& agentType : navigation.getSettings()._listAgentType )
        {
            const INavMesh* pNavMesh = navigation.findNavMesh( agentType._name );
            if ( pNavMesh != nullptr && pNavMesh->getPolygonCount() > 0 )
                ++count;
        }
        return count;
    }
} // namespace sw::editor
