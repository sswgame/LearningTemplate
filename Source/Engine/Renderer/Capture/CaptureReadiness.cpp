#include "pch.h"

#include "Engine/Renderer/Capture/CaptureReadiness.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/RenderThreadSubsystem.h"

namespace sw
{
    uint32 CaptureReadiness::countPendingSceneJobs( const GameObjectManager* pManager )
    {
        uint32 count = 0;
        if ( pManager != nullptr )
        {
            count += static_cast<uint32>( pManager->getPrimitiveRegistry().countAssetWaiters() );
            count += pManager->getSubsystem<SceneNavigation>().getWaitingMeshCount();
        }
        if ( engine::areEngineServicesBound() )
            count += engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().countLoadsInFlight();
        return count;
    }

    uint32 CaptureReadiness::countPendingRenderJobs( uint32 sceneKey )
    {
        const RenderThreadSubsystem* const pRenderThreads = engine::findSubsystem<RenderThreadSubsystem>();
        const RenderThread* const          pRenderThread  = pRenderThreads != nullptr ? pRenderThreads->findRenderThread() : nullptr;
        if ( pRenderThread == nullptr )
            return 0; // 렌더 스레드가 없는 호스트(전용 서버)는 그릴 것이 없다
        uint32 count = 0;
        return pRenderThread->findPendingMaterialPSOBatchCount( sceneKey, count ) ? count : 1u;
    }

    bool CaptureReadiness::isFrameComplete( const FrameRenderer& renderer )
    {
        return renderer.getLastPendingPSOBatchCount() == 0;
    }
} // namespace sw
