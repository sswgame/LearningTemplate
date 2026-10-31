#include "pch.h"

#include "Engine/Automation/AutomationCaptureUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Environment/Foliage/FoliageComponent.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Scene/SceneManager.h"

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

namespace sw
{
    namespace
    {
        /**
         * @brief 활성 씬의 폴리지가 지형 편집을 아직 따라가지 못했으면 true 입니다 - 편집 뒤 가라앉는 시간과 다시 계산 사이에 찍힌 사진은 편집 전 배치를 담는다.
         *        시나리오가 고정 대기로 그 시간을 넉넉히 흘려 숨기던 것을 캡처 쪽이 기다린다(flowterrain 의 지형 편집 뒤 나무 한 그루가 남았다).
         *        컴포넌트는 런타임 사실(`FoliageComponent::isBehindTerrainEdits`)만 내고, 캡처를 미루는 판단은 이 자동화 층이 한다.
         */
        bool isFoliageBehindTerrain()
        {
            const GameObjectManager* pManager = engine::findActiveObjectManager();
            bool                     bBehind  = false;
            if ( pManager != nullptr )
                pManager->forEachComponentOfType<FoliageComponent>( [&bBehind]( const FoliageComponent* pFoliage )
                { bBehind = bBehind || pFoliage->isBehindTerrainEdits(); } );
            return bBehind;
        }
    } // namespace

    SW_AUTOMATION_CAPTURE_BLOCKER( foliageFollowingTerrain, "FoliageFollowingTerrain", &isFoliageBehindTerrain );
} // namespace sw
