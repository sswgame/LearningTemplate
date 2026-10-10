// 파괴 벤치 — 쇼케이스(파괴물 여섯 · 잎 312)의 플레이 첫 프레임(첫 물리 스텝 앞에 파괴 상태를 세우며 잎 볼록 껍질을 짓는다). 값은 Release 로 읽는다.
#include "pch.h"

#include "Core/Time/MonotonicClock.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "TestFramework/TestBench.h"
#include "TestFramework/TestFramework.h"

/**
 * @brief [FractureBenchTest] 파괴 쇼케이스의 beginPlay + 첫 틱 — 잎 볼록 껍질을 짓는 한 프레임(다섯 판)
 * @details 씬을 판마다 새로 세워 beginPlay 와 첫 틱(파괴물마다 첫 물리 스텝 앞에서 상태 · 잎 셰이프를 세운다)을 잰다.
 *          기준: 잎마다 ~80 us(Release)면 잎 312 개에 ≈ 25 ms.
 */
SW_TEST_CASE( FractureBenchTest, ShowcaseBeginPlay )
{
    constexpr uint32 kRoundCount = 5;
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::SceneDocument doc;
    SW_ASSERT_TRUE( doc.loadXML( "game/empty/maps/destructionshowcase.scene.xml" ) );
    sw::vector<int64> listMicros;
    for ( uint32 round = 0; round < kRoundCount; ++round )
    {
        sw::Scene scene{ "FractureBench" };
        SW_ASSERT_TRUE( scene.instantiate( doc ) );
        const sw::Stopwatch stopwatch;
        scene.getObjectManager()->beginPlay();
        scene.getObjectManager()->tick( 1.0f / 60.0f );
        listMicros.push_back( stopwatch.getElapsedNanoseconds() / 1000 );
        scene.getObjectManager()->endPlay();
    }
    SW_EXPECT_EQUAL( static_cast<size_t>( kRoundCount ), listMicros.size() );
    test::logBenchSamples( "destructionshowcase beginPlay + first tick (leaf hulls)", listMicros );
}
