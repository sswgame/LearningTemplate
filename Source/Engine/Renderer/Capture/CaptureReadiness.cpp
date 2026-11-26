#include "pch.h"

#include "Engine/Renderer/Capture/CaptureReadiness.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Graphics/View/SimulationOnlyFrameQueue.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
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
        if ( engine::areEngineSubsystemsBound() )
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
        if ( pRenderThread->findPendingMaterialPSOBatchCount( sceneKey, count ) == false )
            count = 1u;
        // 그리지 않고 진행한 프레임 뒤에 아직 따라잡는 중이면(GPU 입자의 밀린 단계 · 그린 프레임이 아직 없다) 주 화면은 덜 그려졌다 — 따라잡기 전 그림을 찍지 않는다.
        const SimulationOnlyFrameQueue* const pSimulationOnly = engine::findSubsystem<SimulationOnlyFrameQueue>();
        if ( sceneKey == 0 && pSimulationOnly != nullptr && pSimulationOnly->isCatchUpPending() )
            ++count;
        return count;
    }

    bool CaptureReadiness::isFrameComplete( const FrameRenderer& renderer )
    {
        return renderer.getLastPendingPSOBatchCount() == 0;
    }
} // namespace sw
