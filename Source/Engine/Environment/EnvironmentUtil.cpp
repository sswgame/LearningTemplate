#include "pch.h"

#include "Engine/Environment/EnvironmentUtil.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"

namespace sw
{
    bool EnvironmentUtil::findViewPosition( const GameObjectManager& manager, float3& outPosition )
    {
        float3 forward{};
        return findView( manager, outPosition, forward );
    }

    bool EnvironmentUtil::findView( const GameObjectManager& manager, float3& outPosition, float3& outForward )
    {
        const CameraRegistry&  registry = manager.getCameraRegistry();
        const CameraComponent* pCamera  = registry.selectCamera( CameraRole::Game );
        if ( pCamera == nullptr && registry.getAll().empty() == false )
            pCamera = registry.getAll().front();
        if ( pCamera == nullptr )
            return false;
        outPosition = pCamera->getCameraPosition();
        outForward  = pCamera->getCameraForward();
        return true;
    }
} // namespace sw
