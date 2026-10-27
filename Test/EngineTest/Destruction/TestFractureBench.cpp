// 파괴 벤치 — 쇼케이스(파괴물 여섯 · 잎 312)의 플레이 첫 프레임, 잎 셰이프 미리 짓기가 끝나기까지의 프레임, 벽(200 조각)이 부서지는 프레임. 값은 Release 로 읽는다.
#include "pch.h"

#include "Core/Time/MonotonicClock.h"

#include "Engine/Destruction/FractureComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_TEST_LOAD_SUITE( FractureBenchTest, "fracture begin-play, leaf shape warm-up and wall break frame cost" );
SW_TEST_TIME_BUDGET( FractureBenchTest, 2000, "Release 케이스 최대 383 ms(2026-10-15, Ryzen 7 6800H, 세 번 중 최대)의 네 배 — CI 러너 여유" );

namespace
{
    struct FractureBenchInternal
    {
        static constexpr const utf8* kScenePath  = "game/empty/maps/destructionshowcase.scene.xml";
        static constexpr uint32      kRoundCount = 3;
        /// 미리 짓기를 기다리는 시간 상한이다(프레임 수가 아니라 시간 — 예산이 벽시계 창마다 채워지므로 느린 구성에서도 같은 뜻이다).
        static constexpr int64 kMaxWarmMilliseconds = 30000;
        /// 도화선 드럼통(게임 시간 1.5 초)이 터지지 않게 짧은 프레임 시간으로 돈다.
        static constexpr float32 kWarmFrameSeconds = 1.0e-6f;

        /** @brief 쇼케이스의 파괴물 여섯입니다. */
        static void collectFractures( sw::GameObjectManager& manager, sw::vector<sw::FractureComponent*>& outListFracture )
        {
            outListFracture.clear();
            for ( const utf8* pName : { "BrickWall", "BarrelFuse", "BarrelChain", "CrateLeft", "CrateRight", "CrateStacked" } )
            {
                sw::GameObject* pObject = manager.findGameObjectByName( sw::hashed_string( pName ) );
                if ( pObject != nullptr && pObject->getComponent<sw::FractureComponent>() != nullptr )
                    outListFracture.push_back( pObject->getComponent<sw::FractureComponent>() );
            }
        }

        static uint32 sumPendingLeafShapes( const sw::vector<sw::FractureComponent*>& listFracture )
        {
            uint32 pendingCount = 0;
            for ( const sw::FractureComponent* pFracture : listFracture )
            {
                pendingCount += pFracture->getPendingLeafShapeCount();
            }
            return pendingCount;
        }

        /** @brief 남은 잎 셰이프가 0 이 될 때까지 짧은 프레임으로 돕니다. 돈 프레임 수를 반환하고, 가장 긴 프레임을 @p outWorstMicros 에 씁니다. */
        static uint32 updateUntilWarm( sw::GameObjectManager& manager, const sw::vector<sw::FractureComponent*>& listFracture, int64& outWorstMicros )
        {
            outWorstMicros                = 0;
            uint32             frameCount = 0;
            const sw::Deadline deadline   = sw::Deadline::afterMilliseconds( kMaxWarmMilliseconds );
            while ( deadline.isExpired() == false && sumPendingLeafShapes( listFracture ) > 0 )
            {
                const sw::Stopwatch stopwatch;
                manager.update( kWarmFrameSeconds );
                const int64 micros = stopwatch.getElapsedNanoseconds() / 1000;
                outWorstMicros     = micros > outWorstMicros ? micros : outWorstMicros;
                ++frameCount;
            }
            return frameCount;
        }

        /** @brief 벽 가운데를 터뜨리고 그 피해를 적용하는 한 프레임의 시간(us)을 반환합니다. */
        static int64 breakWallFrame( sw::GameObjectManager& manager, sw::FractureComponent& wall )
        {
            wall.applyRadialDamageAtWorld( sw::float3{ 0.0f, 1.5f, 0.2f }, 1.5f, 2000.0f, 300.0f );
            const sw::Stopwatch stopwatch;
            manager.update( 1.0f / 60.0f );
            return stopwatch.getElapsedNanoseconds() / 1000;
        }
    };
} // namespace

/**
 * @brief [FractureBenchTest] 파괴 쇼케이스의 beginPlay + 첫 틱 — 상태를 세우고 잎 껍질 점을 만드는 한 프레임(다섯 판)
 * @details 씬을 판마다 새로 세워 beginPlay 와 첫 틱(파괴물마다 첫 물리 프레임에서 상태 · 잎 점을 세우고 셰이프를 예산만큼 짓는다)을 측정한다.
 *          전/후 비교: `-gv_fractureShapeWarmMicroseconds=-1` 이 예전(첫 프레임에 모두 짓기, 잎 312 개 p50 ~20 ms)이다.
 */
SW_TEST_CASE( FractureBenchTest, ShowcaseBeginPlay )
{
    constexpr uint32 kRoundCount = 5;
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::SceneDocument doc;
    SW_ASSERT_TRUE( doc.loadXML( sw::ResourceID{ FractureBenchInternal::kScenePath } ) );
    sw::vector<int64> listMicros;
    for ( uint32 round = 0; round < kRoundCount; ++round )
    {
        sw::Scene scene{ "FractureBench" };
        SW_ASSERT_TRUE( scene.instantiate( doc ) );
        const sw::Stopwatch stopwatch;
        scene.getObjectManager()->beginPlay();
        scene.getObjectManager()->update( 1.0f / 60.0f );
        listMicros.push_back( stopwatch.getElapsedNanoseconds() / 1000 );
        scene.getObjectManager()->endPlay();
    }
    SW_EXPECT_EQUAL( static_cast<size_t>( kRoundCount ), listMicros.size() );
    test::logBenchSamples( "destructionshowcase beginPlay + first tick (leaf points + warm budget)", listMicros );
}

/**
 * @brief [FractureBenchTest] 잎 셰이프 미리 짓기 — 남은 잎 셰이프가 0 이 되기까지의 프레임 수와 그 동안 가장 긴 프레임(세 판)
 * @details 기본 예산(500 us / 4 ms 창)이면 가장 긴 프레임이 예산 + 씬 틱 정도여야 한다. 프레임 수는 측정값이다(판정하지 않는다).
 */
SW_TEST_CASE( FractureBenchTest, ShowcaseLeafShapeWarmUp )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::SceneDocument doc;
    SW_ASSERT_TRUE( doc.loadXML( sw::ResourceID{ FractureBenchInternal::kScenePath } ) );
    sw::vector<int64> listWorstMicros;
    sw::vector<int64> listFrameCount;
    for ( uint32 round = 0; round < FractureBenchInternal::kRoundCount; ++round )
    {
        sw::Scene scene{ "FractureBenchWarm" };
        SW_ASSERT_TRUE( scene.instantiate( doc ) );
        sw::GameObjectManager&             manager = *scene.getObjectManager();
        sw::vector<sw::FractureComponent*> listFracture;
        FractureBenchInternal::collectFractures( manager, listFracture );
        SW_ASSERT_EQUAL( static_cast<size_t>( 6 ), listFracture.size() );
        manager.beginPlay();
        manager.update( FractureBenchInternal::kWarmFrameSeconds );
        int64        worstMicros = 0;
        const uint32 frameCount  = FractureBenchInternal::updateUntilWarm( manager, listFracture, worstMicros );
        SW_EXPECT_EQUAL( 0u, FractureBenchInternal::sumPendingLeafShapes( listFracture ) );
        listWorstMicros.push_back( worstMicros );
        listFrameCount.push_back( static_cast<int64>( frameCount ) );
        manager.endPlay();
    }
    test::logBenchSamples( "destructionshowcase leaf shape warm-up worst frame (us)", listWorstMicros );
    test::logBenchSamples( "destructionshowcase leaf shape warm-up frame count", listFrameCount );
}

/**
 * @brief [FractureBenchTest] 벽(200 조각)이 부서지는 프레임 — 잎 셰이프를 미리 짓기 전(첫 틱 바로 뒤)과 다 지은 뒤(세 판씩)
 * @details 앞의 것이 "처음 깨질 때까지 미루기" 의 최악이고 뒤의 것이 미리 짓기가 끝난 뒤다. 차이가 남은 잎 셰이프를 그 자리에서 짓는 비용이다.
 */
SW_TEST_CASE( FractureBenchTest, WallBreakFrameColdAndWarm )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::SceneDocument doc;
    SW_ASSERT_TRUE( doc.loadXML( sw::ResourceID{ FractureBenchInternal::kScenePath } ) );
    sw::vector<int64> listColdMicros;
    sw::vector<int64> listWarmMicros;
    for ( uint32 round = 0; round < FractureBenchInternal::kRoundCount * 2; ++round )
    {
        const bool bWarm = ( round % 2 ) == 1;
        sw::Scene  scene{ "FractureBenchBreak" };
        SW_ASSERT_TRUE( scene.instantiate( doc ) );
        sw::GameObjectManager&             manager = *scene.getObjectManager();
        sw::vector<sw::FractureComponent*> listFracture;
        FractureBenchInternal::collectFractures( manager, listFracture );
        SW_ASSERT_FALSE( listFracture.empty() );
        sw::FractureComponent& wall = *listFracture[0];
        manager.beginPlay();
        manager.update( FractureBenchInternal::kWarmFrameSeconds );
        int64 worstMicros = 0;
        if ( bWarm )
            (void)FractureBenchInternal::updateUntilWarm( manager, listFracture, worstMicros );
        const int64 micros = FractureBenchInternal::breakWallFrame( manager, wall );
        SW_EXPECT_TRUE( wall.isFractured() );
        SW_EXPECT_EQUAL( 0u, wall.getPendingLeafShapeCount() );
        ( bWarm ? listWarmMicros : listColdMicros ).push_back( micros );
        manager.endPlay();
    }
    test::logBenchSamples( "destructionshowcase wall break frame, leaf shapes cold (us)", listColdMicros );
    test::logBenchSamples( "destructionshowcase wall break frame, leaf shapes warm (us)", listWarmMicros );
}
