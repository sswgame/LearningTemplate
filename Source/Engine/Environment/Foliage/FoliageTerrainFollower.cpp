#include "pch.h"

#include "Engine/Environment/Foliage/FoliageTerrainFollower.h"

#include "Core/Container/vector.h"

#include "Engine/Environment/Foliage/FoliageComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SW_SCENE_SERVICE( FoliageTerrainFollower, FlushEnd )

    FoliageTerrainFollower::FoliageTerrainFollower( GameObjectManager& manager )
        : _manager{ manager }
    {
    }

    void FoliageTerrainFollower::update( float32 /*deltaTime*/ )
    {
        // 순회 안에서는 레지스트리를 바꾸지 않는다(`forEachComponentOfType` 의 공유 잠금) - 차례가 온 것을 모은 뒤 밖에서 다시 계산한다.
        vector<FoliageComponent*> listReady;
        _manager.forEachComponentOfType<FoliageComponent>( [this, &listReady]( FoliageComponent* pFoliage )
        {
            if ( pFoliage->isPendingDestroy() == false && pFoliage->followTerrainEdits( _manager ) )
                listReady.push_back( pFoliage );
        } );
        for ( FoliageComponent* pFoliage : listReady )
        {
            pFoliage->refreshFromTerrainEdits();
        }
    }
} // namespace sw
