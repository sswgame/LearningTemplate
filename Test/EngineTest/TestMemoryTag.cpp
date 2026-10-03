/**
 * @file TestMemoryTag.cpp
 * @brief 메모리 용도 태그 — 기동 단계 · 서비스 생성 · 씬 로드(워커) · 메시 생성이 제 줄로 세이는지.
 * @details 태그는 진단 구성(`kMemoryTagScopesEnabled`)에서만 걸린다. 그 밖의 구성에서는 건너뛴다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/EngineStartupSequence.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Scene/SceneManager.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief 태그 하나의 "지금 살아 있는 바이트" 입니다. */
    uint64 getLiveBytes( const sw::MemoryProfiler& profiler, sw::MemoryTag tag )
    {
        return profiler.getStats( tag )._currentAllocatedBytes.load();
    }

    /** @brief 태그 하나의 "지금까지 잡은 바이트" 누계입니다(잡았다 놓은 것도 남는다). */
    uint64 getTotalBytes( const sw::MemoryProfiler& profiler, sw::MemoryTag tag )
    {
        return profiler.getStats( tag )._totalAllocatedBytes.load();
    }
} // namespace

/**
 * @brief [MemoryTagTest] 기동 단계마다 Unknown 이 아닌 태그가 있다
 * @details 단계 초기화는 표의 둘째 칸 태그 아래에서 돈다. 새 단계를 Unknown 으로 두면 그 단계의 할당이 모두 Unknown 줄로 간다.
 */
SW_TEST_CASE( MemoryTagTest, EveryStartupStepHasATag )
{
    for ( uint32 stepIndex = 0; stepIndex < static_cast<uint32>( sw::EngineStartupStep::Count ); ++stepIndex )
    {
        const sw::EngineStartupStep step = static_cast<sw::EngineStartupStep>( stepIndex );
        SW_EXPECT_TRUE_MSG( sw::EngineStartupSequence::getStepMemoryTag( step ) != sw::MemoryTag::Unknown, sw::EngineStartupSequence::getStepName( step ) );
    }
    SW_EXPECT_TRUE( sw::EngineStartupSequence::getStepMemoryTag( sw::EngineStartupStep::Task ) == sw::MemoryTag::Task );
    SW_EXPECT_TRUE( sw::EngineStartupSequence::getStepMemoryTag( sw::EngineStartupStep::Reflection ) == sw::MemoryTag::Reflection );
}

/**
 * @brief [MemoryTagTest] 하네스 기동 뒤 주요 태그(태스크 · 리플렉션 · 씬 · 에셋 · 셰이더)에 살아 있는 바이트가 있다
 * @details 하네스는 `EngineLoop` 와 같은 부트스트랩 · 서비스 생성 · 기동 단계 표를 지난다. 서비스 생성(`kServiceMemoryTag`)과 단계 초기화
 *          (`EngineStartupStepList.xxx` 의 태그 칸)가 태그를 걸지 않으면 이 줄들이 0 이다.
 */
SW_TEST_CASE( MemoryTagTest, HarnessStartupIsAttributed )
{
    if constexpr ( sw::kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    const sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr )
        SW_TEST_SKIP( "no active memory profiler in this host" );

    SW_EXPECT_TRUE( getLiveBytes( *pProfiler, sw::MemoryTag::Task ) > 0 );
    SW_EXPECT_TRUE( getLiveBytes( *pProfiler, sw::MemoryTag::Reflection ) > 0 );
    SW_EXPECT_TRUE( getLiveBytes( *pProfiler, sw::MemoryTag::Scene ) > 0 );
    SW_EXPECT_TRUE( getLiveBytes( *pProfiler, sw::MemoryTag::Asset ) > 0 );
    SW_EXPECT_TRUE( getLiveBytes( *pProfiler, sw::MemoryTag::Shader ) > 0 );
    SW_EXPECT_TRUE( getLiveBytes( *pProfiler, sw::MemoryTag::EngineMisc ) > 0 );
}

/**
 * @brief [MemoryTagTest] 워커에서 짓는 씬(비동기 로드)의 할당이 Scene 으로 세인다
 * @details 로드는 `SceneManager::dispatchLoad` 가 만든 태스크가 워커에서 한다(문서 읽기 · GameObject 생성). 태스크는 만든 쪽의 태그를
 *          상속하므로 진입점(`dispatchLoad`)의 Scene 이 워커까지 간다. 씬 채택(`tickTransitions`)을 부르기 **전에** 재서 워커 몫만 본다.
 *          이 시험 스레드의 태그는 Unknown 이라, 기다리며 이 스레드가 대신 실행해도 진입점이 없으면 Scene 이 늘지 않는다.
 */
SW_TEST_CASE( MemoryTagTest, AsyncSceneLoadOnWorkerIsTaggedScene )
{
    if constexpr ( sw::kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    const sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
        SW_TEST_SKIP( "no tracking memory profiler in this host" );

    const sw::string xmlPath = test::makeTempPath( "sw_test_memtag_scene.scene.xml" );
    sw::string       xmlText = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Scene formatVersion=\"1\" name=\"TagTown\">\n  <entities>\n";
    for ( uint32 entityIndex = 0; entityIndex < 64; ++entityIndex )
        xmlText += "    <entity name=\"Villager" + sw::to_string( entityIndex ) + "\"/>\n";
    xmlText += "  </entities>\n</Scene>\n";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath, reinterpret_cast<const uint8*>( xmlText.data() ), static_cast<uint64>( xmlText.size() ) ) );

    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    const sw::MemoryTag outerTag = sw::MemoryProfiler::getCurrentMemoryTag();
    SW_ASSERT_TRUE( outerTag == sw::MemoryTag::Unknown );

    const uint64               sceneBefore = getTotalBytes( *pProfiler, sw::MemoryTag::Scene );
    sw::TaskFuture<sw::Scene*> future      = manager.requestLoadFuture( xmlPath );
    SW_ASSERT_TRUE( future.isValid() );
    SW_EXPECT_TRUE( sw::engine::getTaskManager().waitAll( 5000 ) );
    const uint64 sceneAfterWorker = getTotalBytes( *pProfiler, sw::MemoryTag::Scene );

    for ( int32 stepIndex = 0; stepIndex < 200 && manager.isTransitioning(); ++stepIndex )
    {
        sw::engine::getTaskManager().waitAll();
        manager.tickTransitions();
    }
    SW_EXPECT_TRUE( future.isReady() );
    SW_EXPECT_NOT_NULL( future.get() );

    // GameObject 64 개 + 문서 · 이름표만으로도 수 KB 다. 진입점이 빠지면 워커 몫은 Unknown 으로 가고 Scene 은 그대로다.
    SW_EXPECT_TRUE_MSG( sceneAfterWorker > sceneBefore + 4096, ( sw::string( "Scene bytes grew by " ) + sw::to_string( sceneAfterWorker - sceneBefore ) ).c_str() );
    SW_EXPECT_TRUE( sw::MemoryProfiler::getCurrentMemoryTag() == outerTag );

    manager.shutdown();
}

/**
 * @brief [MemoryTagTest] 메시 생성(`MeshUtil::createPrimitive`)의 정점 · 인덱스가 Mesh 로 세이고, 놓으면 빠진다
 */
SW_TEST_CASE( MemoryTagTest, PrimitiveMeshIsTaggedMesh )
{
    if constexpr ( sw::kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    const sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
        SW_TEST_SKIP( "no tracking memory profiler in this host" );

    const uint64 meshBefore = getLiveBytes( *pProfiler, sw::MemoryTag::Mesh );
    uint64       meshHeld{ 0 };
    {
        sw::shared_ptr<sw::Mesh> sphere = sw::MeshUtil::createPrimitive( "Sphere" );
        SW_ASSERT_NOT_NULL( sphere.get() );
        meshHeld = getLiveBytes( *pProfiler, sw::MemoryTag::Mesh );
        SW_EXPECT_TRUE( meshHeld > meshBefore + 1024 );
    }
    // 정점 · 인덱스는 돌려준다. 렌더 자원 등록부처럼 한 번 자란 표는 남을 수 있어 "거의 다" 로 본다.
    const uint64 meshAfter = getLiveBytes( *pProfiler, sw::MemoryTag::Mesh );
    SW_EXPECT_TRUE( meshAfter < meshBefore + ( meshHeld - meshBefore ) / 4 );
}
