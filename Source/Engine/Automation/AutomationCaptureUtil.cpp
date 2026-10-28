#include "pch.h"

#include "Engine/Automation/AutomationCaptureUtil.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Resource/AssetManager.h"

namespace sw
{
    uint32 AutomationCaptureUtil::countPendingAssetWork( const GameObjectManager* pManager )
    {
        uint32 count = 0;
        if ( pManager != nullptr )
            count += static_cast<uint32>( pManager->getPrimitiveRegistry().countAssetWaiters() );
        if ( engine::areEngineServicesBound() )
            count += engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().countLoadsInFlight();
        return count;
    }

    bool AutomationCaptureUtil::isReadyForCapture( const GameObjectManager* pManager )
    {
        return AutomationStepRegistry::findCaptureBlocker() == nullptr && countPendingAssetWork( pManager ) == 0;
    }
} // namespace sw
