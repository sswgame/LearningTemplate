// DevSceneStreamingAccessTest — 개발 도구의 씬 스트리밍 로드 큐: 아직 오르지 않은 조각을 줄 차례(우선순위 → 거리)로 우선순위 · 거리 · 단계와 함께 내고, 다 오르면 빈다.
#include "pch.h"

#include "Core/Container/span.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Diagnostics/DevSceneStreamingAccess.h"

#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"

using namespace sw;

namespace
{
    /** @brief 테스트 동안 결정적 실행의 "프레임마다 끝내기" 를 끈다 — 켜져 있으면 예산 없이 모두 짓는다. */
    class ScopedStreamingFreeRunningPolicy
    {
    public:
        ScopedStreamingFreeRunningPolicy()
            : _previous{ engine::getSubsystem<DeterministicRun>().getPolicy() }
        {
            DeterministicRunPolicy policy     = _previous;
            policy._bFinishAssetJobsEachFrame = SW_FALSE;
            engine::getSubsystem<DeterministicRun>().setPolicy( policy );
        }
        ~ScopedStreamingFreeRunningPolicy() { engine::getSubsystem<DeterministicRun>().setPolicy( _previous ); }
        ScopedStreamingFreeRunningPolicy( const ScopedStreamingFreeRunningPolicy& )            = delete;
        ScopedStreamingFreeRunningPolicy& operator=( const ScopedStreamingFreeRunningPolicy& ) = delete;

    private:
        DeterministicRunPolicy _previous;
    };

    /** @brief 10 m 셀 (0,0) · (5,0) 에 상자 하나씩이고 프레임마다 엔티티 하나만 짓는 스트리밍 문서입니다(셀 0 에 HLOD 대리 — 파일 id 100). */
    SceneDocument makeDocument()
    {
        SceneDocument doc{};
        doc._name                                        = "Queue";
        doc._sceneSettings._bStreaming                   = true;
        doc._sceneSettings._cellGrid._cellSize           = 10.0f;
        doc._sceneSettings._cellGrid._loadingRadius      = 6.0f;
        doc._sceneSettings._unloadMargin                 = 0.0f;
        doc._sceneSettings._maxCellLoadsPerFrame         = 8;
        doc._sceneSettings._addToWorldBudgetMicroseconds = 1u;
        doc._bValid                                      = true;
        for ( const int32 x : { 0, 5 } )
        {
            SceneDocument::StreamingCell cell{};
            cell._cell = StreamingCellCoord{ x, 0 };
            SceneDocument::SceneObjectNode node{};
            node._fileID = static_cast<uint64>( x + 1 );
            StringBuilder<constant::buffer::kMax256> text;
            text.appendFormat( "Crate%#", static_cast<int64>( x ) );
            node._name = string( text.view() );
            text.clear();
            text.appendFormat( "<GameObject _schemaVersion=\"0\" _name=\"%#\" _bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"%#,0,5\" /></_listComponent></GameObject>", node._name,
                               static_cast<int64>( x * 10 + 5 ) );
            node._embeddedXML = string( text.view() );
            cell._listSceneObjectNode.push_back( std::move( node ) );
            if ( x == 0 )
            {
                SceneDocument::SceneObjectNode proxy{};
                proxy._fileID      = 100;
                proxy._name        = "HLOD_0_0";
                proxy._embeddedXML = "<GameObject _schemaVersion=\"0\" _name=\"HLOD_0_0\" _bActive=\"true\" _bAlwaysLoaded=\"true\"><_listComponent><SceneComponent /></_listComponent></GameObject>";
                cell._hlodFileID   = proxy._fileID;
                doc._listSceneObjectNode.push_back( std::move( proxy ) );
            }
            doc._listStreamingCell.push_back( std::move( cell ) );
        }
        return doc;
    }
} // namespace

/** @brief [DevSceneStreamingAccessTest] 로드 큐는 아직 오르지 않은 조각을 줄 차례로 내고 다 오르면 빈다 — HLOD 대리 수도 조각의 단계를 따른다 */
SW_TEST_CASE( DevSceneStreamingAccessTest, LoadQueueListsPendingFragmentsInOrder )
{
    const ScopedStreamingFreeRunningPolicy policy;
    Scene                                  scene{ "Queue" };
    SW_REQUIRE_TRUE( scene.instantiate( makeDocument() ) );
    SceneStreamingSubsystem& streaming   = scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
    const StreamingSource    arrSource[] = {
        StreamingSource{ float3{ 5.0f, 0.0f, 5.0f }, 0.0f,     StreamingSourcePriority::Low},
        StreamingSource{float3{ 55.0f, 0.0f, 5.0f }, 0.0f, StreamingSourcePriority::Highest}
    };
    SW_REQUIRE_TRUE( streaming.setExternalSources( span<const StreamingSource>( arrSource, 2 ) ) );
    streaming.update( 0.0f ); // 셀 5 만 짓는다 — 셀 0 은 내용을 든 채 짓기를 기다린다

    vector<DevSceneStreamingAccess::LoadQueueEntry> listEntry;
    DevSceneStreamingAccess::collectLoadQueue( streaming, listEntry );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEntry.size() );
    SW_EXPECT_TRUE( listEntry[0]._cell == ( StreamingCellCoord{ 0, 0 } ) );
    SW_EXPECT_TRUE( listEntry[0]._priority == StreamingSourcePriority::Low );
    SW_EXPECT_EQUAL( 0.0f, listEntry[0]._distance );
    SW_EXPECT_TRUE( listEntry[0]._state == StreamingCellState::Ready );
    SW_EXPECT_EQUAL( 1u, DevSceneStreamingAccess::getPendingCellCount( streaming ) );
    SW_EXPECT_EQUAL( 1u, DevSceneStreamingAccess::getHLODCount( streaming ) );
    SW_EXPECT_EQUAL( 1u, DevSceneStreamingAccess::getShownHLODCount( streaming ) );

    streaming.update( 0.0f );
    DevSceneStreamingAccess::collectLoadQueue( streaming, listEntry );
    SW_EXPECT_TRUE( listEntry.empty() );
    SW_EXPECT_EQUAL( 0u, DevSceneStreamingAccess::getPendingCellCount( streaming ) );
    SW_EXPECT_EQUAL( 0u, DevSceneStreamingAccess::getShownHLODCount( streaming ) );
}
