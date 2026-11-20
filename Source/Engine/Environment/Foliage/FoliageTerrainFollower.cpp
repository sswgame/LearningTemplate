#include "pch.h"

#include "Engine/Environment/Foliage/FoliageTerrainFollower.h"

#include "Core/Container/vector.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Environment/Foliage/FoliageComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    SW_SCENE_SERVICE( FoliageTerrainFollower, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

    FoliageTerrainFollower::FoliageTerrainFollower( GameObjectManager& manager )
        : _manager{ manager }
    {
    }

    void FoliageTerrainFollower::update( float32 /*deltaTime*/ )
    {
        // 순회 안에서는 레지스트리를 바꾸지 않는다(`forEachComponentOfType` 의 공유 잠금) - 차례가 온 것을 모은 뒤 밖에서 다시 계산한다.
        vector<FoliageComponent*> listReady;
        for ( FoliageComponent* pFoliage : _manager.getComponentRegistry().getAll<FoliageComponent>() )
        {
            if ( pFoliage->isPendingDestroy() == false && pFoliage->followTerrainEdits( _manager ) )
                listReady.push_back( pFoliage );
        }
        for ( FoliageComponent* pFoliage : listReady )
        {
            pFoliage->refreshFromTerrainEdits();
        }
    }

    namespace
    {
        /**
         * @brief 활성 씬의 폴리지가 지형 편집을 아직 따라가지 못했으면 진행 중(따라가는 폴리지 수)입니다 - 편집 뒤 가라앉는 시간과 다시 계산 사이에 찍힌 사진은 편집 전 배치를 담는다.
         *        시나리오가 고정 대기로 그 시간을 넉넉히 흘려 숨기던 것을 캡처 쪽이 기다린다(flowterrain 의 지형 편집 뒤 나무 한 그루가 남았다).
         *        주인 곁에서 등록한다 - 예전에는 자동화 층(11)의 공용 파일이 이 층(8) 헤더를 읽었다.
         */
        AutomationReadiness readFoliageFollowingTerrain()
        {
            const GameObjectManager* pManager = engine::findActiveObjectManager();
            uint64                   behind   = 0;
            if ( pManager != nullptr )
                for ( const FoliageComponent* pFoliage : pManager->getComponentRegistry().getAll<FoliageComponent>() )
                {
                    behind += pFoliage->isBehindTerrainEdits() ? 1u : 0u;
                }
            return behind > 0 ? AutomationReadiness::waiting( behind, "foliage still follows a terrain edit" ) : AutomationReadiness::ready();
        }
    } // namespace

    SW_AUTOMATION_CAPTURE_BLOCKER( foliageFollowingTerrain, "FoliageFollowingTerrain", &readFoliageFollowingTerrain );
} // namespace sw
