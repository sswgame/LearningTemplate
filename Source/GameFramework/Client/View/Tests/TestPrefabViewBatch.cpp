// PrefabViewBatchTest — 프리팹 뷰 묶음: 세울 수 없는 프리팹은 오류를 한 번 남기고 다시 해 보지 않으며, 지우면 처음 상태로 돌아간다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"

#include "GameFramework/Client/View/PrefabViewBatch.h"

using namespace sw;

/** @brief [PrefabViewBatchTest] 빈 프리팹은 오류이고 보이지 않는다 — 프레임마다 같은 오류를 쏟지 않고, 지운 뒤에는 다시 해 본다 */
SW_TEST_CASE( PrefabViewBatchTest, UnusablePrefabIsRefusedOnce )
{
    SW_EXPECT_LOG( "PrefabViewBatch.PrefabUnusable" );
    GameObjectManager           manager;
    PrefabViewBatch             batch;
    const AssetRef<PrefabAsset> noPrefab;
    const float3                unitScale{ 1.0f, 1.0f, 1.0f };
    for ( int32 frame = 0; frame < 2; ++frame )
    {
        batch.begin();
        SW_EXPECT_FALSE( batch.show( manager, noPrefab, AbsolutePosition{}, unitScale, 0.0f, float4{} ) );
        batch.end( manager );
        SW_EXPECT_EQUAL( 0u, batch.getShownCount() );
    }
    batch.destroyAll( manager );
    batch.begin();
    SW_EXPECT_FALSE( batch.show( manager, noPrefab, AbsolutePosition{}, unitScale, 0.0f, float4{} ) );
}
