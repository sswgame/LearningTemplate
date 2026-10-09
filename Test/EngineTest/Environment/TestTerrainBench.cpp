// 지형 벤치 — 눈이 환경 쇼케이스의 지형(valley 256 m, 청크 32 칸, LOD 거리 48 m)을 가로지를 때 LOD 가 바뀌는 프레임의 게임 스레드 비용
// (LOD 판정 · 청크 메시 재생성 · GPU 씬 수집). 값은 Release 로 읽는다.
#include "pch.h"

#include "Core/Time/MonotonicClock.h"

#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Scene/GpuSceneBuilder.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "TestFramework/TestBench.h"
#include "TestFramework/TestFramework.h"

/**
 * @brief [TerrainBenchTest] 눈이 지형을 가로지르는 600 프레임 — LOD 교체 프레임의 GT 비용(판정 · 청크 메시 · GPU 씬 수집)
 * @details 쇼케이스 씬을 그대로 세워(머티리얼 · 식생 포함) 눈을 x 축으로 250 m(60 fps 로 10 초, 초속 25 m) 옮기며 프레임마다 updateLods ·
 *          GpuSceneBuilder::buildFromScene 을 잰다. 교체가 있었던 프레임만 따로 모은다. 렌더 스레드의 정점 풀 재생성(`RT.GpuScene.vertexPool`)은
 *          여기 없다 — App 의 프로파일 표로 본다.
 */
SW_TEST_CASE( TerrainBenchTest, LodSweepWorstFrame )
{
    constexpr uint32 kFrameCount = 600;
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::SceneDocument doc;
    SW_ASSERT_TRUE( doc.loadXml( "game/empty/maps/envshowcase.scene.xml" ) );
    sw::Scene scene{ "TerrainBench" };
    SW_ASSERT_TRUE( scene.instantiate( doc ) );
    sw::GameObject* pObject = scene.getObjectManager()->findGameObjectByName( "Terrain" );
    SW_ASSERT_NOT_NULL( pObject );
    sw::TerrainComponent* pTerrain = pObject->getComponent<sw::TerrainComponent>();
    SW_ASSERT_NOT_NULL( pTerrain );

    sw::GpuSceneBuilder builder;
    sw::vector<int64>   listAll;
    sw::vector<int64>   listSwapLod;
    sw::vector<int64>   listSwapBuild;
    for ( uint32 frame = 0; frame < kFrameCount; ++frame )
    {
        const float32       sweep = static_cast<float32>( frame ) / static_cast<float32>( kFrameCount - 1 );
        const sw::float3    eye{ -125.0f + 250.0f * sweep, 20.0f, 3.0f };
        const sw::Stopwatch lodWatch;
        const uint32        rebuiltCount = pTerrain->updateLods( eye );
        const int64         lodMicros    = lodWatch.getElapsedNanoseconds() / 1000;
        const sw::Stopwatch buildWatch;
        builder.buildFromScene( &scene, eye );
        const int64 buildMicros = buildWatch.getElapsedNanoseconds() / 1000;
        listAll.push_back( lodMicros + buildMicros );
        if ( rebuiltCount > 0 )
        {
            listSwapLod.push_back( lodMicros );
            listSwapBuild.push_back( buildMicros );
        }
    }
    SW_EXPECT_TRUE_MSG( listSwapLod.empty() == false, "the sweep never changed a LOD - the bench measures nothing" );
    test::logBenchSamples( "terrain sweep  every frame (updateLods + GpuScene build)", listAll );
    test::logBenchSamples( "terrain sweep  LOD swap frames: updateLods (chunk meshes)", listSwapLod );
    test::logBenchSamples( "terrain sweep  LOD swap frames: GpuScene build", listSwapBuild );
}
